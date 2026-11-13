"""Command-line entry point: ``fprime-auto-checklist``."""

from __future__ import annotations

import argparse
import io
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from . import __version__
from . import clang_tidy as ct
from . import codeql as cq
from .carry import apply_previous, parse_previous, sources_changed
from .checklist import ITEMS, codeql_rule_ids
from .fingerprint import section_fingerprints
from .render import render_json, render_markdown
from .results import NO, REVIEW, UNCHECKED, CodeqlEvidence, Verdict, judge_all
from .runner import ModuleOutsideProject, RunOptions, _git_head, _relative, run_checks
from .stages import (
    KIND_AUDIT,
    KIND_REVIEW,
    STAGE_ORDER,
    Decision,
    apply_decisions,
    build_queue,
    decisions_from_json,
    mark_deferred,
    new_decision,
)

EXIT_OK, EXIT_ITEMS_FAILED, EXIT_USAGE, EXIT_CODEQL_UNAVAILABLE = 0, 1, 2, 3


def _add_run_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--project", type=Path, default=Path("."), help="F Prime project checkout root (default: .)")
    parser.add_argument("--module", type=Path, required=True, help="Component directory, e.g. Svc/CmdDispatcher")
    parser.add_argument("--output", "-o", type=Path, default=Path("checklist.md"), help="Markdown checklist to write")
    parser.add_argument(
        "--json-output", type=Path, default=None, help="Also write the machine-readable results (JSON) here"
    )
    parser.add_argument(
        "--work-dir", type=Path, default=None, help="Scratch dir for logs, DB and SARIF (default: <output>.work)"
    )
    parser.add_argument("--owner", default="", help="Component owner for the header")
    parser.add_argument("--contributors", default="", help="Component contributors for the header")
    parser.add_argument("--verbose", "-v", action="store_true", help="Echo the individual check output")
    build = parser.add_argument_group("build-level checks (D6 build, I2, U4, U5, C1)")
    build.add_argument("--build", action="store_true", help="Run fprime-util build/check for the module (slow)")
    build.add_argument(
        "--deployment",
        type=Path,
        action="append",
        default=[],
        help="Deployment dir for the topology build (C1); repeatable",
    )
    build.add_argument(
        "--target-platform", action="append", default=[], help="Extra toolchain/platform for I2/C1; repeatable"
    )
    build.add_argument("--jobs", "-j", type=int, default=None, help="Parallel build jobs")
    build.add_argument("--coverage-threshold", type=float, default=None, help="Line coverage percent required by U5")
    parser.add_argument(
        "--previous",
        type=Path,
        default=None,
        help="Previous rendering of this checklist: reviewer decisions (YES/NO/N/A) on REVIEW rows are kept, "
        "stamped with the commit they were made at and flagged if the component changed since",
    )
    parser.add_argument(
        "--previous-json",
        type=Path,
        default=None,
        help="JSON written by the previous run (default: <previous>.json beside it); identifies the rows "
        "the tool had left at REVIEW so older tool decisions are never carried as reviewer decisions",
    )
    build.add_argument("--artifacts-dir", type=Path, default=None, help="Published unit-test artifacts root (U1)")
    parser.add_argument(
        "--stage",
        choices=STAGE_ORDER,
        default=None,
        help="Development stage this run is for: design (sections 1-2 due), implementation (1-4), closeout (all, "
        "and every tool-decided row is queued for audit). REVIEW rows of later sections are marked deferred and "
        "the rows to decide now are written to the Hand-off section and the JSON 'queue'",
    )
    parser.add_argument(
        "--no-prepare-build",
        action="store_true",
        help="Do not run 'fprime-util generate/build' for the module before the clang-tidy analysis "
        "(by default the tool builds what the analysis needs and scans that build's output for I2)",
    )
    ci = parser.add_argument_group("CI status (C3; needs GITHUB_TOKEN)")
    ci.add_argument("--repo", default=None, help="owner/name (default: origin remote)")
    ci.add_argument("--sha", default=None, help="commit to inspect (default: HEAD)")
    q = parser.add_argument_group("static analysis (CodeQL or its clang-tidy swap-in)")
    q.add_argument(
        "--analyzer",
        choices=("codeql", "clang-tidy"),
        default="codeql",
        help="Engine for the cpp/fprime/* rules: CodeQL (default; license gated, downloads packs) or the "
        "fprime clang-tidy plugin (offline, no license gate)",
    )
    q.add_argument(
        "--skip-codeql",
        "--skip-analysis",
        dest="skip_codeql",
        action="store_true",
        help="Do not run the static analysis; its items are reported UNCHECKED",
    )
    q.add_argument(
        "--clang-tidy", default=None, help="clang-tidy executable (default: $CLANG_TIDY, plugin build, PATH)"
    )
    q.add_argument(
        "--tidy-plugin",
        default=None,
        help="fprime clang-tidy plugin .so (default: $FPRIME_TIDY_PLUGIN or native/build)",
    )
    q.add_argument(
        "--compile-commands",
        type=Path,
        default=None,
        help="compile_commands.json or the build dir holding it (default: build-fprime-automatic-native*/)",
    )
    q.add_argument("--codeql", default=None, help="Path to the CodeQL CLI (default: $CODEQL or PATH)")
    q.add_argument(
        "--codeql-db", type=Path, default=None, help="Existing CodeQL database of the project (skips database creation)"
    )
    q.add_argument(
        "--sarif",
        type=Path,
        default=None,
        help="Existing SARIF from a previous analysis (skips DB creation and analysis)",
    )
    q.add_argument(
        "--build-command", default=None, help='Build command traced by CodeQL (default: "fprime-util build -j<N>")'
    )
    q.add_argument(
        "--codeql-license-ack",
        choices=("open-source", "ghas"),
        default=None,
        help="Attest the CodeQL CLI terms permit this analysis when no OSI license file is found at --project",
    )
    q.add_argument("--codeql-threads", type=int, default=0, help="Threads for the analysis (0 = all cores)")


