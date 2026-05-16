# Shaula

This app doesn't have any users yet, don't make regression tests

## Documentation policy for contributors and agents

- Write all new in-code documentation comments in **English**.
- Add concise doc comments to:
  - large orchestration functions,
  - runtime boundary functions (process execution / IO / protocol mapping),
  - important contract attributes used by QA and release gates.
- Keep comments focused on **why and contract constraints**, not obvious syntax.
- When behavior is taxonomy-sensitive, mention expected deterministic `ERR_*` outcomes.

## Verification after changes

Run these after every code change:

```bash
./dev check
git diff --check
```

For overlay, capture, clipboard, GTK, Wayland, or Niri behavior changes, run a relevant targeted manual check through `./dev run ...` when the changed behavior cannot be covered by `./dev check`.

For interactive overlay UX changes, ask the user to run:

```bash
./dev capture
./dev all
```

## Updates after changes

After making changes, update CONTEXT.md, add relevant and compact context to upload to chatgpt.com (my prompter).

## Agent skills

### Issue tracker

Issues and PRDs are tracked in GitHub Issues for `fgonzalezurriola/shaula` using the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Use the default canonical triage label vocabulary for agent workflows. See `docs/agents/triage-labels.md`.

### Domain docs

This is a single-context repo: read root `CONTEXT.md` and `docs/adr/` when relevant. See `docs/agents/domain.md`.
