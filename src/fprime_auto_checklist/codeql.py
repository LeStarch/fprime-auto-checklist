"""Run the vendored CodeQL query pack over an F Prime build and scope results.

Licensing: the CodeQL CLI is *not* bundled.  The GitHub CodeQL Terms and
Conditions (``LICENSE.md`` in every CLI release) permit using the CLI to
analyze an *Open Source Codebase* -- one released under an OSI-approved
license -- and to generate databases for CI on such codebases hosted on
GitHub.com.  Use against other codebases needs a GitHub Advanced Security
license.  :func:`license_gate` therefore refuses to run unless the source
root carries a recognisable OSI license or the operator attests to one of
the permitted uses with ``--codeql-license-ack``.
"""

from __future__ import annotations

import json
import os
import re
import shlex
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence

from .results import CodeqlEvidence, Finding

PACKAGE_DIR = Path(__file__).resolve().parent
QUERIES_DIR = PACKAGE_DIR / "queries" / "fprime-queries"

#: Stock queries pulled from the public ``codeql/cpp-queries`` pack, pinned to
#: the release matching the ``codeql/cpp-all`` version in the vendored lock file.
STOCK_QUERIES_VERSION = "1.10.0"
STOCK_QUERIES = (f"codeql/cpp-queries@{STOCK_QUERIES_VERSION}:Likely Bugs/Memory Management/UninitializedLocal.ql",)

_OSI_LICENSES = (
    (r"Apache License\s+Version 2\.0", "Apache-2.0"),
    (r"\bMIT License\b|Permission is hereby granted, free of charge", "MIT"),
    (r"Redistribution and use in source and binary forms", "BSD"),
    (r"GNU GENERAL PUBLIC LICENSE", "GPL"),
    (r"GNU LESSER GENERAL PUBLIC LICENSE", "LGPL"),
    (r"GNU AFFERO GENERAL PUBLIC LICENSE", "AGPL"),
    (r"Mozilla Public License,? v(ersion)? 2\.0", "MPL-2.0"),
    (r"Eclipse Public License", "EPL"),
    (r"ISC License", "ISC"),
    (r"Boost Software License", "BSL-1.0"),
    (r"zlib License|This software is provided 'as-is'", "Zlib"),
)
_BUILD_DIR = re.compile(r"(^|/)build-fprime-[^/]*/")


class CodeqlError(RuntimeError):
    """A CodeQL step could not be run; the message is user-facing."""


@dataclass
class LicenseDecision:
    allowed: bool
    basis: str


def detect_osi_license(source_root: Path) -> Optional[str]:
    """Return the SPDX-ish name of an OSI license found at the source root."""
    for candidate in sorted(source_root.glob("LICENSE*")) + sorted(source_root.glob("COPYING*")):
        if not candidate.is_file():
            continue
        try:
            text = candidate.read_text(encoding="utf-8", errors="replace")[:20000]
        except OSError:
            continue
        for pattern, name in _OSI_LICENSES:
            if re.search(pattern, text, re.IGNORECASE):
                return name
    return None


def license_gate(source_root: Path, ack: Optional[str]) -> LicenseDecision:
    if ack == "ghas":
        return LicenseDecision(True, "operator attests a GitHub Advanced Security license covers this codebase")
    found = detect_osi_license(source_root)
    if found:
        return LicenseDecision(True, f"OSI-approved license detected at source root ({found})")
    if ack == "open-source":
        return LicenseDecision(True, "operator attests the codebase is released under an OSI-approved license")
    return LicenseDecision(
        False,
        "no OSI-approved license file found at the source root; the CodeQL CLI terms only permit "
        "analysis of open-source codebases (or GHAS-licensed ones). Re-run with "
        "--codeql-license-ack open-source (or ghas) to attest, or --skip-codeql.",
    )


def find_codeql(explicit: Optional[str]) -> str:
    path = explicit or os.environ.get("CODEQL") or shutil.which("codeql")
    if not path or not Path(path).is_file():
        raise CodeqlError(
            "CodeQL CLI not found: pass --codeql, set CODEQL, or install it on PATH "
            "(https://github.com/github/codeql-cli-binaries/releases). It is not bundled."
        )
    return str(path)


