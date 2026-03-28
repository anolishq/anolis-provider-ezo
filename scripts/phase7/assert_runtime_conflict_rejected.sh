#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  assert_runtime_conflict_rejected.sh --runtime-cmd <path> --config <path> [--log-file <path>]

Checks that runtime startup fails with ownership-conflict diagnostics.

Options:
  --runtime-cmd PATH   Path to anolis-runtime executable
  --config PATH        Runtime config expected to trigger ownership conflict
  --log-file PATH      Optional output log path (default: temp file)
  -h, --help           Show help
USAGE
}

RUNTIME_CMD=""
CONFIG_PATH=""
LOG_FILE=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --runtime-cmd)
      RUNTIME_CMD="$2"
      shift 2
      ;;
    --config)
      CONFIG_PATH="$2"
      shift 2
      ;;
    --log-file)
      LOG_FILE="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ -z "$RUNTIME_CMD" || -z "$CONFIG_PATH" ]]; then
  echo "error: --runtime-cmd and --config are required" >&2
  usage
  exit 2
fi

if [[ ! -x "$RUNTIME_CMD" ]]; then
  echo "error: runtime executable not found or not executable: $RUNTIME_CMD" >&2
  exit 2
fi

if [[ ! -f "$CONFIG_PATH" ]]; then
  echo "error: config file not found: $CONFIG_PATH" >&2
  exit 2
fi

if [[ -z "$LOG_FILE" ]]; then
  LOG_FILE="$(mktemp -t phase7-conflict-log.XXXXXX)"
fi

set +e
"$RUNTIME_CMD" --config "$CONFIG_PATH" >"$LOG_FILE" 2>&1
RC=$?
set -e

if [[ $RC -eq 0 ]]; then
  echo "error: runtime unexpectedly started successfully; conflict was not rejected" >&2
  echo "log: $LOG_FILE" >&2
  exit 1
fi

if ! grep -q "I2C ownership validation failed" "$LOG_FILE"; then
  echo "error: runtime failed, but ownership-validation marker not found" >&2
  echo "log: $LOG_FILE" >&2
  exit 1
fi

if ! grep -q "duplicate ownership for bus='" "$LOG_FILE"; then
  echo "error: runtime failed, but duplicate ownership detail not found" >&2
  echo "log: $LOG_FILE" >&2
  exit 1
fi

echo "ownership conflict rejection check: PASS"
echo "  runtime_rc: $RC"
echo "  log_file: $LOG_FILE"
