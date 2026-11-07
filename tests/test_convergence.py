"""Convergence: the clang-tidy plugin must flag exactly the lines CodeQL flags on every fixture.

Two tiers share one oracle format.  ``codeql/fprime-queries-tests/<Query>/`` is
the vendored nasa/fprime fixture (CodeQL's own happy-path cases);
``codeql/fprime-queries-tests-adversarial/<Query>/`` holds hand-written
adversarial inputs (macros, templates, lambdas, casts, labels, do/while, ...)
whose ``.expected`` was produced by CodeQL itself via ``native/test/adversarial.py learn``.
Running the ported ``fprime-<rule>`` check on each ``test.cpp`` must report the same lines.
Skipped when the plugin is not built or clang-tidy is unavailable; the native
build is required for the clang-tidy mode anyway.
"""

import importlib.util
import os
from pathlib import Path

import pytest

from fprime_auto_checklist import clang_tidy as ct

ROOT = Path(__file__).resolve().parents[1]
TIERS = {
    "codeql": ROOT / "codeql" / "fprime-queries-tests",
    "adversarial": ROOT / "codeql" / "fprime-queries-tests-adversarial",
}
FIXTURES = TIERS["codeql"]


def _oracle():
    spec = importlib.util.spec_from_file_location("run_fixtures", ROOT / "native" / "test" / "run_fixtures.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _tooling():
    try:
        plugin = ct.find_plugin(os.environ.get("FPRIME_TIDY_PLUGIN"))
        tidy = ct.find_clang_tidy(os.environ.get("CLANG_TIDY"), plugin)
    except ct.ClangTidyError as exc:
        pytest.skip(str(exc))
    return tidy, plugin


@pytest.mark.parametrize(
    "tier, fixture",
    [(tier, p.name) for tier, root in TIERS.items() for p in sorted(root.iterdir()) if p.is_dir()],
    ids=lambda v: v if v in TIERS else v,
)
def test_clang_tidy_matches_codeql_expected(tier, fixture):
    tidy, plugin = _tooling()
    oracle = _oracle()
    query_dir = TIERS[tier] / fixture
    assert (query_dir / f"{fixture}.expected").exists(), f"{tier}/{fixture}: run native/test/adversarial.py learn"
    rule = oracle.rule_id(query_dir)
    check = oracle.check_name(rule)
    assert check in ct.check_to_rule(ct.rules()), f"{check} not registered for {rule}"
    diags = oracle.run_clang_tidy(tidy, plugin, check, query_dir / "test.cpp", [])
    got = {int(d["line"]) for d in diags if Path(d["file"]).name == "test.cpp"}
    assert got == oracle.expected_lines(query_dir), f"{tier}/{fixture}: clang-tidy {sorted(got)} vs CodeQL expected"


def test_plugin_lists_every_ported_check():
    tidy, plugin = _tooling()
    import re
    import subprocess

    listed = subprocess.run(
        [tidy, f"--load={plugin}", "--checks=-*,fprime-*", "--list-checks"], capture_output=True, text=True, check=False
    ).stdout
    available = set(re.findall(r"^\s+(fprime-[\w-]+)$", listed, re.M))
    wanted = {r.check for r in ct.rules() if r.check}
    assert wanted <= available, sorted(wanted - available)
