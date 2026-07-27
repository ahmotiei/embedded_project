#!/usr/bin/env bash
set -Eeuo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

DURATION_SECONDS="${DURATION_SECONDS:-300}"
INTERVAL_SECONDS="${INTERVAL_SECONDS:-5}"
OUT_DIR="${EVIDENCE_ROOT}/test_2_2_memory"
mkdir -p "${OUT_DIR}"
OUT_FILE="${OUT_DIR}/c_process_memory.csv"

PID="$(systemctl show -p MainPID --value smart-guard-web.service)"
[[ "${PID}" =~ ^[1-9][0-9]*$ ]] || { echo "C service PID not found" >&2; exit 1; }

cat > "${OUT_FILE}" <<CSV
time_s,timestamp,pid,vmrss_kb,vmsize_kb,temperature_c,cpu_usage_percent
CSV

# Keep an actual stream client open throughout the memory test.
"${CURL[@]}" "${API_BASE}/api/v1/stream" -o /dev/null &
STREAM_PID=$!
trap 'kill "${STREAM_PID}" 2>/dev/null || true' EXIT

start_epoch="$(date +%s)"
end_epoch="$((start_epoch + DURATION_SECONDS))"

echo "Sampling C process memory for ${DURATION_SECONDS}s every ${INTERVAL_SECONDS}s..."
while :; do
    now_epoch="$(date +%s)"
    elapsed="$((now_epoch - start_epoch))"
    status_file="/proc/${PID}/status"
    [[ -r "${status_file}" ]] || { echo "C process stopped during test" >&2; exit 1; }
    vmrss="$(awk '/^VmRSS:/ {print $2}' "${status_file}")"
    vmsize="$(awk '/^VmSize:/ {print $2}' "${status_file}")"
    payload="$(json_get "${API_BASE}/api/v1/telemetry")"
    timestamp="$(jq -r '.timestamp' <<<"${payload}")"
    temperature="$(jq -r '.cpu_temperature_c // ""' <<<"${payload}")"
    cpu="$(jq -r '.cpu_usage_percent' <<<"${payload}")"
    printf '%s,%s,%s,%s,%s,%s,%s\n' \
        "${elapsed}" "${timestamp}" "${PID}" "${vmrss}" "${vmsize}" "${temperature}" "${cpu}" >> "${OUT_FILE}"

    (( now_epoch >= end_epoch )) && break
    sleep "${INTERVAL_SECONDS}"
done

kill "${STREAM_PID}" 2>/dev/null || true
wait "${STREAM_PID}" 2>/dev/null || true
trap - EXIT

python3 "${SCRIPT_DIR}/plot_test_2_2.py" "${OUT_FILE}"
echo "Saved results under ${OUT_DIR}"
