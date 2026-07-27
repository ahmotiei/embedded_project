#!/usr/bin/env bash
set -Eeuo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

DURATION_SECONDS="${DURATION_SECONDS:-240}"
INTERVAL_SECONDS="${INTERVAL_SECONDS:-5}"
OUT_DIR="${EVIDENCE_ROOT}/test_2_4_network_recovery"
mkdir -p "${OUT_DIR}"
STATUS_FILE="${OUT_DIR}/recovery_status.csv"
LOG_FILE="${OUT_DIR}/smart_guard_web_journal.log"
START_TIME="$(date --iso-8601=seconds)"

cat > "${STATUS_FILE}" <<CSV
time_s,timestamp,api_reachable,camera_connected,last_frame_age_seconds,temperature_stale
CSV

cat <<'MESSAGE'
Run this script from the VM console or a session that will remain available.
1) Start/open the stream.
2) Disconnect the physical host network/Wi-Fi.
3) Keep it disconnected for 2 minutes.
4) Reconnect it and wait for the host agent to reconnect automatically.
The script records API state and later exports journal logs.
MESSAGE

start_epoch="$(date +%s)"
end_epoch="$((start_epoch + DURATION_SECONDS))"
while :; do
    now="$(date +%s)"
    elapsed="$((now - start_epoch))"
    timestamp="$(date --iso-8601=seconds)"
    payload="$(json_get "${API_BASE}/api/v1/telemetry" 2>/dev/null || true)"
    if [[ -n "${payload}" ]]; then
        camera="$(jq -r '.camera_connected' <<<"${payload}")"
        frame_age="$(jq -r '.last_frame_age_seconds' <<<"${payload}")"
        stale="$(jq -r '.cpu_temperature_stale' <<<"${payload}")"
        printf '%s,%s,true,%s,%s,%s\n' "${elapsed}" "${timestamp}" "${camera}" "${frame_age}" "${stale}" >> "${STATUS_FILE}"
    else
        printf '%s,%s,false,,,\n' "${elapsed}" "${timestamp}" >> "${STATUS_FILE}"
    fi
    (( now >= end_epoch )) && break
    sleep "${INTERVAL_SECONDS}"
done

journalctl -u smart-guard-web.service --since "${START_TIME}" --no-pager -o short-iso > "${LOG_FILE}"

echo "Saved: ${STATUS_FILE}"
echo "Saved: ${LOG_FILE}"
echo "Look for: 'Host camera disconnected' followed by 'Host camera connected'."
