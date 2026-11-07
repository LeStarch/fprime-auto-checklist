#!/usr/bin/env python3
"""Adversarial convergence tier: CodeQL is the oracle, clang-tidy must agree.

``codeql/fprime-queries-tests-adversarial/<Query>/test.cpp`` holds hand-written
evasion attempts for one query (macros, templates, lambdas, aliasing, ...).

``learn``   runs ``codeql test run --learn`` so every ``<Query>.expected`` is
            exactly what CodeQL reports (requires the CodeQL CLI, offline).
``check``   runs the ported ``fprime-*`` check over the same files via
            ``run_fixtures.py --fixtures`` and diffs the flagged lines.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
FIXTURES = ROOT / "codeql" / "fprime-queries-tests-adversarial"
QUERY_PACKS = ROOT / "src" / "fprime_auto_checklist" / "queries"


def learn(codeql: str, only: list[str]) -> int:
    dirs = [FIXTURES / o for o in only] if only else [FIXTURES]
    cmd = [codeql, "test", "run", "--learn", f"--additional-packs={QUERY_PACKS}", *map(str, dirs)]
    return subprocess.run(cmd, check=False).returncode


def check(clang_tidy: str, plugin: Path, only: list[str], show: bool) -> int:
    cmd = [sys.executable, str(HERE / "run_fixtures.py"), "--fixtures", str(FIXTURES), "--clang-tidy", clang_tidy]
    if plugin:
        cmd += ["--plugin", str(plugin)]
    for o in only:
        cmd += ["--only", o]
    if show:
        cmd.append("--show")
    return subprocess.run(cmd, check=False).returncode


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["learn", "check", "both"])
    ap.add_argument("--codeql", default=os.environ.get("CODEQL", "codeql"))
    ap.add_argument("--clang-tidy", default=os.environ.get("CLANG_TIDY", "clang-tidy"))
    ap.add_argument("--plugin", type=Path, default=None)
    ap.add_argument("--only", action="append", default=[])
    ap.add_argument("--show", action="store_true")
    args = ap.parse_args(argv)
    rc = 0
    if args.mode in ("learn", "both"):
        rc = learn(args.codeql, args.only)
    if rc == 0 and args.mode in ("check", "both"):
        rc = check(args.clang_tidy, args.plugin, args.only, args.show)
    return rc


if __name__ == "__main__":
    sys.exit(main())
