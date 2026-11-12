"""Per-section fingerprints and section-scoped staleness."""

from fprime_auto_checklist import carry, fingerprint, results


def _module(tmp_path):
    m = tmp_path / "Comp"
    (m / "docs").mkdir(parents=True)
    (m / "test" / "ut").mkdir(parents=True)
    (m / "docs" / "sdd.md").write_text("# SDD\n")
    (m / "Comp.fpp").write_text("module M {}\n")
    (m / "Comp.cpp").write_text("int x;\n")
    (m / "Comp.hpp").write_text("int y;\n")
    (m / "test" / "ut" / "CompTester.cpp").write_text("void t() {}\n")
    return m


def _changed(before, after):
    return {s for s in before if before[s] != after[s]}


def test_sections_only_change_with_their_own_inputs(tmp_path):
    m = _module(tmp_path)
    base = fingerprint.section_fingerprints(m)
    assert set(base) == {"1", "2", "3", "4", "5"} and all(len(v) == 12 for v in base.values())
    (m / "test" / "ut" / "CompTester.cpp").write_text("void t() { int z; }\n")
    after_ut = fingerprint.section_fingerprints(m)
    assert _changed(base, after_ut) == {"4", "5"}
    (m / "Comp.cpp").write_text("int x = 1;\n")
    after_impl = fingerprint.section_fingerprints(m)
    assert _changed(after_ut, after_impl) == {"3", "5"}
    (m / "Comp.fpp").write_text("module M { port P }\n")
    after_fpp = fingerprint.section_fingerprints(m)
    assert _changed(after_impl, after_fpp) == {"2", "5"}
    (m / "docs" / "sdd.md").write_text("# SDD v2\n")
    after_sdd = fingerprint.section_fingerprints(m)
    assert _changed(after_fpp, after_sdd) == {"1", "2", "5"}


def test_carried_rows_are_stale_only_when_their_section_changed(tmp_path):
    m = _module(tmp_path)
    before = fingerprint.section_fingerprints(m)
    md = tmp_path / "prev.md"
    md.write_text(
        "_Pre-filled by fprime-auto-checklist 0.1.0 for `Comp` at `0123456789ab`. x_\n"
        f"_Section fingerprints (inputs each section judges): {fingerprint.format_fingerprints(before)}_\n\n"
        "| # | Item | Status | Notes |\n|---|---|---|---|\n"
        "| 1.4 | memory | YES | CL-6: reviewed |\n"
        "| 3.33 | units | YES | CL-3: reviewed |\n"
        "| 4.3 | boundaries | NO | CL-5: reviewed |\n"
    )
    previous = carry.parse_previous(md)
    assert previous.fingerprints == before
    (m / "Comp.cpp").write_text("int x = 2;\n")  # implementation changed, nothing else
    now = fingerprint.section_fingerprints(m)
    rerun = results.judge_all({}, results.CodeqlEvidence(ran=False, reason="skipped"))
    carried = carry.apply_previous(rerun, previous, changed=True, fingerprints=now)
    rows = {v.item.id: v for v in rerun}
    assert carried == ["1.4", "3.33", "4.3"]
    assert not any(n.startswith("STALE") for n in rows["1.4"].notes)  # SDD unchanged
    assert any(n.startswith("STALE: the implementation sources changed") for n in rows["3.33"].notes)
    assert not any(n.startswith("STALE") for n in rows["4.3"].notes)  # UT sources unchanged


def test_without_fingerprints_falls_back_to_whole_module_comparison(tmp_path):
    md = tmp_path / "prev.md"
    md.write_text("| # | Item | Status | Notes |\n|---|---|---|---|\n| 1.4 | memory | YES | CL-6: reviewed |\n")
    rerun = results.judge_all({}, results.CodeqlEvidence(ran=False, reason="skipped"))
    carry.apply_previous(rerun, carry.parse_previous(md), changed=True, fingerprints={"1": "abc"})
    assert any(n.startswith("STALE: the SDD changed") for n in {v.item.id: v for v in rerun}["1.4"].notes)
