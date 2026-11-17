"""Carry human decisions forward from a previous rendering of the checklist.

Only items the tool itself leaves at ``REVIEW`` are taken from the previous
file: a reviewer who edited such a row to ``YES``/``NO``/``N/A`` keeps that
decision and their notes.  Tool-decided rows are always recomputed.  Each
carried row is stamped with the commit the previous checklist was rendered at
and flagged when the component's sources changed since, because a judgement
made on older code may no longer hold.
"""

from __future__ import annotations

import json
import re
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

from .fingerprint import SECTION_INPUTS
from .results import BY_REVIEW, BY_TOOL, NA, NO, OVERRIDE, REVIEW, UNCHECKED, YES, Verdict
from .stages import Decision, decisions_from_json

_HEADER_COMMIT_RE = re.compile(r"_Pre-filled by fprime-auto-checklist [^`]*`[^`]*` at `([0-9a-f]{7,40})`")
_FINGERPRINTS_RE = re.compile(r"_Section fingerprints[^:]*:\s*((?:\d=[0-9a-f]+\s*)+)_")
_ROW_RE = re.compile(r"^\|\s*(\d+\.\d+)\s*\|(.*)\|\s*([^|]*?)\s*\|\s*(.*?)\s*\|\s*$")
# The status is the LEADING token of the cell (after an optional emoji); "by review" also contains REVIEW.
_STATUS_RE = re.compile(r"^\W*(UNCHECKED|REVIEW|N/A|YES|NO)\b", re.IGNORECASE)
_STAMP_RE = re.compile(r"@([0-9a-fA-F]{7,40})\b(.*)$")
_STATUS_OF = {"UNCHECKED": UNCHECKED, "REVIEW": REVIEW, "N/A": NA, "YES": YES, "NO": NO}
_DECIDED = (YES, NO, NA)
# Notes the tool writes on rows it leaves to a reviewer start with the review rule id.
_REVIEW_NOTE_RE = re.compile(r"^(CL-\d+|Human):")


@dataclass
class PreviousChecklist:
    path: Path
    commit: Optional[str]
    rows: Dict[str, Tuple[str, List[str]]] = field(default_factory=dict)
    #: Per row: (commit the row was decided at, kind) parsed from the Status cell stamp.
    stamps: Dict[str, Tuple[Optional[str], str]] = field(default_factory=dict)
    #: Item ids the previous *tool run* left at REVIEW (from the JSON written next to the
    #: Markdown); None when no JSON is available and the notes must be relied on instead.
    review_then: Optional[Set[str]] = None
    #: Per-section input fingerprints recorded when the previous checklist was rendered.
    fingerprints: Dict[str, str] = field(default_factory=dict)
    #: Decisions recorded by ``decide`` in the JSON sidecar (item id -> Decision); these take
    #: precedence over hand-edited Markdown rows.
    decisions: Dict[str, "Decision"] = field(default_factory=dict)
    stage: Optional[str] = None


def _status_of(cell: str) -> Optional[str]:
    m = _STATUS_RE.match(cell.strip())
    return _STATUS_OF[m.group(1).upper()] if m else None


def parse_stamp(cell: str) -> Tuple[Optional[str], str]:
    """``(commit, kind)`` from a Status cell: ``YES @abc123`` (tool), ``… by review``, ``… overridden by reviewer``."""
    m = _STAMP_RE.search(cell)
    tail = (m.group(2) if m else cell).lower()
    kind = OVERRIDE if "overridden by reviewer" in tail else BY_REVIEW if "by review" in tail else BY_TOOL
    return (m.group(1) if m else None), kind


def _review_rows_from_json(json_path: Path) -> Optional[Set[str]]:
    try:
        doc = json.loads(json_path.read_text(encoding="utf-8"))
        return {i["id"] for i in doc["items"] if i.get("status") == REVIEW}
    except (OSError, ValueError, KeyError, TypeError):
        return None


