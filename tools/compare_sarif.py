#!/usr/bin/env python3
"""Compare two SARIF files (e.g. CodeQL vs clang-tidy) finding by finding.

Findings are matched on (ruleId, file, line); message text differs between
engines (CodeQL embeds ``[name](n)`` link markup) and is not compared.
Generated code under ``build-fprime-*`` is ignored, as the checklist does.

    tools/compare_sarif.py codeql.sarif clang-tidy.sarif [--prefix Svc/CmdDispatcher] [--rules-only]
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

_BUILD_DIR = re.compile(r"(^|/)build-fprime-[^/]*/")


def findings(path: Path, prefix: str, rules: set[str] | None) -> set[tuple[str, str, int]]:
    doc = json.loads(path.read_text(encoding="utf-8"))
    out = set()
    for run in doc.get("runs", []):
        for result in run.get("results", []):
            rule = result.get("ruleId", "")
            if rules is not None and rule not in rules:
                continue
            for loc in result.get("locations", []):
                phys = loc.get("physicalLocation", {})
                uri = phys.get("artifactLocation", {}).get("uri", "").lstrip("/")
                if _BUILD_DIR.search(uri) or not uri.startswith(prefix):
                    break
                out.add((rule, uri, int(phys.get("region", {}).get("startLine", 0))))
                break
    return out


def rule_ids(path: Path) -> set[str]:
    doc = json.loads(path.read_text(encoding="utf-8"))
    ids = set()
    for run in doc.get("runs", []):
        tool = run.get("tool", {})
        for rule in tool.get("driver", {}).get("rules", []):
            ids.add(rule["id"])
        for ext in tool.get("extensions", []) or []:
            for rule in ext.get("rules", []):
                ids.add(rule["id"])
    return ids


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("left", type=Path, help="reference SARIF (CodeQL)")
    ap.add_argument("right", type=Path, help="candidate SARIF (clang-tidy)")
    ap.add_argument("--prefix", default="", help="only compare findings under this path prefix")
    ap.add_argument("--common-rules", action="store_true", help="only compare rules present in both tools")
    ap.add_argument("--show", type=int, default=20, help="unmatched findings to list per side")
    ap.add_argument(
        "--files",
        type=Path,
        default=None,
        help="only compare findings in files listed (one path per line) in this file",
    )
    args = ap.parse_args(argv)

    rules = rule_ids(args.left) & rule_ids(args.right) if args.common_rules else None
    left = findings(args.left, args.prefix, rules)
    right = findings(args.right, args.prefix, rules)
    if args.files is not None:
        allowed = {line.strip() for line in args.files.read_text(encoding="utf-8").splitlines() if line.strip()}
        left = {f for f in left if f[1] in allowed}
        right = {f for f in right if f[1] in allowed}
    both, only_left, only_right = left & right, left - right, right - left
    per_rule = Counter()
    for rule, _, _ in left:
        per_rule[(rule, "left")] += 1
    for rule, _, _ in right:
        per_rule[(rule, "right")] += 1
    for rule, _, _ in both:
        per_rule[(rule, "both")] += 1
    all_rules = sorted({r for r, _ in per_rule})
    print(f"{'rule':52s} {'left':>6s} {'right':>6s} {'both':>6s}")
    for rule in all_rules:
        print(f"{rule:52s} {per_rule[(rule, 'left')]:6d} {per_rule[(rule, 'right')]:6d} {per_rule[(rule, 'both')]:6d}")
    total = len(left | right)
    agreement = 100.0 * len(both) / total if total else 100.0
    print(
        f"\n{len(left)} left, {len(right)} right, {len(both)} matched; "
        f"{len(only_left)} left-only, {len(only_right)} right-only; agreement {agreement:.1f}%"
    )
    for label, items in (("left-only", only_left), ("right-only", only_right)):
        for f in sorted(items)[: args.show]:
            print(f"  {label}: {f[0]} {f[1]}:{f[2]}")
        if len(items) > args.show:
            print(f"  ... {len(items) - args.show} more {label}")
    return 0 if not only_left and not only_right else 1


if __name__ == "__main__":
    sys.exit(main())
