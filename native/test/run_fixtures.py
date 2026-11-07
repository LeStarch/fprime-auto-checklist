#!/usr/bin/env python3
"""Run the fprime-* clang-tidy checks over the CodeQL query-test fixtures and diff against .expected.

Each ``codeql/fprime-queries-tests/<Query>/`` directory holds ``test.cpp`` and
``<Query>.expected`` (CodeQL's ``| test.cpp:L:C:L2:C2 | elem | message |``
rows).  The same ``test.cpp`` is analysed with the matching ``fprime-<rule>``
check and the sets of reported *lines* are compared.  This is the convergence
oracle: clang-tidy must flag exactly the lines CodeQL flags.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
FIXTURES = ROOT / "codeql" / "fprime-queries-tests"
QUERIES = ROOT / "src" / "fprime_auto_checklist" / "queries" / "fprime-queries"

_EXPECTED = re.compile(r"^\|\s*test\.cpp:(\d+):(\d+):(\d+):(\d+)\s*\|")
_DIAG = re.compile(
    r"^(?P<file>[^:\n]+):(?P<line>\d+):(?P<col>\d+): (?P<level>warning|error): (?P<msg>.*) \[(?P<check>[\w.-]+)\]$"
)


def rule_id(query_dir: Path) -> str:
    """``cpp/fprime/[jpl-c/]<rule>`` from the query's ``@id`` tag."""
    text = (QUERIES / f"{query_dir.name}.ql").read_text(encoding="utf-8")
    m = re.search(r"@id\s+(\S+)", text)
    if not m:
        raise SystemExit(f"no @id in {query_dir.name}.ql")
    return m.group(1)


def check_name(rule: str) -> str:
    return "fprime-" + rule.split("/")[-1]


_DIVERGENCE = re.compile(r"^([-+])(\d+)\s+\S")


def expected_lines(query_dir: Path) -> set[int]:
    """Lines the CodeQL oracle flags, adjusted by the fixture's documented divergences.

    ``<query_dir>/divergence.txt`` records where the port deliberately differs from CodeQL
    because CodeQL is wrong: ``-N reason`` drops a line CodeQL reports (CodeQL false positive),
    ``+N reason`` adds one it misses (CodeQL false negative).  A stale entry (dropping a line
    CodeQL no longer reports, or adding one it now reports) is an error so the file cannot
    silently mask a real divergence.
    """
    lines = set()
    for row in (query_dir / f"{query_dir.name}.expected").read_text(encoding="utf-8").splitlines():
        m = _EXPECTED.match(row)
        if m:
            lines.add(int(m.group(1)))
    divergence = query_dir / "divergence.txt"
    if divergence.exists():
        for row in divergence.read_text(encoding="utf-8").splitlines():
            m = _DIVERGENCE.match(row)
            if not m:
                continue
            sign, line = m.group(1), int(m.group(2))
            if (line in lines) != (sign == "-"):
                raise ValueError(f"{divergence}: stale entry {row!r} (CodeQL expected {sorted(lines)})")
            (lines.discard if sign == "-" else lines.add)(line)
    return lines


def run_clang_tidy(clang_tidy: str, plugin: Path, check: str, source: Path, extra: list[str]) -> list[dict]:
    cmd = [
        clang_tidy,
        f"--load={plugin}",
        f"--checks=-*,{check}",
        "--header-filter=.*",
        str(source),
        "--",
        "-std=c++14",
        "-fno-exceptions",
        "-fno-rtti",
        *extra,
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    diags = []
    for line in proc.stdout.splitlines():
        m = _DIAG.match(line)
        if m and m.group("check") == check:
            diags.append(m.groupdict())
    if proc.returncode not in (0, 1) and not diags:
        sys.stderr.write(proc.stdout + proc.stderr)
    return diags


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--clang-tidy", default="clang-tidy")
    ap.add_argument("--plugin", type=Path, default=HERE.parent / "build" / "libFprimeTidy.so")
    ap.add_argument("--only", action="append", default=[], help="Query directory name(s) to run")
    ap.add_argument("--show", action="store_true", help="Print every diagnostic")
    ap.add_argument("--skip-missing", action="store_true", help="Ignore queries with no ported check")
    ap.add_argument(
        "--fixtures",
        type=Path,
        default=FIXTURES,
        help="Fixture tree to run (default: the vendored CodeQL query tests)",
    )
    args = ap.parse_args(argv)

    listed = subprocess.run(
        [args.clang_tidy, f"--load={args.plugin}", "--checks=-*,fprime-*", "--list-checks"],
        capture_output=True,
        text=True,
        check=False,
    ).stdout
    available = set(re.findall(r"^\s+(fprime-[\w-]+)$", listed, re.M))

    failures = 0
    total = 0
    for query_dir in sorted(p for p in args.fixtures.iterdir() if p.is_dir()):
        if args.only and query_dir.name not in args.only:
            continue
        rule = rule_id(query_dir)
        check = check_name(rule)
        if check not in available:
            if not args.skip_missing:
                print(f"MISSING {query_dir.name}: {check} not provided by plugin")
                failures += 1
            continue
        total += 1
        try:
            want = expected_lines(query_dir)
        except ValueError as exc:
            print(f"FAIL {query_dir.name:40s} {exc}")
            failures += 1
            continue
        diags = run_clang_tidy(args.clang_tidy, args.plugin, check, query_dir / "test.cpp", [])
        got = {int(d["line"]) for d in diags if Path(d["file"]).name == "test.cpp"}
        status = "ok  " if got == want else "FAIL"
        if got != want:
            failures += 1
        print(f"{status} {query_dir.name:40s} expected {sorted(want)} got {sorted(got)}")
        if args.show or got != want:
            for d in diags:
                print(f"       {Path(d['file']).name}:{d['line']}:{d['col']}: {d['msg']}")
    print(f"{total - failures}/{total} fixtures match; {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
