#!/usr/bin/env bash
set -Eeuo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"
need curl; need jq

OUT="${EVIDENCE_ROOT}/test_3_1_lighting"
mkdir -p "${OUT}/data" "${OUT}/images" "${OUT}/logs"
SUMMARY="${OUT}/data/lighting_accuracy.csv"
printf 'condition,expected_persons,correct_samples,total_samples,accuracy_percent\n' > "${SUMMARY}"

conditions=(daylight artificial low_light backlight)
for condition in "${conditions[@]}"; do
  printf '\nشرایط %s را آماده کنید. تعداد واقعی افراد داخل کادر را وارد کنید: ' "$condition"
  read -r expected
  [[ "$expected" =~ ^[0-9]+$ ]] || die 'Expected-person count must be a non-negative integer.'
  printf 'بعد از آماده‌شدن صحنه Enter بزنید...'
  read -r _

  csv="${OUT}/data/${condition}.csv"
  printf 'sample,timestamp,expected,detected,correct\n' > "$csv"
  correct=0; total=20
  for ((i=1;i<=total;i++)); do
    response="$(api_get /api/v1/persons)"
    detected="$(jq -r '.persons' <<<"$response")"
    timestamp="$(jq -r '.timestamp' <<<"$response")"
    ok=0; [[ "$detected" == "$expected" ]] && { ok=1; correct=$((correct+1)); }
    printf '%d,%s,%d,%d,%d\n' "$i" "$timestamp" "$expected" "$detected" "$ok" >> "$csv"
    sleep 1
  done
  accuracy="$(awk -v c="$correct" -v t="$total" 'BEGIN{printf "%.2f",100*c/t}')"
  printf '%s,%d,%d,%d,%s\n' "$condition" "$expected" "$correct" "$total" "$accuracy" >> "$SUMMARY"
  cp -f /run/smart-guard/latest.jpg "${OUT}/images/${condition}.jpg" 2>/dev/null || true
  log "$condition accuracy=${accuracy}% (${correct}/${total})"
done

if command -v column >/dev/null 2>&1; then
  column -s, -t "$SUMMARY" | tee "${OUT}/logs/summary.txt"
else
  cat "$SUMMARY" | tee "${OUT}/logs/summary.txt"
fi
journalctl -u smart-guard-vision.service -n 80 --no-pager > "${OUT}/logs/vision_journal.txt"
log "Test 3-1 evidence saved under ${OUT}"
