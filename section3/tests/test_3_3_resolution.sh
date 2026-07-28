#!/usr/bin/env bash
set -Eeuo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"
need curl; need jq; need systemctl
OUT="${EVIDENCE_ROOT}/test_3_3_resolution"
mkdir -p "${OUT}/data" "${OUT}/images" "${OUT}/logs"
SUMMARY="${OUT}/data/resolution_summary.csv"
printf 'requested_resolution,actual_resolution,expected_persons,samples,accuracy_percent,mean_fps,max_cpu_temp_c,mean_rss_kb\n' > "$SUMMARY"

resolutions=(320x240 640x480 1280x720)
for resolution in "${resolutions[@]}"; do
  width="${resolution%x*}"; height="${resolution#*x}"
  printf '\nروی HOST اجرا کنید:\n  sudo sed -i "s/^SMART_GUARD_CAMERA_WIDTH=.*/SMART_GUARD_CAMERA_WIDTH=%s/; s/^SMART_GUARD_CAMERA_HEIGHT=.*/SMART_GUARD_CAMERA_HEIGHT=%s/" /etc/smart-guard/host-agent.env && sudo systemctl restart smart-guard-host-agent\n' "$width" "$height"
  printf 'سپس تعداد واقعی افراد ثابت داخل کادر را وارد کنید: '
  read -r expected
  [[ "$expected" =~ ^[0-9]+$ ]] || die 'Expected count must be integer.'
  printf 'بعد از پایدارشدن تصویر Enter بزنید...'; read -r _

  csv="${OUT}/data/${resolution}.csv"
  printf 'elapsed_s,timestamp,input_width,input_height,fps,persons,cpu_temp_c,rss_kb,correct\n' > "$csv"
  vision_pid="$(systemctl show -p MainPID --value smart-guard-vision.service)"
  [[ "$vision_pid" =~ ^[1-9][0-9]*$ ]] || die 'Vision PID unavailable.'
  correct=0; samples=60; max_temp=-999
  for ((i=0;i<samples;i++)); do
    status="$(cat /run/smart-guard/vision_status.json 2>/dev/null || echo '{}')"
    telemetry="$(api_get /api/v1/telemetry)"
    iw="$(jq -r '.input_width // 0' <<<"$status")"; ih="$(jq -r '.input_height // 0' <<<"$status")"
    fps="$(jq -r '.fps // 0' <<<"$status")"; persons="$(jq -r '.persons // 0' <<<"$status")"
    ts="$(jq -r '.timestamp // "unknown"' <<<"$status")"
    temp="$(jq -r '.cpu_temperature_c // 0' <<<"$telemetry")"
    rss="$(awk '/^VmRSS:/{print $2}' "/proc/${vision_pid}/status" 2>/dev/null || echo 0)"
    ok=0; [[ "$persons" == "$expected" ]] && { ok=1; correct=$((correct+1)); }
    awk -v t="$temp" -v m="$max_temp" 'BEGIN{exit !(t>m)}' && max_temp="$temp" || true
    printf '%d,%s,%s,%s,%s,%s,%s,%s,%d\n' "$((i*5))" "$ts" "$iw" "$ih" "$fps" "$persons" "$temp" "$rss" "$ok" >> "$csv"
    sleep 5
  done
  accuracy="$(awk -F, 'NR>1{s+=$9;n++}END{printf "%.2f",100*s/n}' "$csv")"
  mean_fps="$(awk -F, 'NR>1{s+=$5;n++}END{printf "%.3f",s/n}' "$csv")"
  mean_rss="$(awk -F, 'NR>1{s+=$8;n++}END{printf "%.1f",s/n}' "$csv")"
  actual="$(awk -F, 'NR==2{print $3"x"$4}' "$csv")"
  printf '%s,%s,%d,%d,%s,%s,%s,%s\n' "$resolution" "$actual" "$expected" "$samples" "$accuracy" "$mean_fps" "$max_temp" "$mean_rss" >> "$SUMMARY"
  cp -f /run/smart-guard/latest.jpg "${OUT}/images/${resolution}.jpg" 2>/dev/null || true
  log "$resolution done: FPS=${mean_fps}, maxTemp=${max_temp}, RSS=${mean_rss}KB, accuracy=${accuracy}%"
done
if command -v column >/dev/null 2>&1; then
  column -s, -t "$SUMMARY" | tee "${OUT}/logs/summary.txt"
else
  cat "$SUMMARY" | tee "${OUT}/logs/summary.txt"
fi
log "Test 3-3 evidence saved under ${OUT}"
