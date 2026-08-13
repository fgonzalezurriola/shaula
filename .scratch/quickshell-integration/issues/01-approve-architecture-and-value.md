# Approve QuickShell architecture and user value

Type: HITL
Status: wontfix

## Parent

`.scratch/quickshell-integration/spec.md`

## What to build

Review the draft QuickShell plan against current QuickShell and Noctalia
behavior, identify a concrete non-Noctalia user journey, and approve, narrow, or
reject the proposed core-service, Noctalia, and standalone tiers before coding.

## Acceptance criteria

- [x] No real non-Noctalia integration journey currently justifies the work.
- [x] The proposal is rejected for the current roadmap before technical expansion.
- [x] Existing Noctalia behavior remains supported without the proposed refactor.
- [x] The draft plan records the decision and the condition for reconsideration.

## Blocked by

None - can start immediately.

## Decision

Deferred on 2026-08-12. The maintainer does not currently use Noctalia and has
no concrete generic QuickShell journey to support. Preserve the existing
Noctalia integration, but do not build the portable service, IPC expansion, or
standalone widget unless a real consumer appears.
