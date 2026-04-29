# Shaula Agent Guidance

This app doesn't have any users yet, don't make regression tests
You can make non-smoke tests and use my screen, i will make the screenshot and you verify the result

application is called **Shaula**.
Use always the zig related skill.

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

For overlay, capture, clipboard, GTK, Wayland, or Niri behavior changes, also run the relevant targeted command:

```bash
./dev doctor
./dev strategies
./dev bench
```

For interactive overlay UX changes, ask the user to run or allow:

```bash
./dev capture
./dev all
```
