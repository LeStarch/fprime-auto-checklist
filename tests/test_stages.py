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


def _doc(out):
    from fprime_auto_checklist.carry import parse_document
    return parse_document(out)


def _rows(out):
    return {v.item.id: v for v in _doc(out).verdicts}


def _queue(out):
    import re
    text = out.read_text(encoding="utf-8")
    return dict(re.findall(r"^- \*\*(\d+\.\d+)\*\* \((review|re-review \(inputs changed\)|read)", text, re.M))


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
    doc = _doc(out)
    assert doc.stage == "design"
    assert set(_queue(out)) == REQ_ITEMS and set(_queue(out).values()) == {"review"}
    rows = _rows(out)
    assert rows["3.21"].status == "REVIEW" and rows["1.1"].status == "REVIEW"
    text = out.read_text(encoding="utf-8")
    assert "_Stage: **design**" in text and "Review queue for the **design** stage (6 row(s))" in text
    assert "Deferred to a later stage: 3.21, 3.23, 3.33, 3.38, 4.3." in text
    assert not out.with_suffix(".json").exists()  # the Markdown is the only record


def test_decide_records_and_rerenders(tmp_path):
    project = _project(tmp_path)
    out = tmp_path / "cl.md"
    _run(project, out, "--stage", "design")
    rc = cli.main(["decide", "--checklist", str(out), "--from", str(_batch(tmp_path))])
    assert rc == 0
    rows = _rows(out)
    assert all(rows[i].status == "YES" and rows[i].kind == results.BY_REVIEW and rows[i].notes == [] for i in REQ_ITEMS)
    text = out.read_text(encoding="utf-8")
    assert "| 1.1 |" in text and " by review |" in text
    assert "Nothing to review at the **design** stage." in text
    # a reviewer NO carries its one-line reason; a tool row can only be overridden with a note
    assert cli.main(["decide", "--checklist", str(out), "--item", "1.4", "--status", "NO", "--note", "Gap: no memory requirement."]) == 0
    assert _rows(out)["1.4"].notes == ["Gap: no memory requirement."] and _rows(out)["1.4"].kind == results.BY_REVIEW
    assert cli.main(["decide", "--checklist", str(out), "--item", "2.12", "--status", "YES"]) == 2  # override needs a note
    assert cli.main(["decide", "--checklist", str(out), "--item", "2.12", "--status", "YES", "--note", "D4 false positive: priorities set in Top."]) == 0
    row = _rows(out)["2.12"]
    assert row.status == "YES" and row.kind == results.OVERRIDE and row.notes[-1].startswith("D4 false positive")
    assert "overridden by reviewer |" in out.read_text(encoding="utf-8")


def test_decisions_survive_rerun_and_go_stale_when_their_section_changes(tmp_path):
    project = _project(tmp_path)
    out = tmp_path / "cl.md"
    _run(project, out, "--stage", "design")
    cli.main(["decide", "--checklist", str(out), "--from", str(_batch(tmp_path))])
    cli.main(["decide", "--checklist", str(out), "--item", "2.12", "--status", "YES", "--note", "D4 false positive."])
    out2 = tmp_path / "cl2.md"
    _run(project, out2, "--stage", "design", "--previous", str(out))
    rows = _rows(out2)
    assert all(rows[i].status == "YES" and rows[i].kind == results.BY_REVIEW for i in REQ_ITEMS)
    assert rows["2.12"].status == "YES" and rows["2.12"].kind == results.OVERRIDE
    assert _queue(out2) == {}
    sdd = next((project / "Svc" / "Widget" / "docs").glob("*.md"))
    sdd.write_text(sdd.read_text(encoding="utf-8") + "\n\nAn added paragraph.\n", encoding="utf-8")
    out3 = tmp_path / "cl3.md"
    _run(project, out3, "--stage", "design", "--previous", str(out2))
    rows = _rows(out3)
    assert all(any(n.startswith("STALE:") for n in rows[i].notes) for i in REQ_ITEMS)  # section 1/2 inputs changed
    assert set(_queue(out3)) == REQ_ITEMS and set(_queue(out3).values()) == {"re-review (inputs changed)"}
    assert rows["2.12"].kind == results.BY_TOOL and rows["2.12"].status == "NO"  # override dropped: inputs changed


def test_closeout_queues_no_and_unchecked_tool_rows_to_read(tmp_path):
    project = _project(tmp_path)
    out = tmp_path / "cl.md"
    _run(project, out, "--stage", "closeout")
    q = _queue(out)
    rows = _rows(out)
    assert q["1.1"] == "review" and q["2.12"] == "read" and q["3.3"] == "read"
    assert rows["2.12"].status == "NO" and rows["3.3"].status == "UNCHECKED"
    assert all(rows[i].status in ("NO", "UNCHECKED") for i, why in q.items() if why == "read")
    assert not any(rows[i].status == "YES" for i in q)  # a tool YES is never queued
    rc = cli.main(["decide", "--checklist", str(out), "--item", "3.3", "--status", "N/A", "--note", "No analyzer here."])
    assert rc == 0
    rows = _rows(out)
    assert rows["3.3"].status == "N/A" and rows["3.3"].kind == results.OVERRIDE
    assert cli.main(["decide", "--checklist", str(out), "--item", "2.12", "--status", "NO", "--note", "x"]) == 2  # same as the tool
    assert "3.3" not in _queue(out)


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
    assert outcome == {"1.4": "carried", "1.5": "stale", "3.3": "carried"}  # a legacy audit that overrides becomes an override row
    by_id = {v.item.id: v for v in verdicts}
    assert by_id["1.4"].status == "YES" and by_id["1.4"].kind == results.BY_REVIEW and by_id["1.5"].status == "NO"
    assert by_id["3.3"].status == "N/A" and by_id["3.3"].kind == results.OVERRIDE
    assert any(n.startswith("STALE: the SDD changed since abc123abc123 (design stage)") for n in by_id["1.5"].notes)