def _codeql_phase(
    args: argparse.Namespace, project: Path, module: Path, work_dir: Path, log: io.StringIO
) -> Tuple[CodeqlEvidence, Optional[Path]]:
    """Run (or reuse) the CodeQL analysis and scope it to ``module``."""
    if args.skip_codeql:
        return CodeqlEvidence(ran=False, reason="--skip-codeql given"), None
    module_rel = _relative(module, project)
    rules = codeql_rule_ids()
    sarif = args.sarif
    if sarif is None and args.analyzer == "clang-tidy":
        try:
            sarif = ct.analyze(
                project,
                module,
                work_dir,
                clang_tidy=args.clang_tidy,
                plugin=args.tidy_plugin,
                compile_commands=args.compile_commands,
                jobs=args.jobs,
                tool_version=__version__,
            )
        except ct.ClangTidyError as exc:
            log.write(f"clang-tidy: {exc}\n")
            return CodeqlEvidence(ran=False, tool="clang-tidy", reason=str(exc)), None
        return cq.scope_findings(cq.load_sarif(sarif), module_rel, rules, tool="clang-tidy"), sarif
    if sarif is None:
        decision = cq.license_gate(project, args.codeql_license_ack)
        log.write(f"codeql license gate: {decision.basis}\n")
        if not decision.allowed:
            return CodeqlEvidence(ran=False, reason=decision.basis), None
        try:
            codeql = cq.find_codeql(args.codeql)
            db = args.codeql_db
            if db is None:
                db = work_dir / "codeql-db"
                jobs = args.jobs or os.cpu_count() or 2
                build_command = args.build_command or f"fprime-util build -j{jobs}"
                log.write(f"codeql: creating database with: {build_command}\n")
                cq.create_database(codeql, project, db, build_command, work_dir / "codeql-create.log")
            queries_dir = cq.stage_queries(work_dir)
            cq.install_pack(codeql, queries_dir, work_dir / "codeql-pack-install.log")
            sarif = work_dir / "checklist.sarif"
            cq.analyze(
                codeql, db, sarif, work_dir / "codeql-analyze.log", queries_dir=queries_dir, threads=args.codeql_threads
            )
        except cq.CodeqlError as exc:
            log.write(f"codeql: {exc}\n")
            return CodeqlEvidence(ran=False, reason=str(exc)), None
    tool = "clang-tidy" if args.analyzer == "clang-tidy" else "CodeQL"
    evidence = cq.scope_findings(cq.load_sarif(sarif), module_rel, rules, tool=tool)
    return evidence, sarif


