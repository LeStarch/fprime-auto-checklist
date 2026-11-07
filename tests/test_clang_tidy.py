"""clang-tidy backend: rule mapping, diagnostic parsing and SARIF conversion (no clang-tidy needed)."""

import json

import pytest

from fprime_auto_checklist import clang_tidy as ct
from fprime_auto_checklist import codeql as cq
from fprime_auto_checklist.checklist import codeql_rule_ids


def test_every_checklist_rule_has_a_clang_tidy_check():
    rules = ct.rules()
    by_id = {r.id: r for r in rules}
    mapping = ct.check_to_rule(rules)
    for rule_id in codeql_rule_ids():
        assert rule_id in by_id, f"{rule_id} has no clang-tidy rule entry"
        assert rule_id in mapping.values(), f"{rule_id} is not produced by any clang-tidy check"
    assert len([r for r in rules if r.check]) == len(list(cq.QUERIES_DIR.glob("*.ql")))


def test_checks_argument_enables_only_ported_checks():
    arg = ct.checks_argument(ct.rules())
    assert arg.startswith("-*,")
    names = arg.split(",")[1:]
    assert "fprime-no-printf" in names and "clang-analyzer-core.uninitialized.Assign" in names
    assert all(n.startswith(("fprime-", "clang-analyzer-")) for n in names)


def test_parse_diagnostics_handles_notes_and_multiple_checks():
    out = (
        "/p/Svc/X/X.cpp:10:5: warning: Call to printf; use F Prime events/logging instead. [fprime-no-printf]\n"
        '    printf("x");\n'
        "    ^\n"
        "/p/Svc/X/X.hpp:3:7: warning: msg [fprime-public-data-member,fprime-magic-numbers]\n"
        "/p/Svc/X/X.cpp:12:1: note: some note\n"
        "3 warnings generated.\n"
    )
    diags = ct.parse_diagnostics(out)
    assert [(d.check, d.file, d.line, d.column) for d in diags] == [
        ("fprime-no-printf", "/p/Svc/X/X.cpp", 10, 5),
        ("fprime-public-data-member", "/p/Svc/X/X.hpp", 3, 7),
        ("fprime-magic-numbers", "/p/Svc/X/X.hpp", 3, 7),
    ]


def _diag(check, file, line, col=1, msg="m"):
    return ct.Diagnostic(check=check, file=str(file), line=line, column=col, level="warning", message=msg)


def test_to_sarif_uses_codeql_ids_dedups_and_fingerprints(tmp_path):
    src = tmp_path / "Svc" / "X"
    src.mkdir(parents=True)
    hpp = src / "X.hpp"
    hpp.write_text("class X {\n  public:\n    int v;\n};\n", encoding="utf-8")
    diags = [
        _diag("fprime-public-data-member", hpp, 3, 9),
        _diag("fprime-public-data-member", hpp, 3, 9),  # same header seen from a second TU
        _diag("clang-analyzer-core.uninitialized.Assign", src / "X.cpp", 7),
        _diag("readability-foo", src / "X.cpp", 8),  # not ours: dropped
    ]
    rules = ct.rules()
    sarif = ct.to_sarif(diags, tmp_path, rules, tool_version="t", tidy_version="15.0.7")
    run = sarif["runs"][0]
    results = run["results"]
    assert [r["ruleId"] for r in results] == ["cpp/fprime/public-data-member", "cpp/uninitialized-local"]
    loc = results[0]["locations"][0]["physicalLocation"]
    assert loc["artifactLocation"]["uri"] == "Svc/X/X.hpp"
    assert loc["region"] == {"startLine": 3, "startColumn": 9}
    fp = results[0]["partialFingerprints"]["primaryLocationLineHash"]
    assert fp.endswith(":1") and len(fp) == 34
    rule_ids = {r["id"] for r in run["tool"]["driver"]["rules"]}
    assert set(codeql_rule_ids()) <= rule_ids
    # The existing CodeQL scoping consumes it unchanged.
    evidence = cq.scope_findings(sarif, "Svc/X", codeql_rule_ids(), tool="clang-tidy")
    assert evidence.tool == "clang-tidy"
    assert [str(f) for f in evidence.findings["cpp/fprime/public-data-member"]] == ["Svc/X/X.hpp:3 m"]
    assert "cpp/fprime/no-printf" in evidence.rules_run

    # Fingerprint survives the line moving (unrelated code inserted above).
    hpp.write_text("// header comment\n\nclass X {\n  public:\n    int v;\n};\n", encoding="utf-8")
    moved = ct.to_sarif([_diag("fprime-public-data-member", hpp, 5, 9)], tmp_path, rules)
    assert moved["runs"][0]["results"][0]["partialFingerprints"]["primaryLocationLineHash"] == fp