def parse_previous(path: Path, json_path: Optional[Path] = None) -> PreviousChecklist:
    """Read a previous rendering; ``json_path`` (default: ``<path>.json`` beside it) tells which
    rows the tool itself had left at REVIEW, so tool-decided rows of an older run are never
    mistaken for reviewer decisions."""
    text = path.read_text(encoding="utf-8", errors="replace")
    header = _HEADER_COMMIT_RE.search(text)
    previous = PreviousChecklist(path=path, commit=header.group(1) if header else None)
    fps = _FINGERPRINTS_RE.search(text)
    if fps:
        previous.fingerprints = dict(pair.split("=", 1) for pair in fps.group(1).split())
    sidecar = json_path or path.with_suffix(".json")
    if sidecar.is_file():
        previous.review_then = _review_rows_from_json(sidecar)
        try:
            doc = json.loads(sidecar.read_text(encoding="utf-8"))
            if isinstance(doc.get("fingerprints"), dict):
                previous.fingerprints = {str(k): str(v) for k, v in doc["fingerprints"].items()}
            previous.decisions = decisions_from_json(doc)
            previous.stage = doc.get("stage")
        except (OSError, ValueError, AttributeError):
            pass
    for line in text.splitlines():
        match = _ROW_RE.match(line)
        if not match:
            continue
        status = _status_of(match.group(3))
        if status is None:
            continue
        notes = [n.strip() for n in re.split(r"<br\s*/?>", match.group(4)) if n.strip()]
        previous.rows[match.group(1)] = (status, notes)
        previous.stamps[match.group(1)] = parse_stamp(match.group(3))
    return previous


def section_sources_changed(project: Path, module: Path, section: str, since: Optional[str], current: Optional[str]) -> Optional[bool]:
    """Did the files ``section`` judges change between ``since`` and ``current``? (git; None when it cannot tell)."""
    if not since or not current:
        return None
    if since == current or current.startswith(since) or since.startswith(current):
        return False
    from .fingerprint import section_inputs
    paths = section_inputs(module).get(section) or []
    rels = [str(q.resolve().relative_to(project.resolve())) for q in paths if q.exists()]
    if not rels:
        return None
    proc = subprocess.run(["git", "diff", "--quiet", since, current, "--", *rels], cwd=project, capture_output=True, text=True)
    return False if proc.returncode == 0 else True if proc.returncode == 1 else None


def sources_changed(project: Path, module_rel: str, since: Optional[str], current: Optional[str]) -> Optional[bool]:
    """True/False when git can compare the module between the two commits, None when it cannot."""
    if not since or not current:
        return None
    if since == current or current.startswith(since) or since.startswith(current):
        return False
    proc = subprocess.run(
        ["git", "diff", "--quiet", since, current, "--", module_rel],
        cwd=project,
        capture_output=True,
        text=True,
    )
    if proc.returncode == 0:
        return False
    if proc.returncode == 1:
        return True
    return None


def was_review_row(previous: PreviousChecklist, item_id: str, notes: List[str]) -> bool:
    """True when the previous tool run left this row to a reviewer (so a decided status is theirs)."""
    stamp = previous.stamps.get(item_id)
    if stamp is not None and stamp[1] in (BY_REVIEW, OVERRIDE):
        return True
    if previous.review_then is not None:  # legacy JSON sidecar (pre-0.2) names the rows the tool left at REVIEW
        return item_id in previous.review_then
    if stamp is not None and stamp[0] is not None:
        return False  # a 0.2 stamp without "by review" is the tool's own row
    return any(_REVIEW_NOTE_RE.match(n) for n in notes)


def section_changed(
    previous: PreviousChecklist, fingerprints: Optional[Dict[str, str]], section: str, fallback: Optional[bool]
) -> Optional[bool]:
    """Did the inputs of ``section`` change since the previous checklist?

    Compares the section fingerprints when both sides recorded them; otherwise falls back to
    the whole-module git comparison (``fallback``), or None when nothing can be compared."""
    if fingerprints and section in fingerprints and section in previous.fingerprints:
        return fingerprints[section] != previous.fingerprints[section]
    return fallback


