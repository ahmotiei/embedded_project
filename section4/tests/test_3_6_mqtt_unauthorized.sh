#!/usr/bin/env bash
set -Eeuo pipefail
STUDENT_ID="${SMART_GUARD_STUDENT_ID:-401102553}"
HOST="${SMART_GUARD_MQTT_HOST:-127.0.0.1}"
PORT="${SMART_GUARD_MQTT_PORT:-1883}"
OUT="${SMART_GUARD_EVIDENCE_DIR:-./test_3_6_mqtt_auth_$(date +%Y%m%d_%H%M%S)}"
mkdir -p "$OUT"
topic="persons/${STUDENT_ID}/home"

set +e
timeout 6 mosquitto_sub -h "$HOST" -p "$PORT" -t "$topic" -C 1 -d >"${OUT}/anonymous_attempt.log" 2>&1
anonymous_rc=$?
timeout 6 mosquitto_sub -h "$HOST" -p "$PORT" -u invalid_user -P invalid_password -t "$topic" -C 1 -d >"${OUT}/wrong_password_attempt.log" 2>&1
wrong_rc=$?
set -e

cat "${OUT}/anonymous_attempt.log"
cat "${OUT}/wrong_password_attempt.log"
printf 'anonymous_exit_code=%d\nwrong_credentials_exit_code=%d\n' "$anonymous_rc" "$wrong_rc" | tee "${OUT}/result.txt"
if (( anonymous_rc == 0 || wrong_rc == 0 )); then
  echo 'FAIL: an unauthorized MQTT connection succeeded.' >&2
  exit 1
fi
echo 'PASS: anonymous and wrong-password MQTT attempts failed.' | tee -a "${OUT}/result.txt"