def _prepare_build(
    args: argparse.Namespace, project: Path, module: Path, work_dir: Path, log: io.StringIO
) -> Optional[Path]:
    """Build the module for the clang-tidy analysis (generate the cache when missing, then
    ``fprime-util build`` in the module directory); returns the captured build log.

    This is the one build the checker performs itself: the analysis needs the autocode and a
    ``compile_commands.json``.  Everything else the checklist asks about (unit-test runs,
    coverage, topology builds, hardware) is read from what the development process produced."""
    if args.skip_codeql or args.sarif is not None or args.analyzer != "clang-tidy" or args.no_prepare_build:
        return None
    if shutil.which("fprime-util") is None:
        log.write("prepare-build: fprime-util not on PATH; using the existing build cache if any\n")
        return None
    jobs = ["-j", str(args.jobs)] if args.jobs else []
    try:
        ct.find_compile_commands(project, args.compile_commands)
    except ct.ClangTidyError:
        log.write("prepare-build: no compile_commands.json; running fprime-util generate\n")
        gen = subprocess.run(
            ["fprime-util", "generate", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"],
            cwd=project,
            capture_output=True,
            text=True,
        )
        (work_dir / "prepare-generate.log").write_text((gen.stdout or "") + (gen.stderr or ""), encoding="utf-8")
        if gen.returncode != 0:
            log.write(f"prepare-build: fprime-util generate failed (rc {gen.returncode}); see prepare-generate.log\n")
            return None
    build = subprocess.run(["fprime-util", "build", *jobs], cwd=module, capture_output=True, text=True)
    build_log = work_dir / "prepare-build.log"
    build_log.write_text((build.stdout or "") + (build.stderr or ""), encoding="utf-8")
    log.write(f"prepare-build: fprime-util build in {module} rc {build.returncode}; output in {build_log.name}\n")
    return build_log


def _tool_copy(verdicts: List[Verdict]) -> List[Verdict]:
    return [Verdict(v.item, v.status, list(v.notes)) for v in verdicts]


def cmd_run(args: argparse.Namespace) -> int:
    """Run CodeQL and the checks for one module and write the pre-filled checklist.

    Exit codes: 0 no item failed; 1 some item is NO; 2 usage error;
    3 CodeQL was requested but could not run (its items are UNCHECKED).
    """
    project = args.project.resolve()
    module = (args.module if args.module.is_absolute() else project / args.module).resolve()
    if not module.is_dir():
        print(f"error: module directory not found: {module}", file=sys.stderr)
        return EXIT_USAGE
    try:
        module_rel = _relative(module, project)
    except ModuleOutsideProject as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_USAGE
    output: Path = args.output
    work_dir = args.work_dir or output.with_name(output.name + ".work")
    work_dir.mkdir(parents=True, exist_ok=True)
    log = io.StringIO()

    build_log = _prepare_build(args, project, module, work_dir, log)
    codeql_evidence, sarif = _codeql_phase(args, project, module, work_dir, log)
    opts = RunOptions(
        project=project,
        module=module,
        build=args.build,
        build_log=build_log,
        deployments=args.deployment,
        target_platforms=args.target_platform,
        artifacts_dir=args.artifacts_dir,
        coverage_threshold=args.coverage_threshold,
        sarif=sarif,
        repo=args.repo,
        sha=args.sha,
        jobs=args.jobs,
    )
    records = run_checks(opts, work_dir, log)
    verdicts = judge_all(records, codeql_evidence)
    tool_verdicts = _tool_copy(verdicts)

    commit = args.sha or _git_head(project)
    fingerprints = section_fingerprints(module)
    decisions: Dict[str, Decision] = {}
    outcome: Dict[str, str] = {}
    if args.previous is not None:
        previous = parse_previous(args.previous, args.previous_json)
        changed = sources_changed(project, module_rel, previous.commit, commit)
        decisions = dict(previous.decisions)
        outcome = apply_decisions(verdicts, decisions, fingerprints=fingerprints, source=args.previous.name)
        lapsed = [i for i, state in outcome.items() if state == "lapsed"]
        for item_id in lapsed:
            decisions.pop(item_id, None)
        if decisions:
            log.write(
                f"previous checklist {args.previous}: {len(decisions)} recorded decision(s) applied "
                f"({sum(1 for v in outcome.values() if v == 'stale')} stale), {len(lapsed)} audit(s) lapsed {lapsed}\n"
            )
        skipped: list = []
        carried = apply_previous(verdicts, previous, changed=changed, skipped=skipped, fingerprints=fingerprints)
        log.write(f"previous checklist {args.previous}: carried {len(carried)} Markdown review decision(s) {carried}\n")
        for item_id in carried:
            row = next(v for v in verdicts if v.item.id == item_id)
            stale = any(n.startswith("STALE") for n in row.notes)
            outcome[item_id] = "stale" if stale else "carried"
        if skipped:
            log.write(
                f"previous checklist: {skipped} are decided but were not review rows of the previous run "
                "(keep the CL-n rule text in Notes, or pass --previous-json); not carried\n"
            )
    deferred = mark_deferred(verdicts, args.stage)
    audited = {i: d for i, d in decisions.items() if d.kind == KIND_AUDIT}
    queue = build_queue(verdicts, args.stage, outcome, audited) if args.stage else None
    if queue is not None:
        log.write(f"stage {args.stage}: {len(queue)} row(s) queued for review, {len(deferred)} deferred\n")
    markdown = render_markdown(
        verdicts,
        component=module.name,
        module_rel=module_rel,
        commit=commit,
        codeql=codeql_evidence,
        owner=args.owner,
        contributors=args.contributors,
        strip_prefix=str(project),
        fingerprints=fingerprints,
        stage=args.stage,
        queue=queue,
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(markdown, encoding="utf-8")
    json_output = args.json_output or output.with_suffix(".json")
    json_output.parent.mkdir(parents=True, exist_ok=True)
    json_output.write_text(
        render_json(
            verdicts,
            records,
            codeql_evidence,
            tool_verdicts=tool_verdicts,
            decisions=decisions,
            queue=queue,
            module=module_rel,
            component=module.name,
            owner=args.owner,
            contributors=args.contributors,
            project=str(project),
            commit=commit,
            fingerprints=fingerprints,
            stage=args.stage,
        ),
        encoding="utf-8",
    )
    (work_dir / "checks.log").write_text(log.getvalue(), encoding="utf-8")
    if args.verbose:
        sys.stdout.write(log.getvalue())
    counts: Dict[str, int] = {}
    for v in verdicts:
        counts[v.status] = counts.get(v.status, 0) + 1
    print(f"wrote {output}: " + ", ".join(f"{k} {v}" for k, v in counts.items()))
    if queue is not None:
        print(f"stage {args.stage}: {len(queue)} row(s) to decide -> " + ", ".join(e.id for e in queue))
    if not args.skip_codeql and not codeql_evidence.ran:
        print(f"{codeql_evidence.tool} did not run: {codeql_evidence.reason}", file=sys.stderr)
        return EXIT_CODEQL_UNAVAILABLE
    return EXIT_ITEMS_FAILED if counts.get(NO) else EXIT_OK


def _load_sidecar(path: Path) -> dict:
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise SystemExit(f"error: cannot read {path}: {exc}") from exc
    if not isinstance(doc, dict) or "items" not in doc:
        raise SystemExit(f"error: {path} is not a checklist JSON written by 'run'")
    return doc


def _verdicts_from_sidecar(doc: dict) -> List[Verdict]:
    """The tool's own verdicts as recorded (before reviewer decisions)."""
    by_id = {str(i["id"]): i for i in doc["items"] if isinstance(i, dict) and "id" in i}
    out = []
    for item in ITEMS:
        raw = by_id.get(item.id)
        if raw is None:
            out.append(Verdict(item, REVIEW, []))
            continue
        out.append(
            Verdict(
                item, str(raw.get("tool_status", raw.get("status"))), list(raw.get("tool_notes", raw.get("notes", [])))
            )
        )
    return out


def _codeql_from_sidecar(doc: dict) -> CodeqlEvidence:
    raw = doc.get("codeql") or {}
    return CodeqlEvidence(
        ran=bool(raw.get("ran")),
        reason=str(raw.get("reason", "")),
        tool=str(raw.get("tool", "CodeQL")),
        rules_run=list(raw.get("rules_run", [])),
        autocode_findings=int(raw.get("autocode_findings", 0) or 0),
    )


def cmd_decide(args: argparse.Namespace) -> int:
    """Record reviewer decisions (or audits of tool rows) and re-render the checklist.

    Decisions are stored in the JSON sidecar with the commit and section fingerprint of the
    run they were made against; the Markdown is re-rendered from the sidecar so hand-editing
    is never needed.  Exit codes: 0 ok; 2 usage error.
    """
    checklist: Path = args.checklist
    sidecar: Path = args.json or checklist.with_suffix(".json")
    if not sidecar.is_file():
        print(f"error: no JSON sidecar {sidecar}; run the checklist first", file=sys.stderr)
        return EXIT_USAGE
    doc = _load_sidecar(sidecar)
    requested: List[dict] = []
    if args.from_file is not None:
        try:
            batch = json.loads(args.from_file.read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            print(f"error: cannot read {args.from_file}: {exc}", file=sys.stderr)
            return EXIT_USAGE
        if isinstance(batch, dict):
            batch = batch.get("decisions", [])
        requested.extend(d for d in batch if isinstance(d, dict))
    if args.item:
        if not args.status:
            print("error: --status is required with --item", file=sys.stderr)
            return EXIT_USAGE
        requested.append({"id": args.item, "status": args.status, "notes": list(args.note or []), "audit": args.audit})
    if not requested:
        print("error: nothing to decide (give --item/--status or --from)", file=sys.stderr)
        return EXIT_USAGE

    tool_verdicts = _verdicts_from_sidecar(doc)
    tool_by_id = {v.item.id: v for v in tool_verdicts}
    fingerprints = {str(k): str(v) for k, v in (doc.get("fingerprints") or {}).items()}
    commit = doc.get("commit")
    stage = args.stage or doc.get("stage")
    decisions = decisions_from_json(doc)
    for req in requested:
        item_id = str(req.get("id", ""))
        tool = tool_by_id.get(item_id)
        if tool is None:
            print(f"error: unknown checklist item {item_id!r}", file=sys.stderr)
            return EXIT_USAGE
        status = str(req.get("status", "")).strip().upper()
        status = "N/A" if status in ("NA", "N-A") else status
        audit = tool.status != REVIEW  # a decision on a tool-decided row is always an audit of it
        allowed = ("YES", "NO", "N/A", "UNCHECKED") if audit and tool.status == UNCHECKED else ("YES", "NO", "N/A")
        if status not in allowed:
            print(
                f"error: {item_id}: status must be one of {', '.join(allowed)} (got {req.get('status')!r})",
                file=sys.stderr,
            )
            return EXIT_USAGE
        notes = [str(n) for n in (req.get("notes") or []) if str(n).strip()]
        if not notes and (status != "YES" or (audit and status != tool.status)):
            print(
                f"error: {item_id}: a note is required for {status}" + (" (override)" if audit else ""), file=sys.stderr
            )
            return EXIT_USAGE
        decisions[item_id] = new_decision(
            status,
            notes,
            kind=KIND_AUDIT if audit else KIND_REVIEW,
            commit=commit,
            fingerprint=fingerprints.get(tool.item.section),
            stage=stage,
            tool_status=tool.status if audit else None,
        )
    verdicts = _tool_copy(tool_verdicts)
    outcome = apply_decisions(verdicts, decisions, fingerprints=fingerprints, source=sidecar.name)
    mark_deferred(verdicts, stage)
    audited = {i: d for i, d in decisions.items() if d.kind == KIND_AUDIT}
    queue = build_queue(verdicts, stage, outcome, audited) if stage else None
    codeql = _codeql_from_sidecar(doc)
    project = doc.get("project") or ""
    markdown = render_markdown(
        verdicts,
        component=str(doc.get("component") or Path(str(doc.get("module", "component"))).name),
        module_rel=str(doc.get("module", "")),
        commit=commit,
        codeql=codeql,
        owner=str(doc.get("owner", "")),
        contributors=str(doc.get("contributors", "")),
        strip_prefix=str(project),
        fingerprints=fingerprints or None,
        stage=stage,
        queue=queue,
    )
    checklist.write_text(markdown, encoding="utf-8")
    meta = {
        k: v
        for k, v in doc.items()
        if k not in ("summary", "items", "checks", "codeql", "queue", "tool", "schema_version", "checklist_version")
    }
    meta["stage"] = stage
    sidecar.write_text(
        render_json(
            verdicts,
            doc.get("checks") or {},
            codeql,
            tool_verdicts=tool_verdicts,
            decisions=decisions,
            queue=queue,
            **meta,
        ),
        encoding="utf-8",
    )
    ids = ", ".join(str(r.get("id")) for r in requested)
    remaining = f"; {len(queue)} row(s) still queued" if queue is not None else ""
    print(f"recorded {len(requested)} decision(s) ({ids}) in {sidecar}; re-rendered {checklist}{remaining}")
    return EXIT_OK


def cmd_items(_args: argparse.Namespace) -> int:
    """Print every checklist item with the evidence that decides it."""
    print("| # | Item | Evidence |")
    print("|---|------|----------|")
    for item in ITEMS:
        evidence = ", ".join(f"{e.kind}:{e.ref}" for e in item.evidence)
        print(f"| {item.id} | {item.text} | {evidence} |")
    return 0


def cmd_license_check(args: argparse.Namespace) -> int:
    """Report whether the CodeQL CLI terms permit analysing ``--project``."""
    decision = cq.license_gate(args.project.resolve(), args.codeql_license_ack)
    print(("ALLOWED: " if decision.allowed else "REFUSED: ") + decision.basis)
    return 0 if decision.allowed else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="fprime-auto-checklist", description=__doc__)
    parser.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    sub = parser.add_subparsers(dest="command", required=True)
    run = sub.add_parser("run", help="Run CodeQL + checks for a module and write the pre-filled checklist")
    _add_run_arguments(run)
    run.set_defaults(func=cmd_run)
    decide = sub.add_parser(
        "decide",
        help="Record a reviewer decision (REVIEW row) or an audit of a tool-decided row and re-render the checklist",
    )
    decide.add_argument("--checklist", type=Path, required=True, help="Markdown checklist written by 'run'")
    decide.add_argument("--json", type=Path, default=None, help="Its JSON sidecar (default: <checklist>.json)")
    decide.add_argument("--item", default=None, help="Checklist item id, e.g. 1.4")
    decide.add_argument("--status", default=None, help="YES, NO or N/A")
    decide.add_argument("--note", action="append", default=None, help="Justification (repeatable; required unless YES)")
    decide.add_argument(
        "--audit",
        action="store_true",
        help="Treat as an audit of the tool's verdict (implied when the row is not a REVIEW row)",
    )
    decide.add_argument("--stage", choices=STAGE_ORDER, default=None, help="Stage to record (default: the run's)")
    decide.add_argument(
        "--from",
        dest="from_file",
        type=Path,
        default=None,
        help='JSON list of {"id","status","notes",["audit"]} to record in one go',
    )
    decide.set_defaults(func=cmd_decide)
    items = sub.add_parser("items", help="Print the checklist items and the evidence that decides each")
    items.set_defaults(func=cmd_items)
    lic = sub.add_parser("license-check", help="Report whether the CodeQL CLI terms permit analysing --project")
    lic.add_argument("--project", type=Path, default=Path("."))
    lic.add_argument("--codeql-license-ack", choices=("open-source", "ghas"), default=None)
    lic.set_defaults(func=cmd_license_check)
    return parser


def main(argv: Optional[List[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)
