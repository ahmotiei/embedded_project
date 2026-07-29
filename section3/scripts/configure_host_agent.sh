#!/usr/bin/env bash
set -Eeuo pipefail

ENV_FILE="/etc/smart-guard/host-agent.env"
VISION_PORT="${SMART_GUARD_VISION_LISTEN_PORT:-9200}"

log(){ printf '\033[1;34m[host-agent]\033[0m %s\n' "$*"; }
die(){ printf '\033[1;31m[host-agent:error]\033[0m %s\n' "$*" >&2; exit 1; }
[[ ${EUID} -eq 0 ]] || die "Run with sudo on the physical host."
[[ -s "${ENV_FILE}" ]] || die "Missing ${ENV_FILE}; install Section 1 host agent first."

if grep -q '^SMART_GUARD_CAMERA_PORT=' "${ENV_FILE}"; then
  sed -i "s/^SMART_GUARD_CAMERA_PORT=.*/SMART_GUARD_CAMERA_PORT=${VISION_PORT}/" "${ENV_FILE}"
else
  printf '\nSMART_GUARD_CAMERA_PORT=%s\n' "${VISION_PORT}" >> "${ENV_FILE}"
fi

systemctl restart smart-guard-host-agent.service
sleep 2
systemctl --no-pager --full status smart-guard-host-agent.service || true
log "Camera frames now go to the VM vision relay on TCP ${VISION_PORT}."
log "Temperature packets remain on UDP 9090 and still enter the C core directly."
