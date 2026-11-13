"""Carrying reviewer decisions forward from a previous checklist."""

import json

from fprime_auto_checklist import carry, checklist, render, results


def _render(tmp_path, verdicts, commit, review_then=None):
    """Render like the tool does: Markdown plus the JSON sidecar recording the tool's own verdicts."""
    path = tmp_path / "prev.md"
    codeql = results.CodeqlEvidence(ran=False, reason="skipped")
    path.write_text(render.render_markdown(verdicts, component="X", module_rel="Svc/X", commit=commit, codeql=codeql))
    tool_items = [
        {"id": v.item.id, "status": results.REVIEW if v.item.id in (review_then or ()) else v.status} for v in verdicts
    ]
    (tmp_path / "prev.json").write_text(json.dumps({"items": tool_items}))
    return path


def _fresh():
    return results.judge_all({}, results.CodeqlEvidence(ran=False, reason="skipped"))


def test_reviewer_decisions_survive_a_rerun(tmp_path):
    verdicts = _fresh()
    by_id = {v.item.id: v for v in verdicts}
    by_id["1.4"].status, by_id["1.4"].notes = results.YES, ["Memory: static buffers only, SDD section 4."]
    by_id["3.21"].status, by_id["3.21"].notes = results.NA, ["No ISRs."]
    previous = carry.parse_previous(_render(tmp_path, verdicts, "abcdef1234567890", review_then={"1.4", "3.21"}))
    assert previous.commit == "abcdef123456"  # the header carries the 12-char short commit
    assert previous.rows["1.4"][0] == results.YES and previous.rows["3.21"][0] == results.NA

    rerun = _fresh()
    carried = carry.apply_previous(rerun, previous, changed=False)
    again = {v.item.id: v for v in rerun}
    assert carried == ["1.4", "3.21"]
    assert again["1.4"].status == results.YES and again["1.4"].notes == []  # a YES is silent
    assert again["3.21"].status == results.NA
    assert again["1.1"].status == results.REVIEW  # untouched rows stay REVIEW


def test_hand_edited_status_cell_and_stale_flag(tmp_path):
    path = tmp_path / "prev.md"
    path.write_text(
        "_Pre-filled by fprime-auto-checklist 0.1.0 for `Svc/X` at `0123456789ab`. x_\n\n"
        "| # | Item | Status | Notes |\n|---|---|---|---|\n"
        "| 1.5 | HW I/O | YES | SPI only, reviewed by MDS |\n"
        "| 3.33 | units | 🟠 NO | CL-3: timeout lacks unit<br>second note |\n"
        "| 4.3 | boundary | REVIEW | still pending |\n"
    )
    (tmp_path / "prev.json").write_text(
        '{"items": [{"id": "1.5", "status": "REVIEW"}, {"id": "3.33", "status": "REVIEW"}]}'
    )
    previous = carry.parse_previous(path)
    assert previous.rows["1.5"] == (results.YES, ["SPI only, reviewed by MDS"])
    assert previous.rows["3.33"] == (results.NO, ["CL-3: timeout lacks unit", "second note"])
    rerun = _fresh()
    carried = carry.apply_previous(rerun, previous, changed=True)
    again = {v.item.id: v for v in rerun}
    assert carried == ["1.5", "3.33"]
    assert again["4.3"].status == results.REVIEW
    assert any(n.startswith("STALE:") for n in again["1.5"].notes)


def test_tool_decided_rows_are_not_carried(tmp_path):
    path = tmp_path / "prev.md"
    path.write_text("| # | Item | Status | Notes |\n|---|---|---|---|\n| 1.7 | verification | YES | hand-edited |\n")
    rerun = _fresh()
    assert {v.item.id: v for v in rerun}["1.7"].status != results.REVIEW
    assert carry.apply_previous(rerun, carry.parse_previous(path), changed=False) == []


def test_sources_changed_handles_unknown_commits(tmp_path):
    assert carry.sources_changed(tmp_path, "Svc/X", None, "abc") is None
    assert carry.sources_changed(tmp_path, "Svc/X", "abc123", "abc123def") is False
    assert checklist.ITEMS_BY_ID["1.1"].evidence[0].kind == "review"


def test_older_tool_decisions_are_not_mistaken_for_reviewer_decisions(tmp_path):
    md = tmp_path / "prev.md"
    md.write_text(
        "| # | Item | Status | Notes |\n|---|---|---|---|\n"
        "| 1.1 | behavior | 🟠 NO | R9: requirement(s) have no Kind column |\n"
        "| 1.4 | memory | YES | static buffers only |\n"
        "| 1.5 | HW I/O | YES | CL-6: rule text kept by the reviewer |\n"
    )
    # Without a JSON sidecar only rows still carrying the review-rule text are recognised.
    skipped: list = []
    rerun = _fresh()
    assert carry.apply_previous(rerun, carry.parse_previous(md), changed=False, skipped=skipped) == ["1.5"]
    assert skipped == ["1.1", "1.4"]
    # With the previous run's JSON, its REVIEW rows are authoritative.
    (tmp_path / "prev.json").write_text(
        '{"items": [{"id": "1.1", "status": "NO"}, {"id": "1.4", "status": "REVIEW"}, {"id": "1.5", "status": "REVIEW"}]}'
    )
    rerun = _fresh()
    assert carry.apply_previous(rerun, carry.parse_previous(md), changed=False) == ["1.4", "1.5"]
    assert {v.item.id: v for v in rerun}["1.1"].status == results.REVIEW