def _run(cmd: Sequence[str], cwd: Optional[Path], log: Path) -> None:
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w", encoding="utf-8") as fh:
        fh.write("$ " + " ".join(shlex.quote(c) for c in cmd) + "\n")
        fh.flush()
        proc = subprocess.run(cmd, cwd=cwd, stdout=fh, stderr=subprocess.STDOUT, text=True, check=False)
    if proc.returncode != 0:
        raise CodeqlError(f"{cmd[1] if len(cmd) > 1 else cmd[0]} failed (exit {proc.returncode}); see {log}")


def create_database(codeql: str, source_root: Path, db_path: Path, build_command: str, log: Path) -> None:
    cmd = [
        codeql,
        "database",
        "create",
        str(db_path),
        "--language=cpp",
        "--overwrite",
        f"--source-root={source_root}",
        f"--command={build_command}",
    ]
    _run(cmd, source_root, log)


def stage_queries(work_dir: Path, source: Path = QUERIES_DIR) -> Path:
    """Copy the vendored pack into the work dir.

    ``codeql pack install`` writes its lock/cache next to the pack, which must
    not happen inside a (possibly read-only) installed package.
    """
    staged = work_dir / "fprime-queries"
    if staged.exists():
        shutil.rmtree(staged)
    shutil.copytree(source, staged, ignore=shutil.ignore_patterns(".codeql", "__pycache__"))
    return staged


def install_pack(codeql: str, queries_dir: Path, log: Path) -> None:
    """Resolve the pack's dependencies (pinned by its codeql-pack.lock.yml)."""
    _run([codeql, "pack", "install", str(queries_dir)], None, log)


def analyze(
    codeql: str,
    db_path: Path,
    sarif_out: Path,
    log: Path,
    queries_dir: Path = QUERIES_DIR,
    stock: Sequence[str] = STOCK_QUERIES,
    threads: int = 0,
) -> None:
    cmd = [
        codeql,
        "database",
        "analyze",
        str(db_path),
        str(queries_dir),
        *stock,
        "--format=sarif-latest",
        f"--output={sarif_out}",
        f"--threads={threads}",
        "--download",
        "--sarif-add-baseline-file-info",
    ]
    _run(cmd, None, log)


def load_sarif(path: Path) -> dict:
    with path.open(encoding="utf-8") as fh:
        return json.load(fh)


def scope_findings(sarif: dict, module_rel: str, rules: Sequence[str]) -> CodeqlEvidence:
    """Keep findings located in the module's own (non-generated) files."""
    module_prefix = module_rel.strip("/").rstrip("/") + "/"
    findings: Dict[str, List[Finding]] = {}
    autocode = 0
    rules_run: List[str] = []
    for run in sarif.get("runs", []):
        driver = (run.get("tool") or {}).get("driver") or {}
        rules_run.extend(r.get("id", "") for r in driver.get("rules", []) or [])
        for ext in (run.get("tool") or {}).get("extensions") or []:
            rules_run.extend(r.get("id", "") for r in ext.get("rules", []) or [])
        for result in run.get("results", []) or []:
            rule = result.get("ruleId", "")
            for location in result.get("locations", []) or []:
                phys = location.get("physicalLocation") or {}
                uri = ((phys.get("artifactLocation") or {}).get("uri")) or ""
                if _BUILD_DIR.search(uri):
                    if module_prefix.rstrip("/") in uri:
                        autocode += 1
                    break
                if uri.startswith(module_prefix) or uri.startswith("/" + module_prefix):
                    line = int(((phys.get("region") or {}).get("startLine")) or 0)
                    message = ((result.get("message") or {}).get("text")) or ""
                    findings.setdefault(rule, []).append(Finding(rule, uri, line, "; ".join(message.split("\n"))))
                break
    for rule in rules:
        findings.setdefault(rule, [])
    return CodeqlEvidence(ran=True, findings=findings, rules_run=sorted(set(rules_run)), autocode_findings=autocode)
