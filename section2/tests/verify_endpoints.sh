#!/usr/bin/env bash
set -Eeuo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

TOKEN="${SMART_GUARD_COMMAND_TOKEN:-$(sed -n 's/^SMART_GUARD_COMMAND_TOKEN=//p' /etc/smart-guard/section2.env 2>/dev/null | head -n1)}"

echo "== C core health =="
curl -fsS http://127.0.0.1:18080/health | jq .

echo "== Swagger/OpenAPI =="
"${CURL[@]}" "${API_BASE}/openapi.json" | jq -r '.info.title, .info.version'

echo "== Persons =="
json_get "${API_BASE}/api/v1/persons" | jq .

echo "== Telemetry =="
json_get "${API_BASE}/api/v1/telemetry" | jq .

echo "== History =="
json_get "${API_BASE}/api/v1/history" | jq .

echo "== One-frame MJPEG test =="
TMP_STREAM="$(mktemp)"
trap 'rm -f "${TMP_STREAM}"' EXIT
"${CURL[@]}" "${API_BASE}/api/v1/stream?frames=1" -o "${TMP_STREAM}"
grep -a -q 'Content-Type: image/jpeg' "${TMP_STREAM}"
echo "MJPEG response contains a real JPEG part: OK"

if [[ -n "${TOKEN}" ]]; then
    echo "== Safe command test: ping =="
    "${CURL[@]}" \
        -H "X-Command-Token: ${TOKEN}" \
        -H 'Content-Type: application/json' \
        -X POST \
        -d '{"cmd":"ping"}' \
        "${API_BASE}/api/v1/command" | jq .
else
    echo "Command token is unavailable; ping command test skipped." >&2
fi

echo "All non-destructive endpoint tests passed."
