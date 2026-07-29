#!/usr/bin/env bash
set -Eeuo pipefail

STUDENT_ID="${SMART_GUARD_STUDENT_ID:-401102553}"
BROKER_HOST="${SMART_GUARD_MQTT_HOST:-127.0.0.1}"
BROKER_PORT="${SMART_GUARD_MQTT_PORT:-1883}"
MQTT_USER="${SMART_GUARD_MQTT_USERNAME:-smartguard_${STUDENT_ID}}"
MQTT_PASSWORD="${SMART_GUARD_MQTT_PASSWORD:-}"
OUT="${SMART_GUARD_EVIDENCE_DIR:-./test_3_4_lwt_$(date +%Y%m%d_%H%M%S)}"
mkdir -p "$OUT"
[[ -n "$MQTT_PASSWORD" ]] || { echo 'Set SMART_GUARD_MQTT_PASSWORD in the shell.' >&2; exit 1; }
command -v mosquitto_sub >/dev/null || { echo 'mosquitto_sub is required.' >&2; exit 1; }

status_topic="status/${STUDENT_ID}/home"
logfile="${OUT}/status_messages.log"
mosquitto_sub -h "$BROKER_HOST" -p "$BROKER_PORT" -u "$MQTT_USER" -P "$MQTT_PASSWORD" \
  -t "$status_topic" -v -d >"$logfile" 2>&1 &
subscriber_pid=$!
trap 'kill "$subscriber_pid" 2>/dev/null || true' EXIT
sleep 2

cat <<TXT
مرحله A ـ نمایش واقعی LWT:
روی VM این دستور را اجرا کنید:
  sudo systemctl kill --kill-who=main --signal=SIGKILL smart-guard-mqtt.service
Broker باید پیام retained با status=offline-unexpected را منتشر کند.
بعد systemd سرویس را خودکار بالا می‌آورد و status=online دیده می‌شود.
پس از انجام، Enter بزنید.
TXT
read -r _
sleep 8

cat <<TXT
مرحله B ـ آزمایش متن صورت پروژه (خاموشی Broker به مدت 3 دقیقه):
روی همین کامپیوتر اجرا کنید:
  sudo systemctl stop mosquitto
  sleep 180
  sudo systemctl start mosquitto
سپس حدود 10 ثانیه صبر کنید تا کلاینت VM reconnect شود و Enter بزنید.
نکته فنی: وقتی خود Broker خاموش است، امکان انتشار LWT ندارد؛ LWT فقط زمانی
منتشر می‌شود که Broker زنده باشد و قطع غیرمنتظره Client را تشخیص دهد. مرحله A
اثبات LWT است و مرحله B اثبات بازیابی خودکار بعد از بازگشت Broker.
TXT
read -r _
sleep 10
kill "$subscriber_pid" 2>/dev/null || true
wait "$subscriber_pid" 2>/dev/null || true
trap - EXIT

cat "$logfile"
if grep -q 'offline-unexpected' "$logfile" && grep -q '"status":"online"' "$logfile"; then
  echo 'PASS: LWT and automatic online recovery are both visible.' | tee "${OUT}/result.txt"
else
  echo 'FAIL: expected offline-unexpected and online messages were not both captured.' | tee "${OUT}/result.txt" >&2
  exit 1
fi
