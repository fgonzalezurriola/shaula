# Contributing to Shaula

## Políticas de Evidencia

Toda tarea ejecutada por un agente debe producir artefactos de evidencia en la siguiente ruta:

`.sisyphus/evidence/`

Los archivos deben seguir la convención `task-[ID]-[DESCRIPTION].txt` o `task-[ID]-[DESCRIPTION]-error.txt` según corresponda.

## Validación de Estructura

Antes de realizar cambios, se debe ejecutar el script de validación de estructura:

```bash
bash scripts/qa/preflight-repo-structure.sh
```

Este script asegura que todos los directorios fundamentales están presentes.
