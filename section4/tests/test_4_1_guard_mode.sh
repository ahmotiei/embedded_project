#!/usr/bin/env bash
set -Eeuo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"
need curl; need jq; need journalctl
stamp="$(now_stamp)"
out="${EVIDENCE_ROOT}/test_4_1_guard/${stamp}"
mkdir -p "$out"

before="$(api_get '/api/v1/blackbox?limit=0' | jq -r '.total_detection_events')"
log "Arming guard mode through the C API..."
guard_set true | tee "$out/guard_arm_response.json" | jq .
api_get /api/v1/guard | tee "$out/guard_state_armed.json" | jq .

subscriber_pid=""
if [[ -r "${MQTT_ENV}" ]]; then
  # shellcheck disable=SC1090
  source "${MQTT_ENV}"
  if [[ -n "${SMART_GUARD_MQTT_HOST:-}" && -n "${SMART_GUARD_MQTT_USERNAME:-}" &&
        -n "${SMART_GUARD_MQTT_PASSWORD:-}" && "${SMART_GUARD_MQTT_PASSWORD}" != REPLACE_* ]]; then
    mosquitto_sub -h "${SMART_GUARD_MQTT_HOST}" -p "${SMART_GUARD_MQTT_PORT:-1883}" \
      -u "${SMART_GUARD_MQTT_USERNAME}" -P "${SMART_GUARD_MQTT_PASSWORD}" \
      -q 1 -t "alarm/${STUDENT_ID}/home" -C 1 -W 120 -v >"$out/alarm_mqtt.txt" 2>&1 &
    subscriber_pid=$!
  fi
fi

log "Leave the camera frame until Persons=0, then enter the frame. Waiting up to 120 seconds for a new event..."
for _ in $(seq 1 120); do
  current="$(api_get '/api/v1/blackbox?limit=1' | jq -r '.total_detection_events')"
  if (( current > before )); then break; fi
  sleep 1
done
current="$(api_get '/api/v1/blackbox?limit=10' | tee "$out/blackbox_after_alarm.json" | jq -r '.total_detection_events')"
(( current > before )) || die "No new detection event was recorded during the test."

[[ -z "$subscriber_pid" ]] || wait "$subscriber_pid" || true
cp -a /run/smart-guard/detection_event.json "$out/" 2>/dev/null || true
journalctl -u smart-guard-mqtt.service --since '-3 minutes' --no-pager > "$out/controller_journal.txt"
log "Guard alarm event recorded. Capture the dashboard, received email with JPEG, and MQTT output for report test 4-1."

if [[ "${SMART_GUARD_KEEP_ARMED:-0}" != 1 ]]; then
  guard_set false | tee "$out/guard_disarm_response.json" | jq .
fi
