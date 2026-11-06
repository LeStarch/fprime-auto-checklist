#!/usr/bin/env bash
# Re-copy the vendored CodeQL pack and check scripts from local checkouts.
set -euo pipefail
FPRIME="${1:?fprime checkout}"; ACTIONS="${2:?fprime-actions checkout}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
Q="$ROOT/src/fprime_auto_checklist/queries/fprime-queries"
C="$ROOT/src/fprime_auto_checklist/checks"
rm -rf "$Q" "$ROOT/codeql/fprime-queries-tests"
cp -r "$FPRIME/.github/codeql/fprime-queries" "$Q"
cp -r "$FPRIME/.github/codeql/fprime-queries-tests" "$ROOT/codeql/fprime-queries-tests"
find "$Q" "$ROOT/codeql/fprime-queries-tests" \( -name ".codeql" -o -name "*.testproj" \) -prune -exec rm -rf {} +
# Pin the pack's dependencies (kept under version control; see README "CodeQL licensing").
codeql pack install "$Q"
find "$C" -name "check_*.py" -o -name "_*.py" | grep -v __init__ | xargs rm -f
cp "$ACTIONS"/component-checks/scripts/_*.py "$ACTIONS"/component-checks/scripts/check_*.py "$C/"
cp "$ACTIONS"/coverage-common/scripts/_config.py "$ACTIONS"/coverage-common/scripts/_tiers.py "$C/"
rm -f "$C/run_checks.py"
sed -i -E 's/^from (_[a-z_]+) import/from .\1 import/; s/^from (check_[a-z_]+) import/from .\1 import/; s/^import (check_[a-z_]+)$/from . import \1/' "$C"/*.py
rm -rf "$ROOT/tests/fixtures/Svc"
cp -r "$ACTIONS/component-checks/tests/fixtures/Svc" "$ROOT/tests/fixtures/Svc"
echo "vendored from fprime $(git -C "$FPRIME" rev-parse --short HEAD), fprime-actions $(git -C "$ACTIONS" rev-parse --short HEAD); update VENDORED.md"
