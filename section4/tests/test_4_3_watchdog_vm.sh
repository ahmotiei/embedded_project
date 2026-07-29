#!/usr/bin/env bash
set -Eeuo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"
need journalctl; need systemctl
stamp="$(now_stamp)"
out="${EVIDENCE_ROOT}/test_4_3_watchdog/${stamp}"
mkdir -p "$out"
old_id="$(jq -r '.event_id // "0"' /run/smart-guard/system_event.json 2>/dev/null || echo 0)"
start="$(date --iso-8601=seconds)"
log "Now stop/unplug the physical-host camera sender for at least 35 seconds."
log "Waiting up to 90 seconds for a new camera_tamper event..."
for _ in $(seq 1 90); do
  new_id="$(jq -r '.event_id // "0"' /run/smart-guard/system_event.json 2>/dev/null || echo 0)"
  if [[ "$new_id" != 0 && "$new_id" != "$old_id" ]]; then break; fi
  sleep 1
done
new_id="$(jq -r '.event_id // "0"' /run/smart-guard/system_event.json 2>/dev/null || echo 0)"
[[ "$new_id" != 0 && "$new_id" != "$old_id" ]] || die "Watchdog event was not observed."
cp -a /run/smart-guard/system_event.json "$out/"
systemctl show smart-guard-vision.service -p ActiveEnterTimestamp -p NRestarts -p MainPID > "$out/vision_service_state.txt"
journalctl -u smart-guard-watchdog.service --since "$start" --no-pager > "$out/watchdog_journal.txt"
journalctl -u smart-guard-vision.service --since "$start" --no-pager > "$out/vision_journal.txt"
journalctl -u smart-guard-mqtt.service --since "$start" --no-pager > "$out/controller_journal.txt"
log "Watchdog detection/restart captured. Restore the host camera sender and film the stream recovery."
