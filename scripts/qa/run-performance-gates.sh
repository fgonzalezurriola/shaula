#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

OVERLAY_SAMPLES=100
OVERLAY_WARMUP=10
OVERLAY_P95_MAX=75
OVERLAY_P99_MAX=110

CAPTURE_SAMPLES=60
CAPTURE_WARMUP=8
CAPTURE_AREA_P95_MAX=150
CAPTURE_WINDOW_P95_MAX=220

DAEMON_SAMPLES=30
DAEMON_WARMUP=4
DAEMON_CPU_MAX=0.5
DAEMON_RSS_MAX_MB=40
DAEMON_INTERVAL_SEC=0.2

while [[ $# -gt 0 ]]; do
  case "$1" in
    --overlay-samples) OVERLAY_SAMPLES="$2"; shift 2 ;;
    --overlay-warmup) OVERLAY_WARMUP="$2"; shift 2 ;;
    --p95-max-ms|--overlay-p95-max-ms) OVERLAY_P95_MAX="$2"; shift 2 ;;
    --p99-max-ms|--overlay-p99-max-ms) OVERLAY_P99_MAX="$2"; shift 2 ;;

    --capture-samples) CAPTURE_SAMPLES="$2"; shift 2 ;;
    --capture-warmup) CAPTURE_WARMUP="$2"; shift 2 ;;
    --area-p95-max-ms) CAPTURE_AREA_P95_MAX="$2"; shift 2 ;;
    --window-p95-max-ms) CAPTURE_WINDOW_P95_MAX="$2"; shift 2 ;;

    --daemon-samples) DAEMON_SAMPLES="$2"; shift 2 ;;
    --daemon-warmup) DAEMON_WARMUP="$2"; shift 2 ;;
    --cpu-max) DAEMON_CPU_MAX="$2"; shift 2 ;;
    --rss-max-mb) DAEMON_RSS_MAX_MB="$2"; shift 2 ;;
    --daemon-interval-sec) DAEMON_INTERVAL_SEC="$2"; shift 2 ;;

    *) echo "ERR_PERF_USAGE script=run-performance-gates unknown_flag=$1" >&2; exit 1 ;;
  esac
done

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

overlay_json="$(bash ./scripts/qa/benchmark-overlay-first-paint.sh --samples "${OVERLAY_SAMPLES}" --warmup "${OVERLAY_WARMUP}" --p95-max-ms "${OVERLAY_P95_MAX}" --p99-max-ms "${OVERLAY_P99_MAX}" --json-only)"
capture_json="$(bash ./scripts/qa/benchmark-capture-completion.sh --samples "${CAPTURE_SAMPLES}" --warmup "${CAPTURE_WARMUP}" --area-p95-max-ms "${CAPTURE_AREA_P95_MAX}" --window-p95-max-ms "${CAPTURE_WINDOW_P95_MAX}" --json-only)"
daemon_json="$(bash ./scripts/qa/benchmark-daemon-idle.sh --samples "${DAEMON_SAMPLES}" --warmup "${DAEMON_WARMUP}" --cpu-max "${DAEMON_CPU_MAX}" --rss-max-mb "${DAEMON_RSS_MAX_MB}" --interval-sec "${DAEMON_INTERVAL_SEC}" --json-only)"

jq -n \
  --argjson overlay "${overlay_json}" \
  --argjson capture "${capture_json}" \
  --argjson daemon "${daemon_json}" \
  '{
    suite: "task-11-performance-gates",
    pass: ($overlay.pass and $capture.pass and $daemon.pass),
    benchmarks: {
      overlay_first_paint: $overlay,
      capture_completion: $capture,
      daemon_idle_footprint: $daemon
    }
  }'
