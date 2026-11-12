"""Per-section fingerprints of the inputs each checklist section judges.

A reviewer's decision on a REVIEW row is only as current as the artefacts that
section is about, so each section gets its own content hash:

* ``1`` Requirements -- the SDD;
* ``2`` Design -- the SDD and the FPP model;
* ``3`` Implementation -- the non-test C/C++ sources;
* ``4`` Unit testing -- the ``test/ut`` sources;
* ``5`` Close-out -- everything above.

Hashes are over file contents (not git), so they work in any checkout.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
from typing import Dict, Iterable, List

from .checks._sdd import find_sdd
from .checks._sources import impl_sources, is_test_path, test_sources

SECTION_INPUTS = {
    "1": "the SDD",
    "2": "the SDD or FPP model",
    "3": "the implementation sources",
    "4": "the unit-test sources",
    "5": "the component",
}
_DIGEST_CHARS = 12


def fpp_files(module_dir: Path) -> List[Path]:
    out = []
    for pattern in ("*.fpp", "*.fppi"):
        out.extend(p for p in module_dir.rglob(pattern) if not is_test_path(p.relative_to(module_dir)))
    return sorted(out)


def digest(module_dir: Path, paths: Iterable[Path]) -> str:
    h = hashlib.sha256()
    for path in sorted(set(paths)):
        try:
            data = path.read_bytes()
        except OSError:
            continue
        h.update(path.relative_to(module_dir).as_posix().encode())
        h.update(b"\0")
        h.update(data)
        h.update(b"\0")
    return h.hexdigest()[:_DIGEST_CHARS]


def section_inputs(module_dir: Path) -> Dict[str, List[Path]]:
    sdd = find_sdd(module_dir)
    sdd_files = [sdd] if sdd else []
    impl = impl_sources(module_dir)
    tests = test_sources(module_dir)
    model = fpp_files(module_dir)
    return {
        "1": sdd_files,
        "2": sdd_files + model,
        "3": impl,
        "4": tests,
        "5": sdd_files + model + impl + tests,
    }


def section_fingerprints(module_dir: Path) -> Dict[str, str]:
    return {section: digest(module_dir, paths) for section, paths in section_inputs(module_dir).items()}


def format_fingerprints(fps: Dict[str, str]) -> str:
    return " ".join(f"{s}={fps[s]}" for s in sorted(fps))
