"""End-to-end run on the Widget fixture with CodeQL skipped or pre-computed."""

import json
import os
import subprocess
import sys
from pathlib import Path

from fprime_auto_checklist import cli

FIXTURES = Path(__file__).resolve().parent / "fixtures"


def test_run_skip_codeql_writes_checklist(tmp_path):
    out = tmp_path / "checklist.md"
    rc = cli.main(
        [
            "run",
            "--project",
            str(FIXTURES),
            "--module",
            "Svc/Widget",
            "--skip-codeql",
            "-o",
            str(out),
            "--json",
            str(tmp_path / "r.json"),
            "--owner",
            "Widget Team",
        ]
    )
    assert rc == 1  # the fixture carries deliberate failures (D3, D4, D5, I1)
    text = out.read_text(encoding="utf-8")
    assert "**Component Name:** Widget" in text
    assert "**Component Owner:** Widget Team" in text
    assert text.count("\n| ") >= 73
    assert "| 2.12 |" in text and "OrphanContainer" in text
    assert "| 3.21 |" in text and "CL-1" in text
    assert "## Hand-off" in text
    data = json.loads((tmp_path / "r.json").read_text(encoding="utf-8"))
    assert len(data["items"]) == 73
    assert data["codeql"]["ran"] is False
    assert data["checks"]["SDD"]["status"] == "pass"
    statuses = {i["id"]: i["status"] for i in data["items"]}
    assert statuses["2.12"] == "NO" and statuses["3.3"] == "UNCHECKED" and statuses["5.3"] == "YES"
    assert (tmp_path / "checklist.md.work" / "checks.log").is_file()


def test_run_with_prebuilt_sarif(tmp_path):
    sarif = tmp_path / "pre.sarif"
    sarif.write_text(
        json.dumps(
            {
                "runs": [
                    {
                        "tool": {
                            "driver": {"rules": [{"id": "cpp/fprime/no-printf"}, {"id": "cpp/fprime/float-equality"}]}
                        },
                        "results": [
                            {
                                "ruleId": "cpp/fprime/no-printf",
                                "level": "warning",
                                "message": {"text": "printf"},
                                "locations": [
                                    {
                                        "physicalLocation": {
                                            "artifactLocation": {"uri": "Svc/Widget/Widget.cpp"},
                                            "region": {"startLine": 4},
                                        }
                                    }
                                ],
                            }
                        ],
                    }
                ]
            }
        ),
        encoding="utf-8",
    )
    out = tmp_path / "c.md"
    cli.main(
        [
            "run",
            "--project",
            str(FIXTURES),
            "--module",
            "Svc/Widget",
            "--sarif",
            str(sarif),
            "-o",
            str(out),
            "--json",
            str(tmp_path / "r.json"),
        ]
    )
    data = json.loads((tmp_path / "r.json").read_text(encoding="utf-8"))
    statuses = {i["id"]: i["status"] for i in data["items"]}
    assert statuses["3.35"] == "NO" and statuses["3.9"] == "YES"
    assert statuses["3.3"] == "UNCHECKED"  # rule not part of this SARIF
    assert data["checks"]["I4"]["status"] == "fail"  # static analysis consumes the same SARIF


def test_codeql_license_gate_blocks_fixture_without_license(tmp_path):
    out = tmp_path / "c.md"
    cli.main(
        [
            "run",
            "--project",
            str(FIXTURES),
            "--module",
            "Svc/Widget",
            "-o",
            str(out),
            "--json",
            str(tmp_path / "r.json"),
        ]
    )
    data = json.loads((tmp_path / "r.json").read_text(encoding="utf-8"))
    assert data["codeql"]["ran"] is False and "OSI" in data["codeql"]["reason"]


def test_missing_module_is_usage_error(tmp_path):
    assert (
        cli.main(
            ["run", "--project", str(FIXTURES), "--module", "Svc/Nope", "--skip-codeql", "-o", str(tmp_path / "c.md")]
        )
        == 2
    )


def test_items_and_license_subcommands(capsys, tmp_path):
    assert cli.main(["items"]) == 0
    assert capsys.readouterr().out.count("\n| ") == 73
    assert cli.main(["license-check", "--project", str(tmp_path)]) == 1
    assert cli.main(["license-check", "--project", str(tmp_path), "--codeql-license-ack", "open-source"]) == 0


def test_check_scripts_run_as_modules():
    proc = subprocess.run(
        [
            sys.executable,
            "-m",
            "fprime_auto_checklist.checks.check_req_commands",
            "--module",
            str(FIXTURES / "Svc/Widget"),
        ],
        capture_output=True,
        text=True,
        check=False,
        env={**os.environ, "PYTHONPATH": str(FIXTURES.parent.parent / "src")},
    )
    assert proc.returncode == 0 and proc.stdout.startswith("PASS [R1]")
