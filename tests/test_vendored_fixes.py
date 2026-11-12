"""Local fixes applied to the vendored component-checks scripts (see VENDORED.md)."""

import subprocess

import pytest

from fprime_auto_checklist.checks import check_ci_status, check_hardware_run, check_req_ports


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


_FPP = """module Dev {
  passive component Thing {
    sync input port go: Fw.Ping
    output port goComplete: Fw.Ping
    output port busOut: Drv.ByteStreamSend
  }
}
"""


def _module(tmp_path, sdd: str):
    (tmp_path / "Thing.fpp").write_text(_FPP)
    (tmp_path / "docs").mkdir()
    (tmp_path / "docs" / "sdd.md").write_text(sdd)
    return str(tmp_path)


_REQS = """# Thing

## Requirements

| ID | Requirement | Verification |
|---|---|---|
| REQ-1 | The go port shall start a transfer. | Unit Test |
"""


def test_r6_companion_and_sdd_description_cover_ports(tmp_path):
    sdd = (
        _REQS
        + """
## Ports

| Port | Kind | Type | Purpose |
|---|---|---|---|
| go | sync input | Fw.Ping | start |
| goComplete | output | Fw.Ping | |
| busOut | output | Drv.ByteStreamSend | frames to the bus driver |
"""
    )
    assert check_req_ports.main(["--module", _module(tmp_path, sdd)]) == 0


def test_r6_port_without_requirement_description_or_base_fails(tmp_path, capsys):
    sdd = (
        _REQS
        + """
## Ports

| Port | Kind | Type | Purpose |
|---|---|---|---|
| busOut | output | Drv.ByteStreamSend | |
"""
    )
    assert check_req_ports.main(["--module", _module(tmp_path, sdd)]) != 0
    out = capsys.readouterr().out
    assert "busOut" in out and "goComplete" not in out
