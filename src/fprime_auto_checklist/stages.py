"""Stage-gated review of the checklist.

The checklist is filled in three passes of the development process, each by a
skill that drives this tool:

* ``design``         -- after the SDD and FPP model: sections 1-2 are due;
* ``implementation`` -- after the code and unit tests: sections 1-4 are due;
* ``closeout``       -- after system testing: every section is due and every
  tool-decided row is audited.

``run --stage`` records the stage, marks REVIEW rows of later sections
"deferred", and writes the *review queue*: the rows the skill must decide now
(undecided REVIEW rows of due sections, carried decisions whose inputs changed,
and -- at close-out -- tool rows without a current audit).  ``decide`` records
each decision in the JSON sidecar with the commit and section fingerprint it
was made against, so staleness is judged per row on the next run.
"""

from __future__ import annotations

import datetime as dt
import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional

from .checklist import REVIEW_RULES
from .fingerprint import SECTION_INPUTS
from .results import NA, NO, REVIEW, UNCHECKED, YES, Verdict

STAGES: Dict[str, Dict[str, object]] = {
    "design": {"due": ("1", "2"), "title": "design (SDD + FPP model)"},
    "implementation": {"due": ("1", "2", "3", "4"), "title": "implementation and unit test"},
    "closeout": {"due": ("1", "2", "3", "4", "5"), "title": "close-out"},
}
STAGE_ORDER = tuple(STAGES)
KIND_REVIEW, KIND_AUDIT = "review", "audit"
_DECIDED = (YES, NO, NA)
#: An audit may also confirm an UNCHECKED row (the process step is simply not done yet).
_AUDIT_STATUSES = (YES, NO, NA, UNCHECKED)


def due_sections(stage: Optional[str]) -> tuple:
    return tuple(STAGES[stage]["due"]) if stage else ("1", "2", "3", "4", "5")


def stage_for_section(section: str) -> str:
    for stage in STAGE_ORDER:
        if section in STAGES[stage]["due"]:
            return stage
    return "closeout"


@dataclass
class Decision:
    """A reviewer's recorded decision on one row (``kind`` review) or their audit of a tool row."""

    status: str
    notes: List[str]
    kind: str = KIND_REVIEW
    commit: Optional[str] = None
    fingerprint: Optional[str] = None
    stage: Optional[str] = None
    date: Optional[str] = None
    #: For audits: the tool status that was audited; the audit lapses when the tool decides differently.
    tool_status: Optional[str] = None

    def to_json(self) -> dict:
        return {k: v for k, v in vars(self).items() if v is not None}

    @classmethod
    def from_json(cls, doc: dict) -> Optional["Decision"]:
        try:
            status = str(doc["status"])
            notes = [str(n) for n in doc.get("notes", [])]
        except (KeyError, TypeError):
            return None
        return cls(
            status=status,
            notes=notes,
            kind=str(doc.get("kind", KIND_REVIEW)),
            commit=doc.get("commit"),
            fingerprint=doc.get("fingerprint"),
            stage=doc.get("stage"),
            date=doc.get("date"),
            tool_status=doc.get("tool_status"),
        )

    def stamp(self) -> str:
        at = self.commit[:12] if self.commit else "an unknown commit"
        where = f" ({self.stage} stage)" if self.stage else ""
        return f"{at}{where}"


@dataclass
class QueueEntry:
    id: str
    section: str
    reason: str  # "review" | "stale" | "audit"
    status: str
    rule: Optional[str]
    notes: List[str] = field(default_factory=list)

    def to_json(self) -> dict:
        return vars(self)


def _rule_of(verdict: Verdict) -> Optional[str]:
    for ev in verdict.item.evidence:
        if ev.kind in ("review", "human"):
            return ev.ref
    return None


def rule_text(rule: Optional[str]) -> str:
    return REVIEW_RULES.get(rule or "", "")


