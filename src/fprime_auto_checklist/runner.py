"""Run the component-checks scripts for one module and collect their records."""

from __future__ import annotations

import io
import json
import os
import shutil
import subprocess
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

from .checks import (
    _config,
    check_build_warnings,
    check_ci_status,
    check_command_handlers,
    check_coverage_threshold,
    check_dp_priorities,
    check_dp_priority_configurable,
    check_fpp_generate,
    check_hardware_run,
    check_include_dependencies,
    check_leaks,
    check_port_handlers,
    check_req_commands,
    check_req_data_products,
    check_req_design_mapping,
    check_req_events,
    check_req_parameters,
    check_req_parent_trace,
    check_req_ports,
    check_req_telemetry,
    check_req_verification,
    check_sdd,
    check_sdd_dictionary_tables,
    check_sdd_port_behavior,
    check_sdd_port_table,
    check_sdd_sections,
    check_sm_actions,
    check_static_analysis,
    check_topology_build,
    check_ut_artifacts,
    check_ut_include_dependencies,
)
from .checks._fpp import load_module_model


class ModuleOutsideProject(ValueError):
    """The module directory is not inside the project checkout."""


#: Checks that only parse the model, sources and docs; always run.
STATIC_CHECKS = [
    ("R1", check_req_commands),
    ("R2", check_req_telemetry),
    ("R3", check_req_events),
    ("R4", check_req_parameters),
    ("R5", check_req_data_products),
    ("R6", check_req_ports),
    ("R7", check_req_parent_trace),
    ("R8", check_req_verification),
    ("D1", check_port_handlers),
    ("D2", check_command_handlers),
    ("D3", check_sm_actions),
    ("D4", check_dp_priorities),
    ("D5", check_req_design_mapping),
    ("D7", check_sdd_port_table),
    ("D8", check_sdd_dictionary_tables),
    ("D9", check_sdd_sections),
    ("D10", check_sdd_port_behavior),
    ("I1", check_include_dependencies),
    ("I3", check_dp_priority_configurable),
    ("U3", check_ut_include_dependencies),
    ("C2", check_hardware_run),
]

SDD_MIN_TIER = "silver"
_TIER_ORDER = ("bronze", "silver", "gold", "platinum")


@dataclass
class RunOptions:
    project: Path
    module: Path
    build: bool = False
    deployments: List[Path] = field(default_factory=list)
    target_platforms: List[str] = field(default_factory=list)
    artifacts_dir: Optional[Path] = None
    coverage_threshold: Optional[float] = None
    sarif: Optional[Path] = None
    repo: Optional[str] = None
    sha: Optional[str] = None
    jobs: Optional[int] = None


def _invoke(module, argv: List[str], records_file: Path, log: io.StringIO) -> None:
    """Call a check script's ``main`` in-process; it appends to ``records_file``."""
    try:
        with redirect_stdout(log), redirect_stderr(log):
            module.main(argv + ["--json-output", str(records_file)])
    except SystemExit as exc:
        if exc.code not in (None, 0):
            log.write(f"ERROR: {module.__name__} exited {exc.code} (usage error?)\n")
    except Exception as exc:  # noqa: BLE001 -- one broken check must not kill the run
        log.write(f"ERROR: {module.__name__} crashed: {exc}\n")


def _append_record(records_file: Path, record: dict) -> None:
    with records_file.open("a", encoding="utf-8") as fh:
        fh.write(json.dumps(record) + "\n")


def fpp_dependencies(project: Path, module: Path, log: io.StringIO) -> Optional[List[Path]]:
    """FPP files the module's model depends on, resolved through the build cache's locs.fpp.

    ``fpp-check`` needs every transitive dependency on its command line; the
    F Prime build writes the location map ``locs.fpp`` into each build cache,
    and ``fpp-depend`` turns it into the dependency list.  Returns ``None``
    when no build cache or ``fpp-depend`` is available.
    """
    locs = sorted(project.glob("build-fprime-*/locs.fpp"))
    fpp_depend = shutil.which("fpp-depend")
    top_level = [f for f in load_module_model(module).fpp_files if f.suffix == ".fpp"]
    if not locs or fpp_depend is None or not top_level:
        return None
    if len(locs) > 1:
        log.write(f"fpp-depend: {len(locs)} build caches found, using {locs[0]}\n")
    cmd = [fpp_depend, str(locs[0]), *[str(f) for f in top_level]]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        log.write(f"fpp-depend failed: {proc.stderr.strip()}\n")
        return None
    own = {f.resolve() for f in top_level}
    return [Path(line) for line in proc.stdout.splitlines() if line.strip() and Path(line).resolve() not in own]


def _load_records(records_file: Path) -> Dict[str, dict]:
    records: Dict[str, dict] = {}
    if not records_file.is_file():
        return records
    for line in records_file.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line:
            record = json.loads(line)
            records[record["id"]] = record
    return records


