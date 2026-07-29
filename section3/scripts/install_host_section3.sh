#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="/opt/smart-guard/build/section3-host"
BIN_DIR="/opt/smart-guard/bin"
ENV_FILE="/etc/smart-guard/host-agent.env"

log(){ printf '\033[1;34m[section3-host]\033[0m %s\n' "$*"; }
die(){ printf '\033[1;31m[section3-host:error]\033[0m %s\n' "$*" >&2; exit 1; }
[[ ${EUID} -eq 0 ]] || die "Run with sudo on the physical host."
[[ -s "${ENV_FILE}" ]] || die "Section 1 host environment is missing: ${ENV_FILE}"

apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential cmake libjpeg-dev pkg-config
install -d -m 0755 "${BUILD_DIR}" "${BIN_DIR}"
rm -rf "${BUILD_DIR:?}/"*
cmake -S "${ROOT_DIR}/host" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --parallel

if [[ -e "${BIN_DIR}/smart_guard_host_agent" ]]; then
  cp -a "${BIN_DIR}/smart_guard_host_agent" \
    "${BIN_DIR}/smart_guard_host_agent.$(date +%Y%m%d_%H%M%S).bak"
fi
install -m 0755 "${BUILD_DIR}/smart_guard_host_agent" "${BIN_DIR}/smart_guard_host_agent"

set_env(){
  local key="$1" value="$2"
  if grep -q "^${key}=" "${ENV_FILE}"; then
    sed -i "s/^${key}=.*/${key}=${value}/" "${ENV_FILE}"
  else
    printf '%s=%s\n' "${key}" "${value}" >> "${ENV_FILE}"
  fi
}
set_env SMART_GUARD_CAMERA_PORT "${SMART_GUARD_VISION_LISTEN_PORT:-9200}"
set_env SMART_GUARD_CAMERA_WIDTH "${SMART_GUARD_CAMERA_WIDTH:-640}"
set_env SMART_GUARD_CAMERA_HEIGHT "${SMART_GUARD_CAMERA_HEIGHT:-480}"
set_env SMART_GUARD_CAMERA_FPS "${SMART_GUARD_CAMERA_FPS:-10}"

systemctl restart smart-guard-host-agent.service
sleep 2
systemctl --no-pager --full status smart-guard-host-agent.service || true
log "Host agent upgraded. Camera path is now VM TCP 9200 with configurable resolution/FPS."
