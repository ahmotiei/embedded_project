#!/usr/bin/env bash
set -Eeuo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"
need curl; need jq; need systemctl; need sqlite3

failures=0
for service in smart-guard-web.service smart-guard-vision.service smart-guard-mqtt.service smart-guard-watchdog.service smart-guard-swagger.service; do
  if systemctl is-active --quiet "$service"; then
    printf 'PASS service %-34s active\n' "$service"
  else
    printf 'FAIL service %-34s inactive\n' "$service"
    failures=$((failures+1))
  fi
done

for endpoint in /api/v1/guard '/api/v1/blackbox?limit=5' /api/v1/thermal /api/v1/vision-status; do
  if response="$(api_get "$endpoint")" && jq -e . >/dev/null <<<"$response"; then
    echo "PASS ${endpoint}: $(jq -c . <<<"$response" | cut -c1-220)"
  else
    echo "FAIL ${endpoint}"
    failures=$((failures+1))
  fi
done

if [[ -s /var/lib/smart-guard/blackbox.db ]]; then
  echo "PASS SQLite black box exists"
  sqlite3 /var/lib/smart-guard/blackbox.db '.schema detections'
  sqlite3 /var/lib/smart-guard/blackbox.db \
    "SELECT key,value FROM metadata ORDER BY key; SELECT COUNT(*) AS stored_records FROM detections;"
else
  echo "INFO blackbox.db is created by the controller shortly after startup or first detection"
fi

if [[ -s /run/smart-guard/vision_control.json ]]; then
  echo "PASS adaptive control: $(cat /run/smart-guard/vision_control.json)"
else
  echo "FAIL vision control file missing"
  failures=$((failures+1))
fi

journalctl -u smart-guard-mqtt.service -n 30 --no-pager || true
journalctl -u smart-guard-watchdog.service -n 30 --no-pager || true
(( failures == 0 )) || die "Section 4 verification failed with ${failures} error(s)."
log "Section 4 base verification passed."