def grade_sdd_record(module: Path, project: Path) -> dict:
    """Grade docs/sdd.md structurally (check_sdd.py) and report it as check ``SDD``."""
    config = _config.load_config(project / _config.DEFAULT_CONFIG_PATH)
    thresholds = check_sdd.sdd_thresholds(config)
    sdd_path = module / "docs" / "sdd.md"
    text = sdd_path.read_text(encoding="utf-8") if sdd_path.is_file() else None
    metrics = check_sdd.grade_sdd(text, thresholds, load_module_model(module))
    tier = metrics.get("tier", "bronze")
    ok = _TIER_ORDER.index(tier) >= _TIER_ORDER.index(SDD_MIN_TIER)
    return {
        "id": "SDD",
        "name": "SDD structural grade",
        "category": "design",
        "module": str(module),
        "status": "pass" if ok else "fail",
        "failures": []
        if ok
        else [f"docs/sdd.md graded {tier}; {SDD_MIN_TIER} or better required (filled intro + sections)"],
        "detail": f"tier {tier}",
    }


def _git_remote_repo(project: Path) -> Optional[str]:
    try:
        url = subprocess.run(
            ["git", "-C", str(project), "remote", "get-url", "origin"], capture_output=True, text=True, check=True
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None
    for prefix in ("https://github.com/", "git@github.com:", "ssh://git@github.com/"):
        if url.startswith(prefix):
            return url[len(prefix) :].removesuffix(".git")
    return None


def _git_head(project: Path) -> Optional[str]:
    try:
        return (
            subprocess.run(
                ["git", "-C", str(project), "rev-parse", "HEAD"], capture_output=True, text=True, check=True
            ).stdout.strip()
            or None
        )
    except (OSError, subprocess.CalledProcessError):
        return None


def run_checks(opts: RunOptions, work_dir: Path, log: io.StringIO) -> Dict[str, dict]:
    """Run every applicable check; return ``{check_id: record}``."""
    work_dir.mkdir(parents=True, exist_ok=True)
    records_file = work_dir / "check-records.jsonl"
    if records_file.exists():
        records_file.unlink()
    module = str(opts.module)
    base = ["--module", module]
    jobs = ["--jobs", str(opts.jobs)] if opts.jobs else []

    cache = _build_cache(opts.project)
    for check_id, mod in STATIC_CHECKS:
        extra = ["--build-cache", str(cache)] if check_id == "I1" and cache is not None else []
        _invoke(mod, list(base) + extra, records_file, log)

    includes = fpp_dependencies(opts.project, opts.module, log)
    if includes is None:
        _append_record(
            records_file,
            {
                "id": "D6",
                "name": "FPP compiles and generates auto-code",
                "category": "design",
                "module": module,
                "status": "skip",
                "failures": [],
                "detail": "no build cache with locs.fpp under the project (run fprime-util generate) "
                "or fpp-depend missing",
            },
        )
    else:
        include_args = [a for inc in includes for a in ("--include", str(inc))]
        _invoke(check_fpp_generate, base + include_args + (["--build"] if opts.build else []), records_file, log)

    if opts.sarif is not None:
        module_rel = _relative(opts.module, opts.project)
        sarif_args = ["--sarif", str(opts.sarif), "--module-path", module_rel]
        _invoke(check_static_analysis, base + sarif_args, records_file, log)

    if opts.artifacts_dir is not None:
        module_rel = _relative(opts.module, opts.project)
        _invoke(
            check_ut_artifacts,
            base + ["--artifacts-dir", str(opts.artifacts_dir), "--module-path", module_rel],
            records_file,
            log,
        )

    if opts.build:
        platforms = [a for p in opts.target_platforms for a in ("--target-platform", p)]
        _invoke(check_build_warnings, base + platforms + jobs, records_file, log)
        _invoke(check_leaks, base + jobs, records_file, log)
        threshold = ["--threshold", str(opts.coverage_threshold)] if opts.coverage_threshold is not None else []
        _invoke(check_coverage_threshold, base + threshold, records_file, log)
        if opts.deployments:
            deployments = [a for d in opts.deployments for a in ("--deployment", str(d))]
            _invoke(check_topology_build, base + deployments + platforms + jobs, records_file, log)

    repo = opts.repo or _git_remote_repo(opts.project)
    sha = opts.sha or _git_head(opts.project)
    if repo and sha and (os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")):
        _invoke(check_ci_status, base + ["--repo", repo, "--sha", sha], records_file, log)

    records = _load_records(records_file)
    try:
        records["SDD"] = grade_sdd_record(opts.module, opts.project)
    except Exception as exc:  # noqa: BLE001
        log.write(f"ERROR: SDD grading crashed: {exc}\n")
    return records


def _build_cache(project: Path) -> Optional[Path]:
    """The project's native (non-UT) build cache, if generated."""
    caches = sorted(p.parent for p in project.glob("build-fprime-*/locs.fpp"))
    non_ut = [c for c in caches if not c.name.endswith("-ut")]
    return (non_ut or caches or [None])[0]


def _relative(module: Path, project: Path) -> str:
    """Project-relative POSIX path of ``module``; raises when it is outside the project."""
    try:
        return module.resolve().relative_to(project.resolve()).as_posix()
    except ValueError as exc:
        raise ModuleOutsideProject(f"module {module} is not inside project {project}") from exc
