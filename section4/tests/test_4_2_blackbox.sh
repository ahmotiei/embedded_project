#!/usr/bin/env bash
set -Eeuo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"
need curl; need jq; need sqlite3
stamp="$(now_stamp)"
out="${EVIDENCE_ROOT}/test_4_2_blackbox/${stamp}"
mkdir -p "$out"

api_get '/api/v1/blackbox?limit=50' | tee "$out/blackbox_api.json" | jq .
sqlite3 -header -column /var/lib/smart-guard/blackbox.db \
  "SELECT key,value FROM metadata ORDER BY key;" | tee "$out/metadata.txt"
sqlite3 -header -column /var/lib/smart-guard/blackbox.db \
  "SELECT id,event_id,detected_at,persons,temperature_c,vision_fps,guard_armed,snapshot_path FROM detections ORDER BY id DESC LIMIT 20;" \
  | tee "$out/detections.txt"
sqlite3 /var/lib/smart-guard/blackbox.db "PRAGMA journal_mode; PRAGMA integrity_check;" | tee "$out/sqlite_integrity.txt"
log "Take a terminal screenshot of metadata/detections and the dashboard total counter for report test 4-2."
