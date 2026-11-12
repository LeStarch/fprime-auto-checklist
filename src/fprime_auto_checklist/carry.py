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

from .results import NA, NO, REVIEW, UNCHECKED, YES, Verdict

_HEADER_COMMIT_RE = re.compile(r"_Pre-filled by fprime-auto-checklist [^`]*`[^`]*` at `([0-9a-f]{7,40})`")
_ROW_RE = re.compile(r"^\|\s*(\d+\.\d+)\s*\|(.*)\|\s*([^|]*?)\s*\|\s*(.*?)\s*\|\s*$")
# Longest tokens first so "UNCHECKED" is not read as nothing and "N/A" is not read as "NO".
_STATUS_TOKENS = (("UNCHECKED", UNCHECKED), ("REVIEW", REVIEW), ("N/A", NA), ("YES", YES), ("NO", NO))
_DECIDED = (YES, NO, NA)
# Notes the tool writes on rows it leaves to a reviewer start with the review rule id.
_REVIEW_NOTE_RE = re.compile(r"^(CL-\d+|Human):")


@dataclass
class PreviousChecklist:
    path: Path
    commit: Optional[str]
    rows: Dict[str, Tuple[str, List[str]]] = field(default_factory=dict)
    #: Item ids the previous *tool run* left at REVIEW (from the JSON written next to the
    #: Markdown); None when no JSON is available and the notes must be relied on instead.
    review_then: Optional[Set[str]] = None


def _status_of(cell: str) -> Optional[str]:
    upper = cell.upper()
    for token, status in _STATUS_TOKENS:
        if token in upper:
            return status
    return None


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
    sidecar = json_path or path.with_suffix(".json")
    if sidecar.is_file():
        previous.review_then = _review_rows_from_json(sidecar)
    for line in text.splitlines():
        match = _ROW_RE.match(line)
        if not match:
            continue
        status = _status_of(match.group(3))
        if status is None:
            continue
        notes = [n.strip() for n in re.split(r"<br\s*/?>", match.group(4)) if n.strip()]
        previous.rows[match.group(1)] = (status, notes)
    return previous


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
    if previous.review_then is not None:
        return item_id in previous.review_then
    return any(_REVIEW_NOTE_RE.match(n) for n in notes)


def apply_previous(
    verdicts: List[Verdict],
    previous: PreviousChecklist,
    *,
    changed: Optional[bool],
    skipped: Optional[List[str]] = None,
) -> List[str]:
    """Overwrite REVIEW verdicts with the reviewer's decision from ``previous``; returns carried item ids.

    Decided rows that cannot be recognised as former review rows are appended to ``skipped``."""
    carried: List[str] = []
    reviewed_at = previous.commit[:12] if previous.commit else "an unknown commit"
    for verdict in verdicts:
        if verdict.status != REVIEW:
            continue
        row = previous.rows.get(verdict.item.id)
        if row is None or row[0] not in _DECIDED:
            continue
        status, notes = row
        if not was_review_row(previous, verdict.item.id, notes):
            if skipped is not None:
                skipped.append(verdict.item.id)
            continue
        notes = [n for n in notes if not n.startswith("Carried from ") and not n.startswith("STALE")]
        notes.append(f"Carried from {previous.path.name} (reviewed at {reviewed_at}).")
        if changed is True:
            notes.append(notes.append(f"STALE: the component sources changed since {reviewed_at}; re-review."))
        elif changed is None:
            notes.append("STALE?: could not compare the component sources with the reviewed commit; re-review.")
        verdict.status = status
        verdict.notes = notes
        carried.append(verdict.item.id)
    return carried
