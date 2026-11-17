"""Render verdicts as the checklist Markdown — the record — and the evidence JSON kept in the work dir."""

from __future__ import annotations

import datetime as dt
import json
from typing import Dict, List, Optional

from . import __version__
from .checklist import CHECKLIST_VERSION, SECTIONS
from .fingerprint import format_fingerprints
from .results import NA, NO, REVIEW, STATUS_LABELS, UNCHECKED, YES, CodeqlEvidence, Verdict, status_cell, summarize, verdicts_to_json
from .stages import STAGES, Decision, QueueEntry, rule_text

LEGEND = (
    (YES, "Item satisfied -- verified by a tool (or attested by the checked record)."),
    (NO, "Not satisfied -- a tool found a violation; see Notes."),
    (NA, "Not applicable -- the model declares nothing this item concerns."),
    (REVIEW, "Needs a human or LLM judgement; the rule to apply is in Notes."),
    (UNCHECKED, "The deciding tool did not run (missing input, tool or license ack); see Notes."),
)


def _strip(text: str, prefix: str) -> str:
    return text.replace(prefix.rstrip("/") + "/", "") if prefix else text


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
    strip_prefix: str = "",
    fingerprints: Optional[Dict[str, str]] = None,
    stage: Optional[str] = None,
    queue: Optional[List[QueueEntry]] = None,
    codeql_line: Optional[str] = None,
) -> str:
    """Render the checklist; ``strip_prefix`` (the project root) is removed from paths in notes.

    ``stage`` names the development stage the run was made for and ``queue`` the rows the
    reviewer must decide at that stage (rendered in the Hand-off section)."""
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
    ]
    if fingerprints:
        lines.append(f"_Section fingerprints (inputs each section judges): {format_fingerprints(fingerprints)}_")
    if stage:
        lines.append(
            f"_Stage: **{stage}** ({STAGES[stage]['title']}); sections due: {', '.join(STAGES[stage]['due'])}_"
        )
    lines += [
        "",
        "## Status Legend",
        "",
        "| Status | Meaning |",
        "|--------|---------|",
    ]
    lines += [f"| {STATUS_LABELS[s]} | {meaning} |" for s, meaning in LEGEND]
    lines += [
        "",
        f"**Note:** Any status other than **{STATUS_LABELS[YES]}** requires an explanation in the **Notes** column. "
        "A Status cell reads `<status> @<commit>`: decided by the tool at that commit; `by review` when a reviewer decided a "
        "row the tool left open; `overridden by reviewer` when a reviewer replaced the tool's verdict (reason in Notes).",
        "",
        "## Definitions",
        "",
        "| Term | Definition |",
        "|------|------------|",
        "| SDD | Software Description Document \u2014 the design documentation for the component. |",
        "| Block diagram | A diagram illustrating the component's ports (and their directions/types), "
        "used to visualize the component's interfaces. |",
    ]
    if codeql_line is not None:
        lines += ["", codeql_line]
    elif codeql.ran:
        lines += [
            "",
            f"_{codeql.tool}: {len(codeql.rules_run)} rules run; {codeql.autocode_findings} finding(s) in autocoded "
            "files were set aside (autocoder, not component)._",
        ]
    else:
        lines += ["", f"_{codeql.tool} did not run: {codeql.reason}_"]

    for section_id, title in SECTIONS:
        lines += ["", f"## {section_id}. {title}", "", "| # | Item | Status | Notes |", "|---|------|--------|-------|"]
        for v in verdicts:
            if v.item.section != section_id:
                continue
            notes = "<br>".join(_cell(_strip(n, strip_prefix)) for n in v.notes)
            lines.append(f"| {v.item.id} | {_cell(v.item.text)} | {status_cell(v, commit)} | {notes} |")

    if queue is not None:
        lines += ["", "## Hand-off", ""]
        if queue:
            lines.append(
                f"Review queue for the **{stage or 'closeout'}** stage ({len(queue)} row(s)); record each with "
                "`fprime-auto-checklist decide`:"
            )
            lines.append("")
            for entry in queue:
                why = {"review": "review", "stale": "re-review (inputs changed)",
                       "read": "read the tool's finding; override only if it is wrong"}[entry.reason]
                rule = f" [{entry.rule}]" if entry.rule else ""
                lines.append(f"- **{entry.id}** ({why}{rule}) \u2014 {STATUS_LABELS[entry.status]}")
        else:
            lines.append(f"Nothing to review at the **{stage or 'closeout'}** stage.")
        deferred = [v for v in verdicts if v.status == REVIEW and v.item.id not in {e.id for e in queue}]
        if deferred:
            lines.append("")
            lines.append("Deferred to a later stage: " + ", ".join(v.item.id for v in deferred) + ".")
        return "\n".join(lines) + "\n"
    pending = [v for v in verdicts if v.status == REVIEW]
    if pending:
        lines += ["", "## Hand-off", "", "Items left for a human or LLM reviewer:", ""]
        lines += [f"- **{v.item.id}** \u2014 {v.notes[0] if v.notes else ''}" for v in pending]
    return "\n".join(lines) + "\n"


def render_json(
    verdicts: List[Verdict],
    records: Dict[str, dict],
    codeql: CodeqlEvidence,
    *,
    tool_verdicts: Optional[List[Verdict]] = None,
    decisions: Optional[Dict[str, Decision]] = None,
    queue: Optional[List[QueueEntry]] = None,
    **meta,
) -> str:
    """``tool_verdicts`` are the verdicts before any reviewer decision was applied (kept so
    ``decide`` can re-derive the rendering); ``decisions`` are the recorded reviewer decisions."""
    items = verdicts_to_json(verdicts)
    tool_by_id = {v.item.id: v for v in (tool_verdicts or verdicts)}
    for item in items:
        tool = tool_by_id.get(item["id"])
        if tool is not None:
            item["tool_status"] = tool.status
            item["tool_notes"] = list(tool.notes)
        decision = (decisions or {}).get(item["id"])
        if decision is not None:
            item["decision"] = decision.to_json()
    payload = {
        "schema_version": 2,
        "tool": {"name": "fprime-auto-checklist", "version": __version__},
        "checklist_version": CHECKLIST_VERSION,
        **meta,
        "summary": summarize(verdicts),
        "items": items,
        "queue": [dict(e.to_json(), rule_text=rule_text(e.rule)) for e in (queue or [])],
        "checks": records,
        "codeql": {
            "ran": codeql.ran,
            "tool": codeql.tool,
            "reason": codeql.reason,
            "rules_run": codeql.rules_run,
            "autocode_findings": codeql.autocode_findings,
            "findings": {rule: [vars(f) for f in fs] for rule, fs in codeql.findings.items()},
        },
    }
    return json.dumps(payload, indent=2, sort_keys=False) + "\n"
