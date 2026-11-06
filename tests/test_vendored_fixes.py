"""Local fixes applied to the vendored component-checks scripts (see VENDORED.md)."""

import subprocess

import pytest

from fprime_auto_checklist.checks import check_ci_status, check_hardware_run


def test_unknown_commit_inside_repo_is_not_an_ancestor(tmp_path):
    subprocess.run(["git", "init", "-q", str(tmp_path)], check=True)
    assert check_hardware_run.commit_is_ancestor("0123456abcdef", tmp_path) is False


def test_outside_repo_does_not_fail(tmp_path, monkeypatch):
    monkeypatch.setenv("GIT_CEILING_DIRECTORIES", str(tmp_path.parent))
    assert check_hardware_run.commit_is_ancestor("0123456abcdef", tmp_path) is True


def test_ci_status_rejects_malformed_repo_and_sha(tmp_path):
    with pytest.raises(SystemExit):
        check_ci_status.main(["--module", str(tmp_path), "--repo", "../evil", "--sha", "abcdef1"])
    with pytest.raises(SystemExit):
        check_ci_status.main(["--module", str(tmp_path), "--repo", "owner/name", "--sha", "../x"])
