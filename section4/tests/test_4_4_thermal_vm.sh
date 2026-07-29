#!/usr/bin/env bash
set -Eeuo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"
need curl; need jq
stamp="$(now_stamp)"
out="${EVIDENCE_ROOT}/test_4_4_thermal/${stamp}"
mkdir -p "$out"
log "Start tests/test_4_4_stress_host.sh on the physical host. Sampling for 6 minutes..."
printf 'timestamp,temp_c,thermal_active,mode,target_max_fps,detection_width,output_width,vision_fps\n' > "$out/thermal_samples.csv"
for _ in $(seq 1 180); do
  telemetry="$(api_get /api/v1/telemetry || echo '{}')"
  thermal="$(api_get /api/v1/thermal || echo '{}')"
  vision="$(api_get /api/v1/vision-status || echo '{}')"
  printf '%s,%s,%s,%s,%s,%s,%s,%s\n' \
    "$(date --iso-8601=seconds)" \
    "$(jq -r '.cpu_temperature_c // ""' <<<"$telemetry")" \
    "$(jq -r '.thermal_active // false' <<<"$thermal")" \
    "$(jq -r '.mode // "unknown"' <<<"$thermal")" \
    "$(jq -r '.target_max_fps // ""' <<<"$thermal")" \
    "$(jq -r '.detection_width // ""' <<<"$thermal")" \
    "$(jq -r '.output_width // ""' <<<"$thermal")" \
    "$(jq -r '.fps // ""' <<<"$vision")" >> "$out/thermal_samples.csv"
  sleep 2
done
cp -a /run/smart-guard/thermal_status.json /run/smart-guard/vision_control.json /run/smart-guard/vision_status.json "$out/" 2>/dev/null || true
journalctl -u smart-guard-mqtt.service --since '-7 minutes' --no-pager > "$out/controller_journal.txt"
log "Thermal samples captured. Use the CSV and dashboard/stream screenshots in report test 4-4."
