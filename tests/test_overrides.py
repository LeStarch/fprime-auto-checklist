"""Human/LLM overrides: parsing docs/checklist-overrides.md and merging it over the tool verdicts."""

import pytest

from fprime_auto_checklist import overrides as ov
from fprime_auto_checklist.results import NA, NO, REVIEW, YES, Verdict, get_item, verdicts_to_json

TABLE = """# Checklist overrides

| # | Status | By | Date | Justification |
|---|--------|----|------|---------------|
| 3.21 | YES | J. Doe | 2026-11-07 | No ISRs: passive queued component. |
| 2.12 | n/a | llm:claude | 2026-11-07 | Container priorities come from the deployment config. |
| 3.33 | | | | |
| 4.3 | 🟠 NO | J. Doe | 2026-11-07 | Boundary tests missing for the queue depth. |
"""


def test_parse_reads_set_rows_and_skips_unset():
    got = ov.parse_overrides(TABLE, "checklist-overrides.md")
    assert set(got) == {"3.21", "2.12", "4.3"}
    assert got["3.21"].status == YES and got["3.21"].by == "J. Doe" and got["3.21"].date == "2026-11-07"
    assert got["2.12"].status == NA and got["2.12"].by == "llm:claude"
    assert got["4.3"].status == NO and got["4.3"].source == "checklist-overrides.md:8"


def test_parse_accepts_column_aliases_and_no_table_is_empty():
    text = "| Item | Status | Reviewer | Notes |\n|---|---|---|---|\n| 3.38 | yes | me | Traced in SDD §4. |\n"
    assert ov.parse_overrides(text)["3.38"].justification == "Traced in SDD §4."
    assert ov.parse_overrides("just prose, no table\n") == {}


@pytest.mark.parametrize(
    "row, problem",
    [
        ("| 9.9 | YES | me | 2026-11-07 | x |", "unknown checklist item '9.9'"),
        ("| 3.21 | MAYBE | me | 2026-11-07 | x |", "status must be YES, NO or N/A"),
        ("| 3.21 | YES | | 2026-11-07 | x |", "'By' (person or LLM) is required"),
        ("| 3.21 | YES | me | 2026-11-07 | |", "a justification is required"),
        ("| 3.21 | YES | me | 7 Nov | x |", "date must be YYYY-MM-DD"),
    ],
)
def test_malformed_rows_are_rejected_not_ignored(row, problem):
    text = "| # | Status | By | Date | Justification |\n|---|---|---|---|---|\n" + row + "\n"
    with pytest.raises(ov.OverrideError, match=problem.replace("(", "\\(").replace(")", "\\)")):
        ov.parse_overrides(text)


def test_duplicate_item_is_rejected():
    text = (
        "| # | Status | By | Date | Justification |\n|---|---|---|---|---|\n"
        "| 3.21 | YES | me | 2026-11-07 | x |\n| 3.21 | NO | me | 2026-11-07 | y |\n"
    )
    with pytest.raises(ov.OverrideError, match="overridden twice"):
        ov.parse_overrides(text)


def test_apply_replaces_only_overridden_items_and_keeps_tool_verdict():
    verdicts = [
        Verdict(get_item("3.21"), REVIEW, ["CL-1: rule"]),
        Verdict(get_item("2.12"), NO, ["D4: container X has no priority"]),
        Verdict(get_item("3.33"), REVIEW, ["CL-3: rule"]),
    ]
    merged = ov.apply_overrides(verdicts, ov.parse_overrides(TABLE))
    by_id = {v.item.id: v for v in merged}
    assert by_id["3.33"] is verdicts[2]  # unset: untouched
    assert by_id["3.21"].status == YES and by_id["3.21"].tool_status == REVIEW
    assert by_id["3.21"].notes[0] == "Override by J. Doe on 2026-11-07: No ISRs: passive queued component."
    assert "REVIEW" in by_id["3.21"].notes[1] and by_id["3.21"].notes[2] == "CL-1: rule"
    assert by_id["2.12"].status == NA and by_id["2.12"].tool_status == NO
    rows = {r["id"]: r for r in verdicts_to_json(merged)}
    assert rows["2.12"]["tool_status"] == NO and rows["2.12"]["override"]["by"] == "llm:claude"
    assert "override" not in rows["3.33"]
