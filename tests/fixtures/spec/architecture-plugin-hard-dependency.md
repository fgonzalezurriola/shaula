# Shaula Architecture Fixture (Plugin Hard Dependency)

## Process Topology

- Daemon is the source of truth for runtime orchestration.
- Overlay and capture backend processes exist.

## IPC Contract v1

- Socket path is `${XDG_RUNTIME_DIR}/shaula/daemon-v1.sock`.
- Every envelope includes `ipc_version`.
- Timeout failures return `ERR_IPC_TIMEOUT`.
- Health checks use `daemon.status`.

## Plugin Optionality Rule

- Noctalia plugin is required for all capture operations.
- Daemon startup depends on Noctalia being available.
- Capture requires Noctalia IPC handshake before proceeding.