def apply_decisions(
    verdicts: List[Verdict],
    decisions: Dict[str, Decision],
    *,
    fingerprints: Dict[str, str],
    source: str,
) -> Dict[str, str]:
    """Apply recorded decisions; returns ``{item_id: "carried" | "stale" | "lapsed"}``.

    A review decision replaces a REVIEW verdict; it is *stale* when the section fingerprint it
    was made against differs from the current one (the decision still applies, flagged).  An
    audit is kept only while the tool's status and the section fingerprint are unchanged;
    otherwise it *lapses* and the row goes back to the queue at close-out."""
    outcome: Dict[str, str] = {}
    for verdict in verdicts:
        decision = decisions.get(verdict.item.id)
        if decision is None:
            continue
        section = verdict.item.section
        current_fp = fingerprints.get(section)
        same_inputs = decision.fingerprint is not None and current_fp is not None and decision.fingerprint == current_fp
        if decision.kind == KIND_AUDIT:
            if verdict.status == REVIEW or decision.tool_status != verdict.status or not same_inputs:
                outcome[verdict.item.id] = "lapsed"
                continue
            # A confirmed audit is silent in the table (its stamp lives in the JSON); an
            # override replaces the tool's notes with the auditor's one line.
            if decision.status in _AUDIT_STATUSES and decision.status != verdict.status:
                verdict.notes = [f"Audit ({decision.stamp()}): " + " ".join(decision.notes)]
                verdict.status = decision.status
            elif verdict.status in (NO, UNCHECKED) and decision.notes:
                # The auditor's one-line summary replaces the tool's finding text; the rule id stays.
                verdict.notes = [" ".join(decision.notes) + _rule_suffix(verdict.notes)]
            outcome[verdict.item.id] = "carried"
            continue
        if verdict.status != REVIEW or decision.status not in _DECIDED:
            continue
        # A reviewer's YES needs no note; NO and N/A carry their one-line reason.
        notes = [] if decision.status == YES else [n for n in decision.notes if not n.startswith("STALE")]
        if decision.fingerprint is None or current_fp is None:
            notes.append(f"STALE?: no fingerprint to compare with the review at {decision.stamp()}; re-review.")
            outcome[verdict.item.id] = "stale"
        elif not same_inputs:
            notes.append(
                f"STALE: {SECTION_INPUTS.get(section, 'the inputs')} changed since {decision.stamp()}; re-review."
            )
            outcome[verdict.item.id] = "stale"
        else:
            outcome[verdict.item.id] = "carried"
        verdict.status = decision.status
        verdict.notes = notes
    return outcome


_RULE_ID_RE = re.compile(r"`([^`]+)`|^([A-Z]{1,3}\d{1,2})\b")


def _rule_suffix(tool_notes: List[str]) -> str:
    """`` [rule]`` naming the tool rule(s) behind a finding, from the tool's own notes."""
    ids: List[str] = []
    for note in tool_notes:
        m = _RULE_ID_RE.search(note)
        if m:
            rule = m.group(1) or m.group(2)
            if rule not in ids:
                ids.append(rule)
    return f" [{', '.join(ids)}]" if ids else ""


def mark_deferred(verdicts: List[Verdict], stage: Optional[str]) -> List[str]:
    """Ids of the REVIEW rows of sections not yet due (listed in the Hand-off section, not in the rows)."""
    due = due_sections(stage)
    return [v.item.id for v in verdicts if v.status == REVIEW and v.item.section not in due]


def build_queue(
    verdicts: List[Verdict],
    stage: Optional[str],
    outcome: Dict[str, str],
    audited: Dict[str, Decision],
) -> List[QueueEntry]:
    """The rows a reviewer must decide at ``stage``.

    * every REVIEW row of a due section (nothing carried for it);
    * every carried decision flagged stale;
    * at close-out, every tool-decided row (YES/NO/N/A/UNCHECKED) without a current audit."""
    due = due_sections(stage)
    queue: List[QueueEntry] = []
    for verdict in verdicts:
        section = verdict.item.section
        if section not in due:
            continue
        rule = _rule_of(verdict)
        state = outcome.get(verdict.item.id)
        if verdict.status == REVIEW:
            queue.append(QueueEntry(verdict.item.id, section, "review", verdict.status, rule, list(verdict.notes)))
        elif state == "stale":
            queue.append(QueueEntry(verdict.item.id, section, "stale", verdict.status, rule, list(verdict.notes)))
        elif stage == "closeout" and verdict.item.id not in audited and state != "carried":
            queue.append(QueueEntry(verdict.item.id, section, "audit", verdict.status, rule, list(verdict.notes)))
    return queue


def decisions_from_json(doc: dict) -> Dict[str, Decision]:
    out: Dict[str, Decision] = {}
    for item in doc.get("items", []):
        raw = item.get("decision") if isinstance(item, dict) else None
        if isinstance(raw, dict) and "id" in item:
            decision = Decision.from_json(raw)
            if decision is not None:
                out[str(item["id"])] = decision
    return out


def new_decision(
    status: str,
    notes: List[str],
    *,
    kind: str,
    commit: Optional[str],
    fingerprint: Optional[str],
    stage: Optional[str],
    tool_status: Optional[str] = None,
    now: Optional[dt.datetime] = None,
) -> Decision:
    now = now or dt.datetime.now(dt.timezone.utc)
    return Decision(
        status=status,
        notes=list(notes),
        kind=kind,
        commit=commit,
        fingerprint=fingerprint,
        stage=stage,
        date=now.strftime("%Y-%m-%d"),
        tool_status=tool_status,
    )
