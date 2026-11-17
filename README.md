# fprime-auto-checklist

Pre-fills the **F Prime Component Development Checklist v1.2.0** (73 items) for
one component from automated evidence, and hands the remainder to a human or
LLM reviewer.

* **CodeQL** — the F Prime JPL-standard query pack (`cpp/fprime/*`, vendored
  from [nasa/fprime](https://github.com/nasa/fprime) `.github/codeql/`) plus
  one stock `codeql/cpp-queries` rule decide 31 implementation items.
  Requirement coverage (1.1-1.6) is a reviewer judgement (`REVIEW`, rule CL-6):
  no column or keyword convention is imposed on the requirements table.
* **Check scripts** — the `component-checks` scripts (vendored from
  [nasa/fprime-actions](https://github.com/nasa/fprime-actions)) decide 37
  requirements / design / build / close-out items from the FPP model, the SDD,
  CMake files, build output and GitHub CI.
* **Review** — 5 items no tool can prove (ISR enable ordering, interrupt
  init, units in names, implementation-vs-SDD, boundary tests) are emitted as
  `🔵 REVIEW` with the rule a reviewer should apply.  Running on hardware
  (5.3) is attested by a `docs/hardware-run.md` record that the tool checks.

Where a script covers an item that also has an LLM-quality residue, the
script verdict is taken as sufficient: 1.1 (R9/R1), 2.1 (SDD grade), 2.4
(D10), 3.10 (float rules), and 5.4 "SDD reflects the final state" (D7/D8/D9
consistency with the FPP model plus the structural SDD grade are the objective
proxy for "accurately reflects").

## Install

```bash
python3 -m pip install git+https://github.com/LeStarch/fprime-auto-checklist.git
```

Python ≥ 3.10, **no third-party Python dependencies**.  For the CodeQL items
install the [CodeQL CLI](https://github.com/github/codeql-cli-binaries/releases)
(≥ 2.20) on your `PATH` (or pass `--codeql`); it is *not* bundled — see
[Licensing](#codeql-licensing).  For the FPP/build items activate the
project's `fprime-venv` so `fpp-check`, `fpp-depend` and `fprime-util` are on
`PATH`.

## Use

```bash
cd my-fprime-project
fprime-util generate                      # build cache (locs.fpp) for the FPP checks
fprime-auto-checklist run --module Svc/MyComponent -o MyComponent-checklist.md --json-output results.json
```

`run` will

1. check the project's license (see below), create a CodeQL database by
   tracing `fprime-util build` (or reuse `--codeql-db` / `--sarif`), install
   the vendored pack's dependency (`codeql/cpp-all`, downloaded from the
   public GitHub package registry) and run the checklist rules;
2. run the model/doc/source checks in-process; add build-level checks with
   `--build` (`I2` warnings, `U4` leaks, `U5` coverage, and `C1` with
   `--deployment`), `U1` with `--artifacts-dir`, `C3` when `GITHUB_TOKEN` is
   set (repo/sha default to `origin`/`HEAD`);
3. write the checklist Markdown with every item marked

   | Status | Meaning |
   |---|---|
   | 🟢 YES | verified by a tool (or attested by the checked hardware-run record) |
   | 🟠 NO | a tool found a violation; the Notes column lists each one (file:line) |
   | ⚪ N/A | the model declares nothing the item concerns (e.g. no state machines) |
   | 🔵 REVIEW | needs a human/LLM; Notes carries the review rule |
   | ⚫ UNCHECKED | the deciding tool did not run (no build, no CodeQL, no token, …); Notes says why |

   and a **Hand-off** section listing what is left for the reviewer.

Exit codes: `0` no item is NO; `1` at least one item is NO; `2` usage error;
`3` CodeQL was requested (no `--skip-codeql`) but could not run — license gate
refused, CLI missing, or database/analysis failed — so its items are
UNCHECKED.

Useful variants:

```bash
fprime-auto-checklist run --module Svc/X --skip-codeql           # scripts only
fprime-auto-checklist run --module Svc/X --codeql-db ../db        # reuse a database
fprime-auto-checklist run --module Svc/X --sarif codeql.sarif     # reuse an analysis
fprime-auto-checklist run --module Svc/X --build --deployment Ref -j8
fprime-auto-checklist items                                       # item → evidence map
fprime-auto-checklist license-check --project .                   # CodeQL terms gate only
```

Each check is also a stand-alone CLI with the upstream conventions
(`--module`, `--json-output`, `FAIL:` lines, exit 0/1/2):

```bash
python3 -m fprime_auto_checklist.checks.check_sdd_port_table --module Svc/X
```

CodeQL results in autocoded files (`build-fprime-*/…Ac.cpp`) are set aside and
counted in the header, not charged to the component.

## clang-tidy mode (no CodeQL, no network)

The same `cpp/fprime/*` rules are available as native clang-tidy checks
(`native/`, loaded with `clang-tidy --load`), for projects that cannot use the
CodeQL CLI (closed source without GHAS) or must run fully offline:

```bash
cmake -S native -B native/build -DClang_DIR=/usr/lib/llvm-15/lib/cmake/clang && cmake --build native/build
fprime-util generate -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && fprime-util build
fprime-auto-checklist run --analyzer clang-tidy --module Svc/MyComponent -o MyComponent-checklist.md
```

`--analyzer clang-tidy` runs the plugin over the module's translation units
from `compile_commands.json` (`--compile-commands` to point elsewhere;
`--clang-tidy`/`--tidy-plugin` or `$CLANG_TIDY`/`$FPRIME_TIDY_PLUGIN` to pick
the binaries) and writes `checklist.sarif` with CodeQL's rule ids
(`cpp/fprime/<rule>`), one result per finding and content-based
`partialFingerprints`, so the SARIF can be uploaded to code scanning and
individual alerts dismissed through the GitHub API exactly as today.  Nothing
in this mode contacts the network; wrap it in `unshare -rn`/`--network none`
for belt-and-braces.  The plugin must be loaded by the clang-tidy of the LLVM
major it was built against (15 tested).

Convergence with CodeQL is tested three ways: `tests/test_convergence.py` runs
every vendored CodeQL fixture (`codeql/fprime-queries-tests/*/test.cpp`)
through the matching check and requires the same flagged lines (43/43); the
same test runs the adversarial tier (`codeql/fprime-queries-tests-adversarial/`,
hand-written inputs per check — macros wrapping the construct, templates,
lambdas, explicit casts, labels, `do`/`while`, ternaries, references aliasing
guarded values — whose `.expected` CodeQL itself produced via
`native/test/adversarial.py learn`; 43/43); and
`tools/compare_sarif.py codeql.sarif clang-tidy.sarif --common-rules` diffs
two full runs finding by finding.  Where CodeQL is wrong the port does not
mirror it: each such case is listed in the fixture's `divergence.txt`
(`-line reason` for a CodeQL false positive, `+line reason` for a false
negative) and the oracle applies it, so the CodeQL-generated `.expected` stays
untouched and a stale entry fails the test.  Current divergences:
`[[fallthrough]];` is a fall-through annotation; `#define ONE() 1` is a
function-like macro, not a constant; a call through a const-qualified
function-pointer *variable* is a const call; an `if`/`switch` whose condition
reads a field gates a cyclic event even inside `&&`/`||`; an array-typed
parameter is a pointer that must be checked before use.  On nasa/fprime the two engines agree on
~81% of findings; the residue is CodeQL's global call-graph purity analysis
(`no-boolean-side-effects`), its range analysis (`signed-overflow`),
cross-translation-unit deferred command responses, and CodeQL's reporting of
template-instantiation literals at the template's line (`magic-numbers`).

## CodeQL licensing

The CodeQL CLI is distributed under the
[GitHub CodeQL Terms and Conditions](https://github.com/github/codeql-cli-binaries/blob/main/LICENSE.md).
They allow using the CLI to *"perform analysis on the Open Source Codebase"*
— a codebase released under an OSI-approved license — and, when that codebase
is hosted on GitHub.com, to generate databases in CI/CD.  Analysis of any
other codebase requires a GitHub Advanced Security license, and the CLI may
not be redistributed.  Accordingly:

* this package does **not** bundle or download the CLI;
* `run` refuses to create a database or analyze unless an OSI license file is
  found at `--project`, or the operator attests with
  `--codeql-license-ack open-source` (OSI-licensed code without a detectable
  license file) or `--codeql-license-ack ghas` (covered by GHAS);
* the vendored query pack is Apache-2.0 (nasa/fprime); its dependency
  `codeql/cpp-all` and the stock `codeql/cpp-queries` pack are MIT-licensed
  (GitHub) and are fetched from the public GitHub container registry at
  analysis time (`--download`), pinned by the committed
  `codeql-pack.lock.yml` and `STOCK_QUERIES_VERSION`.

## Development

```bash
python3 -m pip install -e ".[dev]"
ruff check . && python3 -m pytest
codeql pack install codeql/fprime-queries-tests --additional-packs=src/fprime_auto_checklist/queries
codeql test run codeql/fprime-queries-tests --additional-packs=src/fprime_auto_checklist/queries   # query unit tests
tools/vendor.sh ../fprime ../fprime-actions   # refresh vendored sources; then update VENDORED.md
```

To try the tool on the bundled `tests/fixtures/Svc/Widget` component, copy
`tests/fixtures` out of this git checkout first: its `docs/hardware-run.md`
carries a placeholder commit id, which check C2 (5.3) rightly rejects inside a
real repository (the tests do this via `_copy_widget`).

Layout: `src/fprime_auto_checklist/checklist.py` (items + evidence map),
`codeql.py` (license gate, CLI driver, SARIF scoping), `clang_tidy.py`
(plugin driver, SARIF conversion), `native/` (the clang-tidy checks and their
fixture oracle `native/test/run_fixtures.py`), `runner.py` (check
orchestration), `results.py` (verdict composition), `render.py`, `cli.py`;
vendored code under `queries/` and `checks/` (see `VENDORED.md`, `NOTICE`).

## License

Apache-2.0 (see `LICENSE`, `NOTICE`).

## Stage-gated review (`--stage`, `decide`)

The checklist is filled in three passes of the development process, each driven
by a skill in `.github/skills/`:

| Stage | When | Sections due | Skill |
|---|---|---|---|
| `design` | after the SDD and FPP model are approved | 1 Requirements, 2 Design | `fprime-checklist-design` |
| `implementation` | after the code and unit tests | 1–4 | `fprime-checklist-implementation` |
| `closeout` | after system testing / run-for-record | all, plus an audit of every tool-decided row | `fprime-checklist-closeout` |

```bash
fprime-auto-checklist run --module Svc/X --analyzer clang-tidy --stage design -o checklist.md
fprime-auto-checklist decide --checklist checklist.md --item 1.4 --status YES --note "REQ-003 bounds the buffer."
fprime-auto-checklist run --module Svc/X --analyzer clang-tidy --stage implementation --previous checklist.md -o checklist.md
```

`run --stage` marks REVIEW rows of later sections *deferred* and writes the
**review queue** — the rows to decide now — to the Hand-off section: undecided
REVIEW rows of the due sections, carried decisions whose section inputs changed
(`stale`), and at close-out the tool's NO / UNCHECKED rows (`read`: open the
finding, override only when the tool is wrong — a tool YES is accepted as is).

**The Markdown is the only record.** Every Status cell carries its decision
stamp: `🟢 YES @508b1a3a39b1` (decided by the tool at that commit),
`🟢 YES @508b1a3a39b1 by review` (a row the tool left at REVIEW, decided by the
reviewer) or `🟠 NO @508b1a3a39b1 overridden by reviewer` (the reviewer replaced
the tool's verdict). `decide` rewrites the table from the Markdown — it is never
hand-edited — and a justification goes in Notes only where a reader could not
otherwise tell why: an override, or a reviewer's NO / N/A (one clause). A
reviewer's YES carries no note; a tool NO keeps the tool's own finding. On the
next `run --previous`, `by review` rows carry (flagged `STALE:` and re-queued
when the inputs their section judges changed since the stamped commit) and an
override holds while those inputs are unchanged and the tool still disagrees —
otherwise the tool's verdict stands again. `--from decisions.json` records a
batch (`[{"id", "status", "notes": [..]}]`). The evidence behind every tool
row (check records, SARIF) is written to the work dir (`checks.json`, or
`--json-output`), not beside the checklist; a pre-0.2 `checklist.json` sidecar
beside `--previous` is still read once to migrate its decisions
(`--previous-json`).

The checker performs exactly one build itself: `fprime-util generate` (when no
`compile_commands.json` exists) and `fprime-util build` of the module, which
the clang-tidy analysis needs; that build's output answers I2 (3.2). Every
other performed step — unit-test run, coverage, leak check, topology build,
hardware run, CI — is read from what the development process already produced
(`coverage/summary.txt`, `docs/hardware-run.md`, the CI status) and is left
`UNCHECKED "not done yet"` otherwise; the tool never runs them unless
`--build` is given explicitly. `--no-prepare-build` disables the one build.

## Carrying reviewer decisions forward

`run --previous <earlier-checklist.md>` reads an earlier rendering of the same
checklist. Rows stamped `by review` keep the reviewer's status and notes; rows
stamped `overridden by reviewer` keep the override while the tool still
disagrees; tool rows are always recomputed. Staleness is judged per section —
section 1 (requirements) the SDD, section 2 (design) the SDD and FPP model,
section 3 (implementation) the non-test sources, section 4 (unit testing) the
`test/ut` sources — from the per-section fingerprints in the rendered header
when both sides have them, else from a `git diff` of that section's files
between the stamped commit and HEAD. A stale review row is flagged `STALE:` and
re-queued; a stale override is dropped (the tool's verdict stands until the
reviewer overrides again).

## External References table (item 1.8)

Item 1.8 asks whether external interface or functional requirements live in
other documents (ICDs, datasheets, FDDs).  The SDD states this in a table:

```markdown
## External References
| Document | Identifier / Revision | Type | Governs |
|---|---|---|---|
| Infineon CY15B102QN datasheet | 002-26764 Rev. *D | Datasheet | command set, write-enable sequence |
```

`Type` is Datasheet / ICD / FDD / Standard / Other; a single `None` row is the
explicit waiver.  D9 copies the rows into the 1.8 note.  An SDD that only cites
a document inline (`datasheet 002-26764 Rev. *D`) still passes, with the note
"no External References table; cited inline"; an SDD with neither fails.
