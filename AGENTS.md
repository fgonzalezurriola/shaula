# Shaula Agent Guidance

Our application is called **Shaula**.

## Documentation policy for contributors and agents

- Write all new in-code documentation comments in **English**.
- Add concise doc comments to:
  - large orchestration functions,
  - runtime boundary functions (process execution / IO / protocol mapping),
  - important contract attributes used by QA and release gates.
- Keep comments focused on **why and contract constraints**, not obvious syntax.
- When behavior is taxonomy-sensitive, mention expected deterministic `ERR_*` outcomes.
