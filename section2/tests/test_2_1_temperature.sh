#!/usr/bin/env bash
set -Eeuo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

MODE="${1:-}"
case "${MODE}" in
    idle|stream|stream_detection) ;;
    *) echo "Usage: $0 {idle|stream|stream_detection}" >&2; exit 2 ;;
esac

DURATION_SECONDS="${DURATION_SECONDS:-300}"
INTERVAL_SECONDS="${INTERVAL_SECONDS:-30}"
OUT_DIR="${EVIDENCE_ROOT}/test_2_1_temperature"
mkdir -p "${OUT_DIR}"
OUT_FILE="${OUT_DIR}/${MODE}.csv"

cat > "${OUT_FILE}" <<CSV
time_s,timestamp,temperature_c,cpu_usage_percent,memory_available_kb,persons,camera_connected
CSV

start_epoch="$(date +%s)"
end_epoch="$((start_epoch + DURATION_SECONDS))"

echo "Collecting ${MODE} temperature data for ${DURATION_SECONDS}s every ${INTERVAL_SECONDS}s..."
while :; do
    now_epoch="$(date +%s)"
    elapsed="$((now_epoch - start_epoch))"
    payload="$(json_get "${API_BASE}/api/v1/telemetry")"
    jq -r --arg elapsed "${elapsed}" '[
        $elapsed,
        .timestamp,
        (.cpu_temperature_c // ""),
        .cpu_usage_percent,
        .memory_available_kb,
        .persons,
        .camera_connected
    ] | @csv' <<<"${payload}" >> "${OUT_FILE}"

    (( now_epoch >= end_epoch )) && break
    sleep "${INTERVAL_SECONDS}"
done

echo "Saved: ${OUT_FILE}"
echo "After collecting all three modes, run:"
echo "  python3 ${SCRIPT_DIR}/plot_test_2_1.py ${OUT_DIR}"
