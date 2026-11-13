"""Stage-gated runs, the review queue, and recorded decisions (``decide``)."""

import json
import shutil
from pathlib import Path

from fprime_auto_checklist import cli, results, stages

FIXTURES = Path(__file__).resolve().parent / "fixtures"
REQ_ITEMS = {"1.1", "1.2", "1.3", "1.4", "1.5", "1.6"}
LATER_REVIEW = {"3.21", "3.23", "3.33", "3.38", "4.3"}


def _project(tmp_path):
    project = tmp_path / "proj"
    shutil.copytree(FIXTURES, project)
    return project


def _run(project, out, *extra):
    return cli.main(
        ["run", "--project", str(project), "--module", "Svc/Widget", "--skip-codeql", "-o", str(out), *extra]
    )


def _load(out):
    return json.loads(out.with_suffix(".json").read_text(encoding="utf-8"))


def _batch(tmp_path):
    path = tmp_path / "decisions.json"
    path.write_text(
        json.dumps(
            [{"id": i, "status": "YES", "notes": [f"{i}: covered by REQ-WIDGET-001."]} for i in sorted(REQ_ITEMS)]
        )
    )
    return path


def test_design_stage_queues_sections_1_and_2_only(tmp_path):
    project = _project(tmp_path)
    out = tmp_path / "cl.md"
    _run(project, out, "--stage", "design")
    doc = _load(out)
    assert doc["stage"] == "design"
    assert {e["id"] for e in doc["queue"]} == REQ_ITEMS
    assert all(e["reason"] == "review" and e["rule_text"] for e in doc["queue"])
    by_id = {i["id"]: i for i in doc["items"]}
    assert by_id["3.21"]["status"] == "REVIEW"
    assert not any("Deferred" in n for n in by_id["3.21"]["notes"])
    text = out.read_text(encoding="utf-8")
    assert "_Stage: **design**" in text and "Review queue for the **design** stage (6 row(s))" in text
    assert "Deferred to a later stage: 3.21, 3.23, 3.33, 3.38, 4.3." in text


def test_decide_records_and_rerenders(tmp_path):
    project = _project(tmp_path)
    out = tmp_path / "cl.md"
    _run(project, out, "--stage", "design")
    rc = cli.main(
        ["decide", "--checklist", str(out), "--item", "1.4", "--status", "YES", "--note", "Static buffers only."]
    )
    assert rc == 0
    doc = _load(out)
    item = next(i for i in doc["items"] if i["id"] == "1.4")
    assert item["status"] == "YES" and item["tool_status"] == "REVIEW"
    assert item["decision"]["kind"] == "review" and item["decision"]["stage"] == "design"
    assert item["decision"]["fingerprint"] == doc["fingerprints"]["1"]
    assert "1.4" not in {e["id"] for e in doc["queue"]} and len(doc["queue"]) == 5
    text = out.read_text(encoding="utf-8")
    assert "| 1.4 |" in text and "Static buffers only." not in text  # a YES is silent; the note lives in the JSON
    assert item["decision"]["notes"] == ["Static buffers only."]
    assert cli.main(["decide", "--checklist", str(out), "--item", "1.5", "--status", "NO"]) == 2  # NO needs a note
    assert cli.main(["decide", "--checklist", str(out), "--item", "9.9", "--status", "YES"]) == 2


def test_decisions_survive_rerun_and_go_stale_when_their_section_changes(tmp_path):
    project = _project(tmp_path)
    out = tmp_path / "cl.md"
    _run(project, out, "--stage", "design")
    assert cli.main(["decide", "--checklist", str(out), "--from", str(_batch(tmp_path))]) == 0
    assert _load(out)["queue"] == []

    out2 = tmp_path / "cl2.md"
    _run(project, out2, "--stage", "implementation", "--previous", str(out))
    doc2 = _load(out2)
    by_id = {i["id"]: i for i in doc2["items"]}
    assert by_id["1.4"]["status"] == "YES" and not any(n.startswith("STALE") for n in by_id["1.4"]["notes"])
    assert by_id["1.4"]["decision"]["stage"] == "design"  # the recorded decision travels with the sidecar
    assert {e["id"] for e in doc2["queue"]} == LATER_REVIEW

    sdd = project / "Svc" / "Widget" / "docs" / "sdd.md"
    sdd.write_text(sdd.read_text(encoding="utf-8") + "\nThe widget shall also beep.\n", encoding="utf-8")
    out3 = tmp_path / "cl3.md"
    _run(project, out3, "--stage", "implementation", "--previous", str(out2))
    doc3 = _load(out3)
    by_id = {i["id"]: i for i in doc3["items"]}
    assert by_id["1.4"]["status"] == "YES" and any(n.startswith("STALE:") for n in by_id["1.4"]["notes"])
    assert {e["id"] for e in doc3["queue"] if e["reason"] == "stale"} == REQ_ITEMS


