# Shaula Architecture Fixture (Missing Error Taxonomy)

## AGENT-FIRST CLI Contract

### Contract Versioning

- `contract_version` is required in all JSON responses.

### Deterministic Machine-First Output Policy

- In `--json` mode, stdout emits parseable JSON only.
- In `--json` mode, no human-only output is allowed on stdout.

### Command Inventory and Required Flags

- `shaula capture area --json`
- `shaula daemon start --json`
- `shaula capabilities list --json`
- `shaula history list --json`
- `shaula clipboard import-image --json`
