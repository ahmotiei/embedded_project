#!/usr/bin/env bash
set -Eeuo pipefail

STUDENT_ID="${SMART_GUARD_STUDENT_ID:-401102553}"
EVIDENCE_ROOT="${SMART_GUARD_EVIDENCE_ROOT:-/opt/smart-guard/section3/docs/evidence}"
C_CORE_URL="${SMART_GUARD_C_CORE_URL:-http://127.0.0.1:18080}"

log(){ printf '\033[1;34m[test]\033[0m %s\n' "$*"; }
warn(){ printf '\033[1;33m[test:warning]\033[0m %s\n' "$*" >&2; }
die(){ printf '\033[1;31m[test:error]\033[0m %s\n' "$*" >&2; exit 1; }
need(){ command -v "$1" >/dev/null 2>&1 || die "Missing command: $1"; }
now_stamp(){ date +%Y%m%d_%H%M%S; }
api_get(){ curl -fsS --max-time 5 "${C_CORE_URL}$1"; }
