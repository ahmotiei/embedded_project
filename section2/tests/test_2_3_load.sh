#!/usr/bin/env bash
set -Eeuo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

CONCURRENCY="${CONCURRENCY:-50}"
DURATION_SECONDS="${DURATION_SECONDS:-30}"
OUT_DIR="${EVIDENCE_ROOT}/test_2_3_load"
WORK_DIR="${OUT_DIR}/workers"
mkdir -p "${WORK_DIR}"
rm -f "${WORK_DIR}"/*.csv

LATENCY_FILE="${OUT_DIR}/latencies.csv"
TELEMETRY_FILE="${OUT_DIR}/telemetry_during_load.csv"
BASELINE_FILE="${OUT_DIR}/baseline_latencies.csv"

echo "request_index,latency_seconds,http_code" > "${BASELINE_FILE}"
for index in $(seq 1 20); do
    result="$(curl --insecure --silent --output /dev/null --write-out '%{time_total},%{http_code}' "${API_BASE}/api/v1/telemetry")"
    printf '%s,%s\n' "${index}" "${result}" >> "${BASELINE_FILE}"
done

end_epoch="$(( $(date +%s) + DURATION_SECONDS ))"
worker() {
    local id="$1"
    local sequence=0
    local file="${WORK_DIR}/worker_${id}.csv"
    echo "worker,sequence,epoch,latency_seconds,http_code" > "${file}"
    while (( $(date +%s) < end_epoch )); do
        sequence=$((sequence + 1))
        epoch="$(date +%s.%N)"
        result="$(curl --insecure --silent --output /dev/null --write-out '%{time_total},%{http_code}' "${API_BASE}/api/v1/telemetry" || printf '0,000')"
        printf '%s,%s,%s,%s\n' "${id}" "${sequence}" "${epoch}" "${result}" >> "${file}"
    done
}

cat > "${TELEMETRY_FILE}" <<CSV
time_s,timestamp,temperature_c,cpu_usage_percent,memory_available_kb
CSV
start_epoch="$(date +%s)"
telemetry_sampler() {
    while (( $(date +%s) <= end_epoch )); do
        now="$(date +%s)"
        elapsed="$((now - start_epoch))"
        payload="$(json_get "${API_BASE}/api/v1/telemetry" || true)"
        if [[ -n "${payload}" ]]; then
            jq -r --arg elapsed "${elapsed}" '[
                $elapsed,
                .timestamp,
                (.cpu_temperature_c // ""),
                .cpu_usage_percent,
                .memory_available_kb
            ] | @csv' <<<"${payload}" >> "${TELEMETRY_FILE}"
        fi
        sleep 1
    done
}

telemetry_sampler &
SAMPLER_PID=$!

echo "Starting ${CONCURRENCY} concurrent curl loops for ${DURATION_SECONDS} seconds..."
worker_pids=()
for id in $(seq 1 "${CONCURRENCY}"); do
    worker "${id}" &
    worker_pids+=("$!")
done

for pid in "${worker_pids[@]}"; do
    wait "${pid}"
done
wait "${SAMPLER_PID}" || true

{
    echo "worker,sequence,epoch,latency_seconds,http_code"
    for file in "${WORK_DIR}"/*.csv; do
        tail -n +2 "${file}"
    done
} > "${LATENCY_FILE}"

python3 "${SCRIPT_DIR}/analyze_test_2_3.py" \
    "${BASELINE_FILE}" "${LATENCY_FILE}" "${TELEMETRY_FILE}"

echo "Saved results under ${OUT_DIR}"
