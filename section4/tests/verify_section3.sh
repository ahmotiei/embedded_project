#!/usr/bin/env bash
set -Eeuo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"
need curl; need jq; need systemctl; need ss

failures=0
check_service(){
  local service="$1"
  if systemctl is-active --quiet "$service"; then
    printf 'PASS service %-32s active\n' "$service"
  else
    printf 'FAIL service %-32s inactive\n' "$service"
    failures=$((failures+1))
  fi
}
check_service smart-guard-web.service
check_service smart-guard-vision.service
check_service smart-guard-mqtt.service

if ss -ltn | awk '{print $4}' | grep -Eq '(^|:)9200$'; then
  echo 'PASS vision input TCP 9200 is listening'
else
  echo 'FAIL vision input TCP 9200 is not listening'; failures=$((failures+1))
fi

if telemetry="$(api_get /api/v1/telemetry)" && jq -e '.student_id and .persons and .timestamp' >/dev/null <<<"$telemetry"; then
  echo "PASS C telemetry: $(jq -c '{student_id,persons,cpu_temperature_c,camera_connected,last_frame_age_seconds}' <<<"$telemetry")"
else
  echo 'FAIL C telemetry endpoint'; failures=$((failures+1))
fi

if persons="$(api_get /api/v1/persons)" && jq -e '.persons >= 0' >/dev/null <<<"$persons"; then
  echo "PASS C persons: $persons"
else
  echo 'FAIL C persons endpoint'; failures=$((failures+1))
fi

if [[ -s /run/smart-guard/latest.jpg ]]; then
  echo "PASS annotated/latest frame exists ($(stat -c %s /run/smart-guard/latest.jpg) bytes)"
else
  echo 'WARN no latest frame yet; confirm the physical host agent uses CAMERA_PORT=9200'
fi

if [[ -s /run/smart-guard/detection_event.json ]]; then
  echo "PASS detection event file: $(cat /run/smart-guard/detection_event.json)"
else
  echo 'INFO no detection event yet; enter the camera frame once'
fi

journalctl -u smart-guard-vision.service -n 20 --no-pager || true
journalctl -u smart-guard-mqtt.service -n 20 --no-pager || true

(( failures == 0 )) || die "Section 3 verification failed with ${failures} error(s)."
log 'Section 3 base verification passed.'
