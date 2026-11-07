"""Convergence: the clang-tidy plugin must flag exactly the lines CodeQL flags on every vendored fixture.

Each ``codeql/fprime-queries-tests/<Query>/`` holds the ``test.cpp`` the CodeQL
query is tested against and the ``.expected`` CodeQL output.  Running the
ported ``fprime-<rule>`` check on the same file must report the same lines.
Skipped when the plugin is not built or clang-tidy is unavailable; the native
build is required for the clang-tidy mode anyway.
"""

import importlib.util
import os
from pathlib import Path

import pytest

from fprime_auto_checklist import clang_tidy as ct

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "codeql" / "fprime-queries-tests"


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


@pytest.mark.parametrize("fixture", sorted(p.name for p in FIXTURES.iterdir() if p.is_dir()))
def test_clang_tidy_matches_codeql_expected(fixture):
    tidy, plugin = _tooling()
    oracle = _oracle()
    query_dir = FIXTURES / fixture
    rule = oracle.rule_id(query_dir)
    check = oracle.check_name(rule)
    assert check in ct.check_to_rule(ct.rules()), f"{check} not registered for {rule}"
    diags = oracle.run_clang_tidy(tidy, plugin, check, query_dir / "test.cpp", [])
    got = {int(d["line"]) for d in diags if Path(d["file"]).name == "test.cpp"}
    assert got == oracle.expected_lines(query_dir), f"{fixture}: clang-tidy {sorted(got)} vs CodeQL expected"


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
