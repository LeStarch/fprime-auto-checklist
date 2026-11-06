"""Checklist data and verdict composition."""

import re

from fprime_auto_checklist import checklist, codeql, results
from fprime_auto_checklist.checklist import ITEMS, Evidence, check_ids, codeql_rule_ids


def test_has_73_items_in_five_sections():
    assert len(ITEMS) == 73
    assert len({i.id for i in ITEMS}) == 73
    per_section = {s: sum(1 for i in ITEMS if i.section == s) for s, _ in checklist.SECTIONS}
    assert per_section == {"1": 8, "2": 17, "3": 38, "4": 5, "5": 5}


def test_every_item_has_evidence_and_known_kinds():
    for item in ITEMS:
        assert item.evidence, item.id
        for ev in item.evidence:
            assert ev.kind in ("check", "codeql", "review", "human"), item.id
            if ev.kind == "review" or ev.kind == "human":
                assert ev.ref in checklist.REVIEW_RULES, item.id
            if ev.pattern:
                re.compile(ev.pattern)


def test_codeql_rules_exist_in_vendored_pack_or_stock_pack():
    have = set()
    for ql in codeql.QUERIES_DIR.glob("*.ql"):
        match = re.search(r"@id\s+(\S+)", ql.read_text(encoding="utf-8"))
        assert match, ql
        have.add(match.group(1))
    stock = {"cpp/uninitialized-local"}
    missing = [r for r in codeql_rule_ids() if r not in have | stock]
    assert not missing, missing


def test_check_ids_are_runner_known():
    from fprime_auto_checklist import runner

    runnable = {cid for cid, _ in runner.STATIC_CHECKS} | {"D6", "I2", "I4", "U1", "U4", "U5", "C1", "C3", "SDD"}
    assert set(check_ids()) <= runnable, set(check_ids()) - runnable


def _rec(cid, status, failures=(), detail=""):
    return {"id": cid, "status": status, "failures": list(failures), "detail": detail}


def test_pattern_scopes_composite_check_failures():
    item = checklist.ITEMS_BY_ID["1.4"]
    records = {"R9": _rec("R9", "fail", ["no performance requirement; add one"])}
    assert results.judge_item(item, records, results.CodeqlEvidence(ran=False, reason="x")).status == results.YES
    records = {"R9": _rec("R9", "fail", ["no memory requirement; add one"])}
    v = results.judge_item(item, records, results.CodeqlEvidence(ran=False, reason="x"))
    assert v.status == results.NO and "memory" in v.notes[0]


def test_skip_reason_distinguishes_not_applicable_from_not_run():
    item = checklist.ITEMS_BY_ID["2.9"]
    ev = results.CodeqlEvidence(ran=False, reason="x")
    assert (
        results.judge_item(item, {"D3": _rec("D3", "skip", detail="no state machines declared")}, ev).status
        == results.NA
    )
    assert results.judge_item(item, {}, ev).status == results.UNCHECKED
    item = checklist.ITEMS_BY_ID["2.17"]
    assert (
        results.judge_item(item, {"D6": _rec("D6", "skip", detail="fpp-check not found on PATH")}, ev).status
        == results.UNCHECKED
    )


def test_codeql_item_statuses():
    item = checklist.ITEMS_BY_ID["3.9"]
    rule = "cpp/fprime/float-equality"
    clean = results.CodeqlEvidence(ran=True, findings={rule: []}, rules_run=[rule])
    assert results.judge_item(item, {}, clean).status == results.YES
    dirty = results.CodeqlEvidence(
        ran=True, findings={rule: [results.Finding(rule, "Svc/X/X.cpp", 7, "m")]}, rules_run=[rule]
    )
    v = results.judge_item(item, {}, dirty)
    assert v.status == results.NO and "Svc/X/X.cpp:7" in v.notes[0]
    not_in_run = results.CodeqlEvidence(ran=True, findings={}, rules_run=["cpp/fprime/no-printf"])
    assert results.judge_item(item, {}, not_in_run).status == results.UNCHECKED
    assert results.judge_item(item, {}, results.CodeqlEvidence(ran=False, reason="skipped")).status == results.UNCHECKED


def test_review_items_are_never_yes():
    ev = results.CodeqlEvidence(ran=True)
    for item_id in ("3.21", "3.23", "3.33", "3.38", "4.3"):
        assert results.judge_item(checklist.ITEMS_BY_ID[item_id], {}, ev).status == results.REVIEW


def test_hardware_run_item_follows_record_check():
    item = checklist.ITEMS_BY_ID["5.3"]
    ev = results.CodeqlEvidence(ran=True)
    assert results.judge_item(item, {"C2": _rec("C2", "pass", detail="record")}, ev).status == results.YES
    assert results.judge_item(item, {"C2": _rec("C2", "fail", ["no record"])}, ev).status == results.NO
    assert results.judge_item(item, {}, ev).status == results.REVIEW


def test_notes_are_trimmed():
    item = checklist.ITEMS_BY_ID["3.1"]
    rec = {"I1": _rec("I1", "fail", [f"dep {n}" for n in range(20)])}
    v = results.judge_item(item, rec, results.CodeqlEvidence(ran=False, reason="x"))
    assert len(v.notes) == results.MAX_NOTES + 1 and v.notes[-1].startswith("...")


def test_evidence_helpers():
    assert checklist.codeql("no-printf").ref == "cpp/fprime/no-printf"
    assert checklist.codeql("cpp/uninitialized-local").ref == "cpp/uninitialized-local"
    assert Evidence("check", "R1").pattern is None
