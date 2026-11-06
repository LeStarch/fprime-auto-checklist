# fprime-auto-checklist

Pre-fills the **F Prime Component Development Checklist v1.2.0** (73 items) for
one component from automated evidence, and hands the remainder to a human or
LLM reviewer.

* **CodeQL** — the F Prime JPL-standard query pack (`cpp/fprime/*`, vendored
  from [nasa/fprime](https://github.com/nasa/fprime) `.github/codeql/`) plus
  one stock `codeql/cpp-queries` rule decide 31 implementation items.
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

Python ≥ 3.9, **no third-party Python dependencies**.  For the CodeQL items
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
`codeql.py` (license gate, CLI driver, SARIF scoping), `runner.py` (check
orchestration), `results.py` (verdict composition), `render.py`, `cli.py`;
vendored code under `queries/` and `checks/` (see `VENDORED.md`, `NOTICE`).

## License

Apache-2.0 (see `LICENSE`, `NOTICE`).
