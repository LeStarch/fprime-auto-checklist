"""Command-line entry point: ``fprime-auto-checklist``."""

from __future__ import annotations

import argparse
import io
import os
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from . import __version__
from . import codeql as cq
from .checklist import ITEMS, codeql_rule_ids
from .render import render_json, render_markdown
from .results import NO, CodeqlEvidence, judge_all
from .runner import ModuleOutsideProject, RunOptions, _git_head, _relative, run_checks

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
    build.add_argument("--artifacts-dir", type=Path, default=None, help="Published unit-test artifacts root (U1)")
    ci = parser.add_argument_group("CI status (C3; needs GITHUB_TOKEN)")
    ci.add_argument("--repo", default=None, help="owner/name (default: origin remote)")
    ci.add_argument("--sha", default=None, help="commit to inspect (default: HEAD)")
    q = parser.add_argument_group("CodeQL")
    q.add_argument("--skip-codeql", action="store_true", help="Do not run CodeQL; its items are reported UNCHECKED")
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
    evidence = cq.scope_findings(cq.load_sarif(sarif), module_rel, rules)
    return evidence, sarif


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

    codeql_evidence, sarif = _codeql_phase(args, project, module, work_dir, log)
    opts = RunOptions(
        project=project,
        module=module,
        build=args.build,
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

    commit = args.sha or _git_head(project)
    markdown = render_markdown(
        verdicts,
        component=module.name,
        module_rel=module_rel,
        commit=commit,
        codeql=codeql_evidence,
        owner=args.owner,
        contributors=args.contributors,
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(markdown, encoding="utf-8")
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(
            render_json(verdicts, records, codeql_evidence, module=module_rel, commit=commit), encoding="utf-8"
        )
    (work_dir / "checks.log").write_text(log.getvalue(), encoding="utf-8")
    if args.verbose:
        sys.stdout.write(log.getvalue())
    counts: Dict[str, int] = {}
    for v in verdicts:
        counts[v.status] = counts.get(v.status, 0) + 1
    print(f"wrote {output}: " + ", ".join(f"{k} {v}" for k, v in counts.items()))
    if not args.skip_codeql and not codeql_evidence.ran:
        print(f"codeql did not run: {codeql_evidence.reason}", file=sys.stderr)
        return EXIT_CODEQL_UNAVAILABLE
    return EXIT_ITEMS_FAILED if counts.get(NO) else EXIT_OK


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