def test_module_sources_selects_module_tus(tmp_path):
    module = tmp_path / "Svc" / "X"
    module.mkdir(parents=True)
    build = tmp_path / "build-fprime-automatic-native"
    build.mkdir()
    entries = [
        {"directory": str(build), "file": str(module / "X.cpp"), "command": "c++ X.cpp"},
        {"directory": str(build), "file": str(build / "Svc/X/XComponentAc.cpp"), "command": "c++"},
        {"directory": str(build), "file": str(tmp_path / "Svc/Y/Y.cpp"), "command": "c++"},
        {"directory": str(build), "file": "../Svc/X/sub/Z.cpp", "command": "c++"},
    ]
    (build / "compile_commands.json").write_text(json.dumps(entries), encoding="utf-8")
    assert ct.find_compile_commands(tmp_path, None) == build / "compile_commands.json"
    assert ct.find_compile_commands(tmp_path, build) == build / "compile_commands.json"
    assert ct.module_sources(build / "compile_commands.json", module) == [module / "X.cpp", module / "sub" / "Z.cpp"]


def test_find_plugin_and_clang_tidy_errors(tmp_path, monkeypatch):
    monkeypatch.delenv("FPRIME_TIDY_PLUGIN", raising=False)
    monkeypatch.delenv("CLANG_TIDY", raising=False)
    monkeypatch.setattr(ct, "NATIVE_DIR", tmp_path)
    with pytest.raises(ct.ClangTidyError):
        ct.find_plugin(None)
    plugin = tmp_path / "libFprimeTidy.so"
    plugin.write_bytes(b"")
    assert ct.find_plugin(str(plugin)) == plugin
    (tmp_path / "clang-tidy-path.txt").write_text(str(tmp_path / "ct"), encoding="utf-8")
    monkeypatch.setenv("PATH", str(tmp_path))
    with pytest.raises(ct.ClangTidyError):
        ct.find_clang_tidy(None, plugin)
    (tmp_path / "ct").write_text("", encoding="utf-8")
    assert ct.find_clang_tidy(None, plugin) == str(tmp_path / "ct")
    with pytest.raises(ct.ClangTidyError):
        ct.find_compile_commands(tmp_path, None)


def test_run_records_failed_translation_units(tmp_path):
    """A translation unit clang-tidy cannot parse is reported, not silently dropped; all failing raises."""
    fake = tmp_path / "clang-tidy"
    fake.write_text(
        "#!/bin/sh\n"
        'case "$@" in *bad.cpp*) echo "bad.cpp:1:1: error: unknown type" >&2; exit 1;; esac\n'
        'echo "good.cpp:3:5: warning: printf() is not allowed [fprime-no-printf]"\n'
    )
    fake.chmod(0o755)
    (tmp_path / "compile_commands.json").write_text("[]")
    good, bad = tmp_path / "good.cpp", tmp_path / "bad.cpp"
    good.write_text("int main() {}\n")
    bad.write_text("bogus\n")
    failed: list = []
    diags = ct.run(
        str(fake),
        tmp_path / "p.so",
        tmp_path / "compile_commands.json",
        [good, bad],
        tmp_path / "log",
        header_filter=".*",
        checks="-*,fprime-*",
        jobs=1,
        failures=failed,
    )
    assert [d.check for d in diags] == ["fprime-no-printf"]
    assert failed == [bad]
    assert "failed translation units" in (tmp_path / "log").read_text()
    sarif = ct.to_sarif(diags, tmp_path, ct.rules(), failed_sources=failed)
    notes = sarif["runs"][0]["invocations"][0]["toolExecutionNotifications"]
    assert len(notes) == 1 and "bad.cpp" in notes[0]["message"]["text"]
    with pytest.raises(ct.ClangTidyError):
        ct.run(
            str(fake),
            tmp_path / "p.so",
            tmp_path / "compile_commands.json",
            [bad],
            tmp_path / "log2",
            header_filter=".*",
            checks="-*",
            jobs=1,
        )