def test_closeout_queues_every_tool_row_for_audit_and_audits_lapse(tmp_path):
    project = _project(tmp_path)
    out = tmp_path / "cl.md"
    _run(project, out, "--stage", "closeout")
    doc = _load(out)
    reasons = {e["id"]: e["reason"] for e in doc["queue"]}
    assert len(reasons) == 73
    assert reasons["2.12"] == "audit" and reasons["1.1"] == "review" and reasons["3.3"] == "audit"
    tool = {i["id"]: i["tool_status"] for i in doc["items"]}
    assert tool["2.12"] == "NO" and tool["3.3"] == "UNCHECKED"

    rc = cli.main(["decide", "--checklist", str(out), "--item", "2.12", "--status", "NO", "--note", "Confirmed."])
    assert rc == 0
    rc = cli.main(
        ["decide", "--checklist", str(out), "--item", "3.3", "--status", "N/A", "--note", "No analyzer here."]
    )
    assert rc == 0
    doc = _load(out)
    by_id = {i["id"]: i for i in doc["items"]}
    assert by_id["2.12"]["decision"]["kind"] == "audit" and by_id["2.12"]["decision"]["tool_status"] == "NO"
    assert by_id["2.12"]["notes"] == by_id["2.12"]["tool_notes"]  # confirmed audit: silent
    assert by_id["3.3"]["status"] == "N/A" and by_id["3.3"]["notes"][0].startswith("Audit (")
    assert len(doc["queue"]) == 71
    rc = cli.main(["decide", "--checklist", str(out), "--item", "3.4", "--status", "UNCHECKED", "--note", "not run"])
    assert rc == 0  # 3.4 is UNCHECKED without an analyzer: confirming it is allowed
    assert cli.main(["decide", "--checklist", str(out), "--item", "2.13", "--status", "UNCHECKED", "--note", "x"]) == 2
    doc = _load(out)
    assert len(doc["queue"]) == 70

    out2 = tmp_path / "cl2.md"
    _run(project, out2, "--stage", "closeout", "--previous", str(out))
    doc2 = _load(out2)
    assert {e["id"] for e in doc2["queue"]} == {e["id"] for e in doc["queue"]}

    src = next((project / "Svc" / "Widget").glob("*.cpp"))
    src.write_text(src.read_text(encoding="utf-8") + "\n// touched\n", encoding="utf-8")
    out3 = tmp_path / "cl3.md"
    _run(project, out3, "--stage", "closeout", "--previous", str(out2))
    ids = {e["id"] for e in _load(out3)["queue"]}
    assert "3.3" in ids and "2.12" not in ids  # section-3 audit lapsed, section-2 audit holds


def test_apply_decisions_unit():
    verdicts = results.judge_all({}, results.CodeqlEvidence(ran=False, reason="skipped"))
    fps = {"1": "aaa", "2": "bbb", "3": "ccc", "4": "ddd", "5": "eee"}
    decisions = {
        "1.4": stages.new_decision(
            "YES", ["ok"], kind="review", commit="abc123abc123", fingerprint="aaa", stage="design"
        ),
        "1.5": stages.new_decision(
            "NO", ["gap"], kind="review", commit="abc123abc123", fingerprint="old", stage="design"
        ),
        "3.3": stages.new_decision(
            "N/A", ["n/a"], kind="audit", commit="abc123abc123", fingerprint="ccc", stage="closeout", tool_status="YES"
        ),
    }
    outcome = stages.apply_decisions(verdicts, decisions, fingerprints=fps, source="prev.json")
    assert outcome == {"1.4": "carried", "1.5": "stale", "3.3": "lapsed"}  # 3.3 is UNCHECKED now, was audited as YES
    by_id = {v.item.id: v for v in verdicts}
    assert by_id["1.4"].status == "YES" and by_id["1.5"].status == "NO"
    assert any(n.startswith("STALE: the SDD changed since abc123abc123 (design stage)") for n in by_id["1.5"].notes)
