from pathlib import Path

from fprime_auto_checklist.checks._fpp import load_module_model


def test_events_without_arguments_are_declared(tmp_path: Path) -> None:
    (tmp_path / "Cam.fpp").write_text(
        "active component Cam {\n"
        "    event port eventOut\n"
        '    event SetupStarted severity activity low format "started"\n'
        '    event SetupFailed(step: U8) severity warning high format "failed {}"\n'
        "}\n"
    )
    assert load_module_model(tmp_path).events == ["SetupStarted", "SetupFailed"]
