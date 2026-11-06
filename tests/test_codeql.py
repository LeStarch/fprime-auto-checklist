"""License gate and SARIF scoping (no CodeQL CLI needed)."""

import json

import pytest

from fprime_auto_checklist import codeql


def test_license_detection(tmp_path):
    assert codeql.detect_osi_license(tmp_path) is None
    (tmp_path / "LICENSE.txt").write_text("Apache License\nVersion 2.0, January 2004\n", encoding="utf-8")
    assert codeql.detect_osi_license(tmp_path) == "Apache-2.0"
    (tmp_path / "LICENSE.txt").write_text("Proprietary. All rights reserved.", encoding="utf-8")
    assert codeql.detect_osi_license(tmp_path) is None
    (tmp_path / "COPYING").write_text("GNU GENERAL PUBLIC LICENSE Version 3", encoding="utf-8")
    assert codeql.detect_osi_license(tmp_path) == "GPL"


def test_license_gate_requires_osi_or_ack(tmp_path):
    refused = codeql.license_gate(tmp_path, None)
    assert not refused.allowed and "--codeql-license-ack" in refused.basis
    assert codeql.license_gate(tmp_path, "open-source").allowed
    assert codeql.license_gate(tmp_path, "ghas").allowed
    (tmp_path / "LICENSE").write_text("MIT License\nPermission is hereby granted, free of charge", encoding="utf-8")
    decision = codeql.license_gate(tmp_path, None)
    assert decision.allowed and "MIT" in decision.basis


def test_find_codeql_missing(tmp_path, monkeypatch):
    monkeypatch.delenv("CODEQL", raising=False)
    monkeypatch.setenv("PATH", str(tmp_path))
    with pytest.raises(codeql.CodeqlError):
        codeql.find_codeql(None)
    fake = tmp_path / "codeql"
    fake.write_text("", encoding="utf-8")
    assert codeql.find_codeql(str(fake)) == str(fake)


def _sarif(results):
    return {
        "runs": [
            {
                "tool": {
                    "driver": {"rules": [{"id": "cpp/fprime/no-printf"}]},
                    "extensions": [{"rules": [{"id": "cpp/uninitialized-local"}]}],
                },
                "results": [
                    {
                        "ruleId": rule,
                        "message": {"text": msg},
                        "locations": [
                            {"physicalLocation": {"artifactLocation": {"uri": uri}, "region": {"startLine": line}}}
                        ],
                    }
                    for rule, uri, line, msg in results
                ],
            }
        ]
    }


def test_scope_findings_keeps_module_hand_written_only(tmp_path):
    doc = _sarif(
        [
            ("cpp/fprime/no-printf", "Svc/Widget/Widget.cpp", 10, "printf used\nsecond line"),
            ("cpp/fprime/no-printf", "Svc/WidgetOther/Other.cpp", 3, "elsewhere"),
            (
                "cpp/fprime/no-printf",
                "build-fprime-automatic-native/F-Prime/Svc/Widget/WidgetComponentAc.cpp",
                1,
                "autocode",
            ),
            ("cpp/uninitialized-local", "Svc/Widget/test/ut/WidgetTester.cpp", 5, "ut"),
        ]
    )
    path = tmp_path / "r.sarif"
    path.write_text(json.dumps(doc), encoding="utf-8")
    ev = codeql.scope_findings(
        codeql.load_sarif(path), "Svc/Widget", ["cpp/fprime/no-printf", "cpp/fprime/float-equality"]
    )
    assert ev.ran
    assert [str(f) for f in ev.findings["cpp/fprime/no-printf"]] == ["Svc/Widget/Widget.cpp:10 printf used"]
    assert ev.findings["cpp/fprime/float-equality"] == []
    assert len(ev.findings["cpp/uninitialized-local"]) == 1
    assert ev.autocode_findings == 1
    assert ev.rules_run == ["cpp/fprime/no-printf", "cpp/uninitialized-local"]
