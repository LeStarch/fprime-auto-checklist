"""clang-tidy backend: the ``fprime-*`` plugin checks as a swap-in for the CodeQL queries.

The native checks live in ``native/`` and are loaded into clang-tidy with
``--load``.  This module locates the tool and the plugin, runs them over the
module's translation units from a ``compile_commands.json``, and converts the
diagnostics into a SARIF document whose ``ruleId`` values are the CodeQL ids
(``cpp/fprime/<rule>``), so the rest of the checklist (scoping, judging, GitHub
code-scanning upload and per-alert dismissal) is unchanged.

Nothing here talks to the network: no downloads, no telemetry, no registry.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import subprocess
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence

from .codeql import QUERIES_DIR

TOOL_NAME = "fprime-auto-checklist/clang-tidy"
PLUGIN_BASENAME = "libFprimeTidy.so"
NATIVE_DIR = Path(__file__).resolve().parents[2] / "native"
SOURCE_SUFFIXES = (".cpp", ".cc", ".cxx", ".c")

# Stock CodeQL queries the checklist relies on, mapped to clang-tidy checks.
STOCK_CHECKS: Dict[str, str] = {
    "clang-analyzer-core.uninitialized.Assign": "cpp/uninitialized-local",
    "clang-analyzer-core.uninitialized.Branch": "cpp/uninitialized-local",
    "clang-analyzer-core.uninitialized.CallArgs": "cpp/uninitialized-local",
    "clang-analyzer-core.uninitialized.UndefReturn": "cpp/uninitialized-local",
    "clang-analyzer-core.UndefinedBinaryOperatorResult": "cpp/uninitialized-local",
}
STOCK_RULE_META = {
    "cpp/uninitialized-local": {
        "name": "Potentially uninitialized local variable",
        "severity": "warning",
        "description": "Reading from a local variable that has not been assigned to will typically yield garbage.",
    }
}

_DIAG = re.compile(
    r"^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): (?P<level>warning|error): (?P<msg>.*?) \[(?P<checks>[\w.,-]+)\]$"
)
_TAG = re.compile(r"^\s*\*\s*@(?P<tag>[\w.-]+)\s+(?P<value>.*)$", re.M)


class ClangTidyError(RuntimeError):
    """The clang-tidy backend could not run."""


@dataclass(frozen=True)
class Rule:
    id: str
    check: str
    name: str
    severity: str  # CodeQL @problem.severity: error | warning | recommendation
    description: str


@dataclass(frozen=True)
class Diagnostic:
    check: str
    file: str
    line: int
    column: int
    level: str
    message: str


def rules(queries_dir: Path = QUERIES_DIR) -> List[Rule]:
    """One Rule per vendored query (from its ``@id``/``@name``/``@problem.severity`` tags) plus the stock ones."""
    out: List[Rule] = []
    for ql in sorted(queries_dir.glob("*.ql")):
        tags = {m.group("tag"): m.group("value").strip() for m in _TAG.finditer(ql.read_text(encoding="utf-8"))}
        rule_id = tags.get("id")
        if not rule_id:
            raise ClangTidyError(f"{ql.name} has no @id tag")
        out.append(
            Rule(
                id=rule_id,
                check="fprime-" + rule_id.rsplit("/", 1)[-1],
                name=tags.get("name", ql.stem),
                severity=tags.get("problem.severity", "warning"),
                description=tags.get("description", ""),
            )
        )
    for rule_id, meta in STOCK_RULE_META.items():
        out.append(Rule(rule_id, "", meta["name"], meta["severity"], meta["description"]))
    return out


def check_to_rule(rule_list: Sequence[Rule]) -> Dict[str, str]:
    mapping = {r.check: r.id for r in rule_list if r.check}
    mapping.update(STOCK_CHECKS)
    return mapping


def checks_argument(rule_list: Sequence[Rule]) -> str:
    """The ``--checks`` value enabling exactly the ported checks."""
    names = sorted({r.check for r in rule_list if r.check} | set(STOCK_CHECKS))
    return "-*," + ",".join(names)


def find_plugin(explicit: Optional[str]) -> Path:
    candidates = [explicit, os.environ.get("FPRIME_TIDY_PLUGIN"), NATIVE_DIR / "build" / PLUGIN_BASENAME]
    for cand in candidates:
        if cand and Path(cand).is_file():
            return Path(cand).resolve()
    raise ClangTidyError(
        "fprime clang-tidy plugin not found; build native/ (see native/CMakeLists.txt) or pass --tidy-plugin"
    )


def find_clang_tidy(explicit: Optional[str], plugin: Optional[Path] = None) -> str:
    """Prefer the clang-tidy the plugin was built against (recorded by the native build)."""
    if explicit:
        if Path(explicit).is_file() or shutil.which(explicit):
            return explicit
        raise ClangTidyError(f"clang-tidy not found at {explicit}")
    env = os.environ.get("CLANG_TIDY")
    if env and (Path(env).is_file() or shutil.which(env)):
        return env
    if plugin is not None:
        recorded = plugin.parent / "clang-tidy-path.txt"
        if recorded.is_file():
            path = recorded.read_text(encoding="utf-8").strip()
            if Path(path).is_file():
                return path
    for name in ("clang-tidy", "clang-tidy-15", "clang-tidy-16", "clang-tidy-17", "clang-tidy-18"):
        found = shutil.which(name)
        if found:
            return found
    raise ClangTidyError("clang-tidy not found; pass --clang-tidy or set $CLANG_TIDY")


def find_compile_commands(project: Path, explicit: Optional[Path]) -> Path:
    """``compile_commands.json`` given explicitly (file or build dir) or from the F Prime native build cache."""
    candidates: List[Path] = []
    if explicit is not None:
        candidates.append(explicit if explicit.name == "compile_commands.json" else explicit / "compile_commands.json")
    else:
        candidates.extend(sorted(project.glob("build-fprime-automatic-native*/compile_commands.json")))
        candidates.append(project / "build" / "compile_commands.json")
    for cand in candidates:
        if cand.is_file():
            return cand.resolve()
    raise ClangTidyError(
        "compile_commands.json not found; generate the build with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "
        "(fprime-util generate -DCMAKE_EXPORT_COMPILE_COMMANDS=ON) or pass --compile-commands"
    )


def module_sources(compile_commands: Path, module: Path) -> List[Path]:
    """Translation units of the module (its own files only; autocode in build dirs is excluded)."""
    entries = json.loads(compile_commands.read_text(encoding="utf-8"))
    module = module.resolve()
    sources = set()
    for entry in entries:
        file = Path(entry["file"])
        if not file.is_absolute():
            file = Path(entry.get("directory", ".")) / file
        file = file.resolve()
        if file.suffix not in SOURCE_SUFFIXES:
            continue
        try:
            file.relative_to(module)
        except ValueError:
            continue
        sources.add(file)
    return sorted(sources)


def parse_diagnostics(output: str) -> List[Diagnostic]:
    diags: List[Diagnostic] = []
    for line in output.splitlines():
        m = _DIAG.match(line.rstrip())
        if not m:
            continue
        for check in m.group("checks").split(","):
            diags.append(
                Diagnostic(
                    check=check,
                    file=m.group("file"),
                    line=int(m.group("line")),
                    column=int(m.group("col")),
                    level=m.group("level"),
                    message=m.group("msg"),
                )
            )
    return diags


def run(
    clang_tidy: str,
    plugin: Path,
    compile_commands: Path,
    sources: Sequence[Path],
    log: Path,
    header_filter: str,
    checks: str,
    jobs: Optional[int] = None,
) -> List[Diagnostic]:
    """Run clang-tidy once per translation unit (in parallel) and collect the diagnostics."""
    if not sources:
        return []
    build_dir = compile_commands.parent

    def one(source: Path):
        cmd = [
            clang_tidy,
            f"--load={plugin}",
            "--config={}",  # ignore the project's .clang-tidy (e.g. WarningsAsErrors, other checks)
            f"--checks={checks}",
            f"--header-filter={header_filter}",
            "-p",
            str(build_dir),
            "--quiet",
            str(source),
        ]
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
        return source, cmd, proc

    with ThreadPoolExecutor(max_workers=jobs or os.cpu_count() or 2) as pool:
        results = list(pool.map(one, sources))
    diags: List[Diagnostic] = []
    with log.open("w", encoding="utf-8") as fh:
        for source, cmd, proc in results:
            fh.write("$ " + " ".join(cmd) + f"\n[exit {proc.returncode}]\n{proc.stdout}\n{proc.stderr}\n")
            if proc.returncode not in (0, 1) and "error:" in proc.stderr and not proc.stdout.strip():
                raise ClangTidyError(f"clang-tidy failed on {source}: see {log}")
            diags.extend(parse_diagnostics(proc.stdout))
    return diags


def _line_hash(path: Path, line: int, rule_id: str) -> str:
    """Content-based fingerprint: stable when unrelated lines are added or removed."""
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        lines = []
    lo, hi = max(0, line - 3), min(len(lines), line + 2)
    context = "\n".join(re.sub(r"\s+", "", s) for s in lines[lo:hi])
    return hashlib.sha256(f"{rule_id}\n{context}".encode("utf-8")).hexdigest()[:32] + ":1"


_LEVELS = {"error": "error", "warning": "warning", "recommendation": "note"}


def to_sarif(
    diags: Iterable[Diagnostic],
    project: Path,
    rule_list: Sequence[Rule],
    tool_version: str = "",
    tidy_version: str = "",
) -> dict:
    """SARIF 2.1.0 with one result per unique diagnostic, CodeQL rule ids and partial fingerprints.

    Repeated header diagnostics (one per translation unit) collapse to a single result.
    """
    mapping = check_to_rule(rule_list)
    by_id = {r.id: r for r in rule_list}
    project = project.resolve()
    results: List[dict] = []
    seen = set()
    used_rules = set()
    for d in diags:
        rule_id = mapping.get(d.check)
        if rule_id is None:
            continue
        path = Path(d.file)
        try:
            uri = path.resolve().relative_to(project).as_posix()
        except ValueError:
            uri = path.as_posix()
        key = (rule_id, uri, d.line, d.column, d.message)
        if key in seen:
            continue
        seen.add(key)
        used_rules.add(rule_id)
        results.append(
            {
                "ruleId": rule_id,
                "level": _LEVELS.get(by_id[rule_id].severity, "warning") if rule_id in by_id else "warning",
                "message": {"text": d.message},
                "locations": [
                    {
                        "physicalLocation": {
                            "artifactLocation": {"uri": uri, "uriBaseId": "%SRCROOT%"},
                            "region": {"startLine": d.line, "startColumn": d.column},
                        }
                    }
                ],
                "partialFingerprints": {"primaryLocationLineHash": _line_hash(path, d.line, rule_id)},
                "properties": {"clangTidyCheck": d.check},
            }
        )
    results.sort(
        key=lambda r: (
            r["ruleId"],
            r["locations"][0]["physicalLocation"]["artifactLocation"]["uri"],
            r["locations"][0]["physicalLocation"]["region"]["startLine"],
            r["locations"][0]["physicalLocation"]["region"]["startColumn"],
        )
    )
    rules_json = [
        {
            "id": r.id,
            "name": r.name,
            "shortDescription": {"text": r.name},
            "fullDescription": {"text": r.description or r.name},
            "defaultConfiguration": {"level": _LEVELS.get(r.severity, "warning")},
            "properties": {"clangTidyCheck": r.check or ", ".join(k for k, v in STOCK_CHECKS.items() if v == r.id)},
        }
        for r in rule_list
    ]
    return {
        "$schema": "https://json.schemastore.org/sarif-2.1.0.json",
        "version": "2.1.0",
        "runs": [
            {
                "tool": {
                    "driver": {
                        "name": TOOL_NAME,
                        "version": tool_version or "0",
                        "informationUri": "https://github.com/LeStarch/fprime-auto-checklist",
                        "properties": {"clangTidyVersion": tidy_version},
                        "rules": rules_json,
                    }
                },
                "automationDetails": {"id": "fprime-auto-checklist/clang-tidy/"},
                "originalUriBaseIds": {"%SRCROOT%": {"uri": project.as_uri() + "/"}},
                "results": results,
            }
        ],
    }


def tidy_version(clang_tidy: str) -> str:
    try:
        out = subprocess.run([clang_tidy, "--version"], capture_output=True, text=True, check=False).stdout
    except OSError:
        return ""
    m = re.search(r"LLVM version (\S+)", out)
    return m.group(1) if m else out.strip().splitlines()[0] if out.strip() else ""


def analyze(
    project: Path,
    module: Path,
    work_dir: Path,
    *,
    clang_tidy: Optional[str] = None,
    plugin: Optional[str] = None,
    compile_commands: Optional[Path] = None,
    jobs: Optional[int] = None,
    tool_version: str = "",
) -> Path:
    """Run the plugin over ``module`` and write ``work_dir/checklist.sarif``; returns its path."""
    plugin_path = find_plugin(plugin)
    tidy = find_clang_tidy(clang_tidy, plugin_path)
    compdb = find_compile_commands(project, compile_commands)
    sources = module_sources(compdb, module)
    if not sources:
        raise ClangTidyError(f"no translation units of {module} in {compdb}; build the module first")
    rule_list = rules()
    module_abs = module.resolve()
    header_filter = "^" + re.escape(str(module_abs)) + "/"
    diags = run(
        tidy,
        plugin_path,
        compdb,
        sources,
        work_dir / "clang-tidy.log",
        header_filter=header_filter,
        checks=checks_argument(rule_list),
        jobs=jobs,
    )
    sarif = to_sarif(diags, project, rule_list, tool_version=tool_version, tidy_version=tidy_version(tidy))
    out = work_dir / "checklist.sarif"
    out.write_text(json.dumps(sarif, indent=2), encoding="utf-8")
    return out
