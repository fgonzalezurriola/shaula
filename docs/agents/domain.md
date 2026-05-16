# Domain Docs

How the engineering skills should consume this repo's domain documentation when exploring the codebase.

## Layout

Shaula is a single-context repo.

- Read `CONTEXT.md` at the repo root before diagnosing, refactoring, writing issues, or producing implementation plans.
- Read `docs/adr/` for architectural decisions that touch the area about to be changed.
- If `docs/adr/` has no relevant ADR yet, proceed silently. Do not suggest creating one upfront unless the task resolves a durable architectural decision.

## Use the glossary's vocabulary

When output names a domain concept in an issue title, refactor proposal, hypothesis, or test name, use the term as defined in `CONTEXT.md`. Do not drift to synonyms the context explicitly avoids.

If the concept is missing, treat that as a signal: either reconsider whether the project uses that language, or note the gap for a future `grill-with-docs` pass.

## Flag ADR conflicts

If output contradicts an existing ADR, surface it explicitly rather than silently overriding it.
