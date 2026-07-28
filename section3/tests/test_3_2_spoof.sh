#!/usr/bin/env bash
set -Eeuo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"
need curl; need jq
OUT="${EVIDENCE_ROOT}/test_3_2_spoof"
mkdir -p "${OUT}/data" "${OUT}/images" "${OUT}/logs"

printf 'عکس چاپ‌شده یا تصویر انسان روی موبایل را بدون حضور انسان واقعی جلوی دوربین بگذارید و Enter بزنید...'
read -r _
CSV="${OUT}/data/spoof_samples.csv"
printf 'sample,timestamp,detected_persons,fooled\n' > "$CSV"
fooled=0; total=30
for ((i=1;i<=total;i++)); do
  response="$(api_get /api/v1/persons)"
  count="$(jq -r '.persons' <<<"$response")"
  timestamp="$(jq -r '.timestamp' <<<"$response")"
  flag=0; (( count > 0 )) && { flag=1; fooled=$((fooled+1)); }
  printf '%d,%s,%d,%d\n' "$i" "$timestamp" "$count" "$flag" >> "$CSV"
  sleep 1
done
rate="$(awk -v c="$fooled" -v t="$total" 'BEGIN{printf "%.2f",100*c/t}')"
cp -f /run/smart-guard/latest.jpg "${OUT}/images/spoof_scene.jpg" 2>/dev/null || true
cat > "${OUT}/logs/analysis.txt" <<TXT
Spoof detections: ${fooled}/${total}
Spoof success rate: ${rate}%
Interpretation: HOG/SVM detects appearance, not liveness. A printed/displayed person can therefore be classified as a real person. Recommended defenses: temporal motion/challenge checks, face anti-spoofing, depth/IR sensing, or multi-frame optical-flow consistency.
TXT
cat "${OUT}/logs/analysis.txt"
log "Test 3-2 evidence saved under ${OUT}"
