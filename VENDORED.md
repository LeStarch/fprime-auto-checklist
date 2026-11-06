# Vendored sources

| Path | Upstream | Commit |
| --- | --- | --- |
| `src/fprime_auto_checklist/queries/fprime-queries/` | nasa/fprime `.github/codeql/fprime-queries` (branch of nasa/fprime#6336) | `8c81541b88dde6cf515c06ef91a1ecc9875f965d` |
| `codeql/fprime-queries-tests/` | nasa/fprime `.github/codeql/fprime-queries-tests` | same |
| `src/fprime_auto_checklist/checks/` | nasa/fprime-actions `component-checks/scripts` + `coverage-common/scripts/{_config,_tiers}.py` (branch of nasa/fprime-actions#55) | `a0baa54b7cc80e97a7c3ff1b6aaf4534d84338fb` |
| `tests/fixtures/Svc/Widget` | nasa/fprime-actions `component-checks/tests/fixtures` | same |

Local changes to the vendored check scripts: absolute imports (`from _report import`)
rewritten to package-relative imports (`from ._report import`); `run_checks.py`
(the multi-module publisher) dropped in favour of `fprime_auto_checklist.runner`.
The CodeQL queries are unmodified.

Refresh with `tools/vendor.sh <fprime-checkout> <fprime-actions-checkout>`.
