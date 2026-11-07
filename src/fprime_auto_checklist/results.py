"""Turn raw evidence (check records, CodeQL findings) into checklist verdicts."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Dict, List, Optional

from .checklist import ITEMS, REVIEW_RULES, Evidence, Item

if TYPE_CHECKING:  # pragma: no cover
    from .overrides import Override

YES, NO, NA, REVIEW, UNCHECKED = "YES", "NO", "N/A", "REVIEW", "UNCHECKED"

STATUS_LABELS = {
    YES: "\U0001f7e2 YES",
    NO: "\U0001f7e0 NO",
    NA: "\u26aa N/A",
    REVIEW: "\U0001f535 REVIEW",
    UNCHECKED: "\u26ab UNCHECKED",
}

#: A check that skipped for one of these reasons found nothing to check in the
#: model, so the item does not apply.  Any other skip reason (tool missing,
#: no token, ...) means the check did not run.
_NOT_APPLICABLE = re.compile(
    r"^no (commands|data product|state machines|typed|C\+\+ sources|unit-test sources|dictionary)"
    r"|declares? no|managed by framework",
    re.IGNORECASE,
)
#: A check that skipped because the inputs the item asks about are missing
#: altogether ("do requirements exist" with no requirements) has failed.
_SKIP_IS_FAILURE = re.compile(r"^no requirements found", re.IGNORECASE)

MAX_NOTES = 6


@dataclass
class Finding:
    rule: str
    file: str
    line: int
    message: str

    def __str__(self) -> str:
        return f"{self.file}:{self.line} {self.message}"


@dataclass
class CodeqlEvidence:
    """Module-scoped static-analysis results (CodeQL or the clang-tidy swap-in); ``ran`` is False when skipped."""

    ran: bool
    reason: str = ""
    tool: str = "CodeQL"
    findings: Dict[str, List[Finding]] = field(default_factory=dict)
    rules_run: List[str] = field(default_factory=list)
    autocode_findings: int = 0


@dataclass
class Verdict:
    item: Item
    status: str
    notes: List[str]
    #: Set when a human/LLM override replaced the tool's verdict (``status`` is then the override).
    tool_status: Optional[str] = None
    override: Optional["Override"] = None


def _check_outcome(ev: Evidence, records: Dict[str, dict]) -> tuple:
    record = records.get(ev.ref)
    if record is None:
        return UNCHECKED, [f"{ev.ref}: check not run"]
    status = record.get("status")
    if status == "pass":
        return YES, [f"{ev.ref} pass" + (f" ({record['detail']})" if record.get("detail") else "")]
    if status == "skip":
        reason = str(record.get("detail") or "skipped")
        if _SKIP_IS_FAILURE.search(reason):
            return NO, [f"{ev.ref}: {reason}"]
        if _NOT_APPLICABLE.search(reason):
            return NA, [f"{ev.ref}: {reason}"]
        return UNCHECKED, [f"{ev.ref} not run: {reason}"]
    failures = [str(f) for f in record.get("failures") or []]
    if ev.pattern:
        rx = re.compile(ev.pattern, re.IGNORECASE)
        failures = [f for f in failures if rx.search(f)]
        if not failures:
            return YES, [f"{ev.ref} pass (no failures concern this item)"]
    return NO, [f"{ev.ref}: {f}" for f in failures]


def _codeql_outcome(ev: Evidence, codeql: CodeqlEvidence) -> tuple:
    if not codeql.ran:
        return UNCHECKED, [f"{codeql.tool} `{ev.ref}` not run: {codeql.reason}"]
    if ev.ref not in codeql.rules_run:
        return UNCHECKED, [f"{codeql.tool} `{ev.ref}` was not part of the analysis"]
    findings = codeql.findings.get(ev.ref, [])
    if findings:
        return NO, [f"`{ev.ref}` {f}" for f in findings]
    return YES, [f"`{ev.ref}` 0 findings"]


def _combine(outcomes: List[str]) -> str:
    if NO in outcomes:
        return NO
    if UNCHECKED in outcomes:
        return UNCHECKED
    if REVIEW in outcomes:
        return REVIEW
    if outcomes and all(o == NA for o in outcomes):
        return NA
    return YES


def judge_item(item: Item, records: Dict[str, dict], codeql: CodeqlEvidence) -> Verdict:
    outcomes: List[str] = []
    notes: List[str] = []
    for ev in item.evidence:
        if ev.kind == "check":
            status, ev_notes = _check_outcome(ev, records)
        elif ev.kind == "codeql":
            status, ev_notes = _codeql_outcome(ev, codeql)
        elif ev.kind == "human":
            # The hardware run is attested by its record; the record check (C2)
            # decides YES/NO, this evidence only adds the instruction when missing.
            status, ev_notes = REVIEW, [f"Human: {REVIEW_RULES[ev.ref]}"]
        else:
            status, ev_notes = REVIEW, [f"{ev.ref}: {REVIEW_RULES[ev.ref]}"]
        outcomes.append(status)
        notes.extend(ev_notes)
    status = _combine(outcomes)
    if any(ev.kind == "human" for ev in item.evidence):
        tool_outcomes = [o for ev, o in zip(item.evidence, outcomes, strict=True) if ev.kind != "human"]
        tool_status = _combine(tool_outcomes)
        if tool_status == YES:
            status = YES
            notes = [n for n in notes if not n.startswith("Human:")]
            notes.append("Attested by the hardware-run record checked by C2.")
        else:
            status = REVIEW if tool_status in (UNCHECKED, NA) else NO
    return Verdict(item, status, _trim(notes))


def _trim(notes: List[str]) -> List[str]:
    if len(notes) <= MAX_NOTES:
        return notes
    return notes[:MAX_NOTES] + [f"... {len(notes) - MAX_NOTES} more"]


def judge_all(records: Dict[str, dict], codeql: CodeqlEvidence) -> List[Verdict]:
    return [judge_item(item, records, codeql) for item in ITEMS]


def summarize(verdicts: List[Verdict]) -> Dict[str, int]:
    counts = {s: 0 for s in (YES, NO, NA, REVIEW, UNCHECKED)}
    for v in verdicts:
        counts[v.status] += 1
    return counts


def verdicts_to_json(verdicts: List[Verdict]) -> List[dict]:
    rows = []
    for v in verdicts:
        row = {"id": v.item.id, "section": v.item.section, "text": v.item.text, "status": v.status, "notes": v.notes}
        if v.override is not None:
            row["tool_status"] = v.tool_status
            row["override"] = {
                "status": v.override.status,
                "by": v.override.by,
                "date": v.override.date,
                "justification": v.override.justification,
                "source": v.override.source,
            }
        rows.append(row)
    return rows


def get_item(item_id: str) -> Optional[Item]:
    return next((i for i in ITEMS if i.id == item_id), None)
