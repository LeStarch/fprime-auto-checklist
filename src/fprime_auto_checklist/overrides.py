"""Human or LLM overrides of checklist verdicts, read from the component's ``docs/`` folder.

``<module>/docs/checklist-overrides.md`` carries a Markdown table; a row with a
status overrides that item's tool verdict, a row with an empty status leaves it
alone (so the file can be a complete template).  The tool's own verdict and
notes are kept beneath the override, and the final status says who set it.

    | # | Status | By | Date | Justification |
    |---|--------|----|------|---------------|
    | 3.21 | YES | J. Doe | 2026-11-07 | No ISRs: component is a passive queued component. |
    | 3.33 | | | | |

Status is YES, NO or N/A.  ``By`` names the person or LLM (e.g. ``llm:claude``),
``Date`` is YYYY-MM-DD, ``Justification`` is mandatory.  A malformed row is an
error, never silently ignored.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional

from .checklist import ITEMS
from .results import MAX_NOTES, NA, NO, STATUS_LABELS, YES, Verdict

OVERRIDE_FILES = ("checklist-overrides.md", "checklist_overrides.md")
_STATUS_WORDS = {"yes": YES, "no": NO, "n/a": NA, "na": NA, "not applicable": NA}
_COLUMNS = {
    "item": ("#", "item", "id"),
    "status": ("status",),
    "by": ("by", "reviewer", "who"),
    "date": ("date",),
    "justification": ("justification", "notes", "reason", "rationale"),
}
_DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
_ITEM_IDS = {item.id for item in ITEMS}


class OverrideError(ValueError):
    """The overrides file is malformed; every problem is listed in the message."""


@dataclass(frozen=True)
class Override:
    item_id: str
    status: str
    by: str
    date: str
    justification: str
    source: str  # file:line


def find_overrides(module: Path) -> Optional[Path]:
    for name in OVERRIDE_FILES:
        path = module / "docs" / name
        if path.is_file():
            return path
    return None


def _cells(line: str) -> List[str]:
    inner = line.strip()
    if inner.startswith("|"):
        inner = inner[1:]
    if inner.endswith("|"):
        inner = inner[:-1]
    return [c.strip() for c in re.split(r"(?<!\\)\|", inner)]


def _columns(header: List[str]) -> Optional[Dict[str, int]]:
    found: Dict[str, int] = {}
    for index, cell in enumerate(header):
        name = cell.strip("*` ").lower()
        for key, aliases in _COLUMNS.items():
            if name in aliases and key not in found:
                found[key] = index
    return found if {"item", "status"} <= set(found) else None


class _CellReader:
    def __init__(self, cells: List[str], columns: Dict[str, int]):
        self.cells, self.columns = cells, columns

    def __call__(self, key: str) -> str:
        index = self.columns.get(key)
        return self.cells[index] if index is not None and index < len(self.cells) else ""


def _clean_status(cell: str) -> Optional[str]:
    word = re.sub(r"[^\w/ ]", " ", cell).strip().lower()  # drop emoji / markup
    return _STATUS_WORDS.get(word) if word else ""


def parse_overrides(text: str, source: str = "checklist-overrides.md") -> Dict[str, Override]:
    """Parse the overrides table; raise OverrideError listing every malformed row."""
    overrides: Dict[str, Override] = {}
    problems: List[str] = []
    columns: Optional[Dict[str, int]] = None
    for number, line in enumerate(text.splitlines(), start=1):
        if not line.lstrip().startswith("|"):
            columns = None
            continue
        cells = _cells(line)
        if columns is None:
            columns = _columns(cells)
            continue
        if all(re.fullmatch(r":?-{3,}:?", c) for c in cells if c):
            continue
        where = f"{source}:{number}"
        cell = _CellReader(cells, columns)
        item_id = cell("item").strip("*` ")
        status = _clean_status(cell("status"))
        if status == "":
            continue  # unset: the tool verdict stands
        if item_id not in _ITEM_IDS:
            problems.append(f"{where}: unknown checklist item {item_id!r}")
            continue
        if status is None:
            problems.append(f"{where}: item {item_id}: status must be YES, NO or N/A, not {cell('status')!r}")
            continue
        by, date, justification = cell("by"), cell("date"), cell("justification")
        if not by:
            problems.append(f"{where}: item {item_id}: 'By' (person or LLM) is required")
        if date and not _DATE_RE.match(date):
            problems.append(f"{where}: item {item_id}: date must be YYYY-MM-DD, not {date!r}")
        if not justification:
            problems.append(f"{where}: item {item_id}: a justification is required")
        if item_id in overrides:
            problems.append(f"{where}: item {item_id} overridden twice (first at {overrides[item_id].source})")
        if problems and problems[-1].startswith(where):
            continue
        overrides[item_id] = Override(item_id, status, by, date, justification, where)
    if problems:
        raise OverrideError("overrides file rejected:\n  " + "\n  ".join(problems))
    return overrides


def apply_overrides(verdicts: List[Verdict], overrides: Dict[str, Override]) -> List[Verdict]:
    """Replace the tool verdict of every overridden item, keeping the tool's verdict in the notes."""
    result = []
    for v in verdicts:
        o = overrides.get(v.item.id)
        if o is None:
            result.append(v)
            continue
        when = f" on {o.date}" if o.date else ""
        notes = [f"Override by {o.by}{when}: {o.justification}", f"Tool verdict: {STATUS_LABELS[v.status]}"]
        notes += v.notes[: MAX_NOTES - len(notes)]
        result.append(Verdict(v.item, o.status, notes, tool_status=v.status, override=o))
    return result
