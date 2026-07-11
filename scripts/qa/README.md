# Shaula QA Scripts

This directory contains current contract checks plus older task/evidence
scripts. Do not treat every executable here as a release gate.

## Required After Code Changes

The repository baseline is:

```bash
./dev check
git diff --check
```

For overlay, capture, clipboard, GTK, Wayland, or Niri behavior changes, add a
targeted manual check through `./dev run ...` when the baseline cannot cover the
changed runtime behavior.

## Current Non-Intrusive QA

`./dev qa` is the only curated QA wrapper. It runs:

- `scripts/qa/run-all-tests.sh`
- `scripts/qa/run-unit-tests.sh`
- `scripts/qa/assert-preflight-schema.sh`
- `scripts/qa/test-failure-matrix.sh`
- `scripts/qa/assert-exit-code-mapping.sh`

This lane is intentionally small. It validates deterministic CLI contracts and
negative preflight behavior without relying on a real compositor session. Niri
on CachyOS is the primary development environment. Negative compositor checks
use the explicit `x11` token; Sway is supported wlroots coverage through `grim`
and is not a valid unsupported-compositor fixture.

## Targeted Maintained Checks

These can be useful when touching the named contract, but they are not part of
the default gate unless explicitly invoked:

- `./dev check` validates the Meson build and test suite, including
  `assert-port-command-compatibility.sh` with fake capture, Niri, clipboard, and
  notification processes.
- `scripts/qa/assert-default-output-path.sh`
- `scripts/qa/assert-overlay-geometry-fixtures.sh`
- `scripts/qa/assert-no-runtime-stub.sh`
- `scripts/qa/assert-capture-content-validity.sh`
- `scripts/qa/assert-history-topn.sh`
- `scripts/qa/assert-capabilities-consistency.sh`

## Manual / Legacy Investigation

These wrappers aggregate older task scripts, evidence reports, benchmarks, real
Niri/Wayland assumptions, Noctalia behavior, or intrusive UI lanes. They remain
available for investigation, but can encode stale product assumptions and should
not be cited as release readiness without refreshing the failing subchecks:

- `scripts/qa/run-integration-tests.sh`
- `scripts/qa/run-e2e-niri.sh`
- `scripts/qa/run-performance-gates.sh`
- `scripts/qa/release-readiness-capture-fix.sh`
- `scripts/qa/benchmark-capture-completion.sh`
- `scripts/qa/benchmark-overlay-first-paint.sh`
- `scripts/qa/assert-noctalia-*.sh`
- `scripts/qa/test-noctalia-plugin-optional.sh`
- `scripts/qa/assert-overlay-helper-interactive.sh`

When updating one of these scripts, first decide whether it should become part
of `./dev qa`, stay targeted/manual, or stay legacy. Then update this file and
`CONTEXT.md` together.
