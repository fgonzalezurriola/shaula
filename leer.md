1. src/main.zig - Entry point. Muestra todos los comandos disponibles: capture, daemon, preflight, capabilities, history, clipboard, errors
2. src/ipc/protocol.zig - Define el protocolo de comunicación IPC (socket path, mensajes)
3. src/daemon/server.zig - El servidor daemon que corre en background
4. src/daemon/state_machine.zig - Máquina de estados del daemon
5. src/capture/command.zig - Lógica del comando capture
6. src/backends/capture_backend.zig - Backend de captura (base)
7. src/backends/capture_backend_runtime_exec.zig - Backend que ejecuta el proceso
8. src/capture/types.zig - Tipos usados en captura
9. src/capture/command_guards.zig - Precondiciones antes de capturar
10. src/preflight/probe.zig - Chequea capacidades del sistema antes de capturar