def apply_previous(
    verdicts: List[Verdict],
    previous: PreviousChecklist,
    *,
    changed: Optional[bool],
    skipped: Optional[List[str]] = None,
    fingerprints: Optional[Dict[str, str]] = None,
    project: Optional[Path] = None,
    module: Optional[Path] = None,
    current: Optional[str] = None,
) -> List[str]:
    """Overwrite REVIEW verdicts with the reviewer's decision from ``previous``; returns carried item ids.

    Stamped ``overridden by reviewer`` rows carry over a tool verdict too (while the section's inputs are unchanged
    since the row's commit and the tool still disagrees).

    A carried row is flagged STALE only when the inputs its own section judges changed
    (per-section ``fingerprints``); ``changed`` is the whole-module fallback when the previous
    checklist recorded no fingerprints.  Decided rows that cannot be recognised as former
    review rows are appended to ``skipped``."""
    carried: List[str] = []
    for verdict in verdicts:
        if verdict.item.id in previous.decisions:
            continue
        row = previous.rows.get(verdict.item.id)
        if row is None or row[0] not in _DECIDED:
            continue
        status, notes = row
        row_commit, kind = previous.stamps.get(verdict.item.id, (None, BY_TOOL))
        row_commit = row_commit or previous.commit
        reviewed_at = row_commit[:12] if row_commit else "an unknown commit"
        section = verdict.item.section
        if kind == OVERRIDE and verdict.status != REVIEW:
            if verdict.status == status:
                continue  # the tool now agrees: the override is moot, the tool row stands
            section_state = section_changed(previous, fingerprints, section, changed)
            if section_state is None and project is not None and module is not None:
                section_state = section_sources_changed(project, module, section, row_commit, current)
            if section_state is not False:
                continue  # inputs changed (or unknown): the tool's verdict stands, the reviewer may override again
            verdict.status, verdict.kind, verdict.commit = status, OVERRIDE, row_commit
            verdict.notes = verdict.notes + [n for n in notes if n not in verdict.notes and not n.startswith("STALE")]
            carried.append(verdict.item.id)
            continue
        if verdict.status != REVIEW:
            continue
        if not was_review_row(previous, verdict.item.id, notes):
            if skipped is not None:
                skipped.append(verdict.item.id)
            continue
        notes = [n for n in notes if not n.startswith("Carried from ") and not n.startswith("STALE")]
        if status == YES:
            notes = []
        inputs = SECTION_INPUTS.get(section, "the component")
        section_state = section_changed(previous, fingerprints, section, changed)
        if section_state is None and project is not None and module is not None:
            section_state = section_sources_changed(project, module, section, row_commit, current)
        if section_state is True:
            notes.append(f"STALE: {inputs} changed since {reviewed_at}; re-review.")
        elif section_state is None:
            notes.append(
                f"STALE?: cannot tell whether {inputs} changed since the review "
                "(no fingerprint or commit to compare); re-review."
            )
        verdict.status, verdict.notes, verdict.kind, verdict.commit = status, notes, BY_REVIEW, row_commit
        carried.append(verdict.item.id)
    return carried


_HEADER_MODULE_RE = re.compile(r"_Pre-filled by fprime-auto-checklist [^`]*`([^`]*)`")
_COMPONENT_RE = re.compile(r"^\*\*Component Name:\*\* (.*?)\s*$", re.M)
_OWNER_RE = re.compile(r"^\*\*Component Owner:\*\* (.*?)\s*$", re.M)
_CONTRIB_RE = re.compile(r"^\*\*Component Contributors:\*\* (.*?)\s*$", re.M)
_STAGE_RE = re.compile(r"_Stage: \*\*(\w+)\*\*")
_CODEQL_LINE_RE = re.compile(r"^_(?:CodeQL|clang-tidy)[^\n]*_$", re.M)


@dataclass
class Document:
    """A rendered checklist read back: everything `decide` needs to re-render it from the Markdown alone."""

    path: Path
    component: str
    module_rel: str
    commit: Optional[str]
    owner: str
    contributors: str
    stage: Optional[str]
    fingerprints: Dict[str, str]
    codeql_line: Optional[str]
    verdicts: List[Verdict]


def parse_document(path: Path) -> Document:
    from .results import get_item, judge_all, CodeqlEvidence
    previous = parse_previous(path)
    text = path.read_text(encoding="utf-8", errors="replace")
    def _m(rx, default=""):
        m = rx.search(text)
        return m.group(1).strip() if m else default
    verdicts: List[Verdict] = []
    for verdict in judge_all({}, CodeqlEvidence(ran=False, reason="")):  # the item list, in order
        row = previous.rows.get(verdict.item.id)
        if row is None:
            verdicts.append(Verdict(verdict.item, REVIEW, [], previous.commit, BY_TOOL))
            continue
        status, notes = row
        commit, kind = previous.stamps.get(verdict.item.id, (None, BY_TOOL))
        verdicts.append(Verdict(verdict.item, status, notes, commit or previous.commit, kind))
    owner, contributors = _m(_OWNER_RE), _m(_CONTRIB_RE)
    return Document(
        path=path,
        component=_m(_COMPONENT_RE),
        module_rel=_m(_HEADER_MODULE_RE),
        commit=previous.commit,
        owner="" if owner.startswith("_[") else owner,
        contributors="" if contributors.startswith("_[") else contributors,
        stage=_m(_STAGE_RE, None) or previous.stage,
        fingerprints=dict(previous.fingerprints),
        codeql_line=(_CODEQL_LINE_RE.search(text).group(0) if _CODEQL_LINE_RE.search(text) else None),
        verdicts=verdicts,
    )
