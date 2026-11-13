"""Local fixes applied to the vendored component-checks scripts (see VENDORED.md)."""

import subprocess

import pytest

from fprime_auto_checklist.checks import (
    check_ci_status,
    check_coverage_threshold,
    check_hardware_run,
    check_include_dependencies,
    check_leaks,
    check_req_ports,
    check_sdd_sections,
)


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


def _i1_project(tmp_path, depends: str):
    """Project with a component including a framework header, an FPP-config header
    (found through a library include root) and a plain library header."""
    fw = tmp_path / "fprime"
    (fw / "Fw" / "Types").mkdir(parents=True)
    (fw / "Fw" / "Types" / "CMakeLists.txt").write_text("register_fprime_module()\n")
    (fw / "Fw" / "Types" / "Assert.hpp").write_text("")
    proj = tmp_path / "proj"
    lib = proj / "Lib"
    (lib / "Cfg").mkdir(parents=True)
    (lib / "Cfg" / "CMakeLists.txt").write_text("register_fprime_config()\n")
    (lib / "Cfg" / "Cfg.fpp").write_text("")
    (lib / "Util").mkdir()
    (lib / "Util" / "CMakeLists.txt").write_text("register_fprime_module()\n")
    (lib / "Util" / "Util.hpp").write_text("")
    comp = lib / "Comp" / "Thing"
    comp.mkdir(parents=True)
    (comp / "Thing.cpp").write_text(
        '#include "Fw/Types/Assert.hpp"\n#include "Cfg/Cfg.hpp"\n#include "Util/Util.hpp"\n'
    )
    (comp / "CMakeLists.txt").write_text(f"register_fprime_module(SOURCES Thing.cpp DEPENDS {depends})\n")
    cache = proj / "build-fprime-automatic-native"
    (cache / "Lib" / "Cfg").mkdir(parents=True)
    (cache / "Lib" / "Cfg" / "Cfg.hpp").write_text("")  # generated config header
    fpp_cache = cache / "Lib" / "Comp" / "Thing" / "fpp-cache"
    fpp_cache.mkdir(parents=True)
    (fpp_cache / "direct.txt").write_text(f"{lib / 'Cfg' / 'Cfg.fpp'}\n")
    (fpp_cache / "framework.txt").write_text("Fw_Comp\n")
    (cache / "locs.fpp").write_text("")
    import json

    (cache / "compile_commands.json").write_text(
        json.dumps(
            [
                {
                    "directory": str(cache),
                    "file": str(comp / "Thing.cpp"),
                    "command": f"c++ -I{proj} -I{fw} -I{cache} -I{lib} -I{cache / 'Lib' / 'Cfg' / '..'} -c {comp / 'Thing.cpp'}",
                }
            ]
        )
    )
    return comp, cache


def test_i1_build_cache_resolves_fpp_and_implicit_dependencies(tmp_path):
    comp, cache = _i1_project(tmp_path, "Lib_Util")
    assert check_include_dependencies.main(["--module", str(comp), "--build-cache", str(cache)]) == 0


def test_i1_build_cache_still_reports_undeclared_library(tmp_path, capsys):
    comp, cache = _i1_project(tmp_path, "Fw_Buffer")
    assert check_include_dependencies.main(["--module", str(comp), "--build-cache", str(cache)]) != 0
    out = capsys.readouterr().out
    assert "Lib_Util" in out and "Fw_Types" not in out and "Cfg" not in out


def test_u5_reads_fprime_util_summary_txt():
    assert check_coverage_threshold.line_percent_txt("lines: 98.7% (223 out of 226)\nbranches: 91.8%") == 98.7
    table = (
        "File   Lines  Exec  Cover  Missing\nX.cpp\n   226  223  98%  293\n"
        + "-" * 20
        + "\nTOTAL      226     223    98%\n"
    )
    assert check_coverage_threshold.line_percent_txt(table) == 98.67
    assert check_coverage_threshold.line_percent_txt("no coverage here") is None


def test_u4_finds_ut_executable_in_project_cache(tmp_path):
    (tmp_path / "settings.ini").write_text("[fprime]\n")
    module = tmp_path / "Lib" / "Comp"
    module.mkdir(parents=True)
    exe = tmp_path / "build-fprime-automatic-native-ut" / "bin" / "Linux" / "Lib_Comp_ut_exe"
    exe.parent.mkdir(parents=True)
    exe.write_text("")
    assert check_leaks.ut_executables(module) == [exe]
    assert check_leaks.leaks_in("definitely lost: 0 bytes\nERROR SUMMARY: 0 errors") == []
    assert check_leaks.leaks_in("definitely lost: 1,024 bytes in 2 blocks") == ["definitely lost: 1,024"]
    assert check_leaks.leaks_in("SUMMARY: LeakSanitizer: 48 byte(s) leaked in 1 allocation(s).") == [
        "SUMMARY: LeakSanitizer: 48 byte(s) leaked in 1 allocation(s)."
    ]
    assert check_leaks.is_asan_instrumented(exe) is False
    exe.write_bytes(b"\x7fELF__asan_init")
    assert check_leaks.is_asan_instrumented(exe) is True


def test_d9_accepts_a_cited_external_document_as_external_interface():
    text = "Reads shall follow datasheet 002-26764 Rev. *D command set. See also ICD 1234-A."
    cited = sorted({f"{m.group(1)} {m.group(2)}" for m in check_sdd_sections._EXTERNAL_DOC_RE.finditer(text)})
    assert cited == ["ICD 1234-A", "datasheet 002-26764 Rev. *D"]
    assert not check_sdd_sections._EXTERNAL_DOC_RE.search("the datasheet says nothing numbered here")


def test_d9_external_references_table(tmp_path):
    from fprime_auto_checklist.checks._sdd import load_sdd

    (tmp_path / "docs").mkdir()
    (tmp_path / "docs" / "sdd.md").write_text(
        "# X\n\n## External References\n\n| Document | Identifier / Revision | Type | Governs |\n|---|---|---|---|\n"
        "| Infineon CY15B102QN datasheet | 002-26764 Rev. *D | Datasheet | command set |\n"
        "| Bus ICD | ICD-1234 | ICD | framing |\n"
    )
    rows = check_sdd_sections.external_references_table(load_sdd(tmp_path))
    assert rows == ["Datasheet 002-26764 Rev. *D: Infineon CY15B102QN datasheet", "ICD ICD-1234: Bus ICD"]
    (tmp_path / "docs" / "sdd.md").write_text(
        "# X\n\n## External References\n\n| Document | Identifier | Type | Governs |\n|---|---|---|---|\n| None | - | - | no external interfaces |\n"
    )
    assert check_sdd_sections.external_references_table(load_sdd(tmp_path)) == []
    (tmp_path / "docs" / "sdd.md").write_text("# X\n\nNo tables here.\n")
    assert check_sdd_sections.external_references_table(load_sdd(tmp_path)) is None
