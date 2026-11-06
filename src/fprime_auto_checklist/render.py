"""Render verdicts as the checklist Markdown (and JSON)."""

from __future__ import annotations

import datetime as dt
import json
from typing import Dict, List, Optional

from . import __version__
from .checklist import CHECKLIST_VERSION, SECTIONS
from .results import NA, NO, REVIEW, STATUS_LABELS, UNCHECKED, YES, CodeqlEvidence, Verdict, summarize, verdicts_to_json

LEGEND = (
    (YES, "Item satisfied -- verified by a tool (or attested by the checked record)."),
    (NO, "Not satisfied -- a tool found a violation; see Notes."),
    (NA, "Not applicable -- the model declares nothing this item concerns."),
    (REVIEW, "Needs a human or LLM judgement; the rule to apply is in Notes."),
    (UNCHECKED, "The deciding tool did not run (missing input, tool or license ack); see Notes."),
)


def _cell(text: str) -> str:
    return text.replace("|", "\\|").replace("\n", " ")


def render_markdown(
    verdicts: List[Verdict],
    *,
    component: str,
    module_rel: str,
    commit: Optional[str],
    codeql: CodeqlEvidence,
    owner: str = "",
    contributors: str = "",
    now: Optional[dt.datetime] = None,
) -> str:
    now = now or dt.datetime.now(dt.timezone.utc)
    counts = summarize(verdicts)
    lines = [
        "# F\u00b4 (F Prime) Component Development Checklist",
        "",
        f"**Component Name:** {component}  ",
        f"**Component Owner:** {owner or '_[Owner Name]_'}  ",
        f"**Component Contributors:** {contributors or '_[Contributor Names]_'}  ",
        f"**Review Date:** {now.strftime('%Y-%m-%d')}  ",
        f"**Checklist Version:** {CHECKLIST_VERSION}",
        "",
        f"_Pre-filled by fprime-auto-checklist {__version__} for `{module_rel}`"
        + (f" at `{commit[:12]}`" if commit else "")
        + ". "
        + ", ".join(f"{STATUS_LABELS[s]} {n}" for s, n in counts.items())
        + "._",
        "",
        "## Status Legend",
        "",
        "| Status | Meaning |",
        "|--------|---------|",
    ]
    lines += [f"| {STATUS_LABELS[s]} | {meaning} |" for s, meaning in LEGEND]
    lines += [
        "",
        f"**Note:** Any status other than **{STATUS_LABELS[YES]}** requires an explanation in the **Notes** column.",
        "",
        "## Definitions",
        "",
        "| Term | Definition |",
        "|------|------------|",
        "| SDD | Software Description Document \u2014 the design documentation for the component. |",
        "| Block diagram | A diagram illustrating the component's ports (and their directions/types), "
        "used to visualize the component's interfaces. |",
    ]
    if codeql.ran:
        lines += [
            "",
            f"_CodeQL: {len(codeql.rules_run)} rules run; {codeql.autocode_findings} finding(s) in autocoded "
            "files were set aside (autocoder, not component)._",
        ]
    else:
        lines += ["", f"_CodeQL did not run: {codeql.reason}_"]

    for section_id, title in SECTIONS:
        lines += ["", f"## {section_id}. {title}", "", "| # | Item | Status | Notes |", "|---|------|--------|-------|"]
        for v in verdicts:
            if v.item.section != section_id:
                continue
            notes = "<br>".join(_cell(n) for n in v.notes)
            lines.append(f"| {v.item.id} | {_cell(v.item.text)} | {STATUS_LABELS[v.status]} | {notes} |")

    pending = [v for v in verdicts if v.status == REVIEW]
    if pending:
        lines += ["", "## Hand-off", "", "Items left for a human or LLM reviewer:", ""]
        lines += [f"- **{v.item.id}** \u2014 {v.notes[0] if v.notes else ''}" for v in pending]
    return "\n".join(lines) + "\n"


def render_json(verdicts: List[Verdict], records: Dict[str, dict], codeql: CodeqlEvidence, **meta) -> str:
    payload = {
        "schema_version": 1,
        "tool": {"name": "fprime-auto-checklist", "version": __version__},
        "checklist_version": CHECKLIST_VERSION,
        **meta,
        "summary": summarize(verdicts),
        "items": verdicts_to_json(verdicts),
        "checks": records,
        "codeql": {
            "ran": codeql.ran,
            "reason": codeql.reason,
            "rules_run": codeql.rules_run,
            "autocode_findings": codeql.autocode_findings,
            "findings": {rule: [vars(f) for f in fs] for rule, fs in codeql.findings.items()},
        },
    }
    return json.dumps(payload, indent=2, sort_keys=False) + "\n"
