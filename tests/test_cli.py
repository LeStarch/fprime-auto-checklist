"""End-to-end run on the Widget fixture with CodeQL skipped or pre-computed."""

import json
import os
import subprocess
import sys
from pathlib import Path

from fprime_auto_checklist import cli

FIXTURES = Path(__file__).resolve().parent / "fixtures"


def test_run_skip_codeql_writes_checklist(tmp_path):
    # Copied out of this git checkout so the fixture's hardware-run record
    # (fake commit id) is judged as it would be in a plain source tree.
    project, _widget = _copy_widget(tmp_path)
    out = tmp_path / "checklist.md"
    rc = cli.main(
        [
            "run",
            "--project",
            str(project),
            "--module",
            "Svc/Widget",
            "--skip-codeql",
            "-o",
            str(out),
            "--json-output",
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


def test_run_applies_docs_overrides(tmp_path):
    project, widget = _copy_widget(tmp_path)
    (widget / "docs" / "checklist-overrides.md").write_text(
        "| # | Status | By | Date | Justification |\n|---|---|---|---|---|\n"
        "| 3.21 | YES | J. Doe | 2026-11-07 | No ISRs: passive queued component. |\n"
        "| 2.12 | N/A | llm:claude | 2026-11-07 | Priorities come from the deployment config. |\n"
        "| 3.33 | | | | |\n",
        encoding="utf-8",
    )
    out = tmp_path / "checklist.md"
    rc = cli.main(
        [
            "run",
            "--project",
            str(project),
            "--module",
            "Svc/Widget",
            "--skip-codeql",
            "-o",
            str(out),
            "--json-output",
            str(tmp_path / "r.json"),
        ]
    )
    assert rc == 1  # other deliberate failures remain
    text = out.read_text(encoding="utf-8")
    assert "2 overridden by `docs/checklist-overrides.md`" in text
    assert "| 3.21 |" in text and "YES *(override)*" in text and "Override by J. Doe on 2026-11-07" in text
    assert "| 3.33 |" in text and "CL-3" in text
    handoff = text.split("## Hand-off", 1)[1]
    assert "3.33" in handoff and "3.21" not in handoff
    data = json.loads((tmp_path / "r.json").read_text(encoding="utf-8"))
    rows = {i["id"]: i for i in data["items"]}
    assert rows["3.21"]["status"] == "YES" and rows["3.21"]["tool_status"] == "REVIEW"
    assert rows["2.12"]["status"] == "N/A" and rows["2.12"]["tool_status"] == "NO"
    assert data["overrides"] == 2 and "override" not in rows["3.33"]


def test_run_rejects_malformed_overrides(tmp_path, capsys):
    project, widget = _copy_widget(tmp_path)
    (widget / "docs" / "checklist-overrides.md").write_text(
        "| # | Status | By | Date | Justification |\n|---|---|---|---|---|\n| 3.21 | YES | | | |\n",
        encoding="utf-8",
    )
    rc = cli.main(
        ["run", "--project", str(project), "--module", "Svc/Widget", "--skip-codeql", "-o", str(tmp_path / "c.md")]
    )
    assert rc == 2
    err = capsys.readouterr().err
    assert "'By' (person or LLM) is required" in err and "a justification is required" in err


def test_run_explicit_missing_overrides_is_usage_error(tmp_path, capsys):
    project, _widget = _copy_widget(tmp_path)
    rc = cli.main(
        [
            "run",
            "--project",
            str(project),
            "--module",
            "Svc/Widget",
            "--skip-codeql",
            "-o",
            str(tmp_path / "c.md"),
            "--overrides",
            str(tmp_path / "missing.md"),
        ]
    )
    assert rc == 2 and "overrides file not found" in capsys.readouterr().err


def test_run_without_overrides_file_is_unchanged(tmp_path):
    project, _widget = _copy_widget(tmp_path)
    out = tmp_path / "checklist.md"
    cli.main(["run", "--project", str(project), "--module", "Svc/Widget", "--skip-codeql", "-o", str(out)])
    text = out.read_text(encoding="utf-8")
    assert "overridden" not in text.split("## Status Legend")[0] and " *(override)* |" not in text


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
            "--json-output",
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
            "--json-output",
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


def _sarif_with_printf():
    return {
        "runs": [
            {
                "tool": {"driver": {"rules": [{"id": "cpp/fprime/no-printf"}]}},
                "results": [
                    {
                        "ruleId": "cpp/fprime/no-printf",
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


def test_non_normalised_module_path_scopes_sarif_correctly(tmp_path):
    sarif = tmp_path / "pre.sarif"
    sarif.write_text(json.dumps(_sarif_with_printf()), encoding="utf-8")
    rc = cli.main(
        [
            "run",
            "--project",
            str(FIXTURES),
            "--module",
            "Svc/../Svc/Widget",
            "--sarif",
            str(sarif),
            "-o",
            str(tmp_path / "c.md"),
            "--json-output",
            str(tmp_path / "r.json"),
        ]
    )
    data = json.loads((tmp_path / "r.json").read_text(encoding="utf-8"))
    assert data["module"] == "Svc/Widget"
    assert {i["id"]: i["status"] for i in data["items"]}["3.35"] == "NO"
    assert rc == cli.EXIT_ITEMS_FAILED


def test_module_outside_project_is_usage_error(tmp_path):
    outside = tmp_path / "Elsewhere"
    outside.mkdir()
    rc = cli.main(
        ["run", "--project", str(FIXTURES), "--module", str(outside), "--skip-codeql", "-o", str(tmp_path / "c.md")]
    )
    assert rc == cli.EXIT_USAGE


def test_codeql_requested_but_unavailable_exits_3(tmp_path):
    rc = cli.main(["run", "--project", str(FIXTURES), "--module", "Svc/Widget", "-o", str(tmp_path / "c.md")])
    assert rc == cli.EXIT_CODEQL_UNAVAILABLE


def _copy_widget(tmp_path):
    import shutil

    project = tmp_path / "proj"
    shutil.copytree(FIXTURES, project)
    return project, project / "Svc" / "Widget"


def _statuses(project, tmp_path):
    cli.main(
        [
            "run",
            "--project",
            str(project),
            "--module",
            "Svc/Widget",
            "--skip-codeql",
            "-o",
            str(tmp_path / "c.md"),
            "--json-output",
            str(tmp_path / "r.json"),
        ]
    )
    data = json.loads((tmp_path / "r.json").read_text(encoding="utf-8"))
    return {i["id"]: i["status"] for i in data["items"]}


def test_pattern_drift_missing_sdd(tmp_path):
    """Composite-check failure patterns must still match the vendored scripts' messages."""
    project, widget = _copy_widget(tmp_path)
    (widget / "docs" / "sdd.md").unlink()
    statuses = _statuses(project, tmp_path)
    for item in ("1.1", "1.4", "1.7", "2.1", "2.2", "2.7", "2.8", "2.10", "2.15"):
        assert statuses[item] == "NO", item


def test_pattern_drift_diagrams_and_kinds(tmp_path):
    project, widget = _copy_widget(tmp_path)
    sdd = widget / "docs" / "sdd.md"
    text = sdd.read_text(encoding="utf-8")
    assert "```mermaid" in text
    text = "\n".join(
        line
        for line in text.splitlines()
        if "mermaid" not in line and "sequenceDiagram" not in line and "-->" not in line and "->>" not in line
    )
    text = text.replace("| Memory |", "| |").replace("| Performance |", "| |")
    sdd.write_text(text, encoding="utf-8")
    statuses = _statuses(project, tmp_path)
    assert statuses["2.2"] == "NO" and statuses["2.7"] == "NO"
    assert statuses["1.4"] == "NO" and statuses["1.6"] == "NO"
    assert statuses["1.7"] == "YES"
