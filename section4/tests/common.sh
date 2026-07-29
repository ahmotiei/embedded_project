#!/usr/bin/env bash
set -Eeuo pipefail

STUDENT_ID="${SMART_GUARD_STUDENT_ID:-401102553}"
EVIDENCE_ROOT="${SMART_GUARD_EVIDENCE_ROOT:-/opt/smart-guard/section4/docs/evidence}"
C_CORE_URL="${SMART_GUARD_C_CORE_URL:-http://127.0.0.1:18080}"
SECTION2_ENV="${SMART_GUARD_SECTION2_ENV:-/etc/smart-guard/section2.env}"
MQTT_ENV="${SMART_GUARD_MQTT_ENV:-/etc/smart-guard/mqtt.env}"

log(){ printf '\033[1;34m[test]\033[0m %s\n' "$*"; }
warn(){ printf '\033[1;33m[test:warning]\033[0m %s\n' "$*" >&2; }
die(){ printf '\033[1;31m[test:error]\033[0m %s\n' "$*" >&2; exit 1; }
need(){ command -v "$1" >/dev/null 2>&1 || die "Missing command: $1"; }
now_stamp(){ date +%Y%m%d_%H%M%S; }
api_get(){ curl -fsS --max-time 5 "${C_CORE_URL}$1"; }

load_command_token(){
  if [[ -z "${SMART_GUARD_COMMAND_TOKEN:-}" && -r "${SECTION2_ENV}" ]]; then
    # shellcheck disable=SC1090
    source "${SECTION2_ENV}"
  fi
  [[ -n "${SMART_GUARD_COMMAND_TOKEN:-}" ]] || die "SMART_GUARD_COMMAND_TOKEN is unavailable. Run as root or export it."
}

guard_set(){
  local enabled="$1"
  load_command_token
  curl -fsS --max-time 5 -X POST "${C_CORE_URL}/api/v1/guard" \
    -H 'Content-Type: application/json' \
    -H "X-Command-Token: ${SMART_GUARD_COMMAND_TOKEN}" \
    -d "{\"enabled\":${enabled}}"
}
