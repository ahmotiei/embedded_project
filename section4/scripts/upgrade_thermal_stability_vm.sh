#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALL_ROOT=/opt/smart-guard
SECTION4_ROOT="${INSTALL_ROOT}/section4"
BUILD_ROOT="${INSTALL_ROOT}/build/section4/controller-thermal-stable"
ENV_FILE=/etc/smart-guard/section4.env
BIN="${INSTALL_ROOT}/bin/smart_guard_notifier"

[[ ${EUID} -eq 0 ]] || { echo 'Run with sudo on the VM.' >&2; exit 1; }
[[ -s "${ROOT_DIR}/src/smart_guard_notifier.c" ]] || { echo 'Missing notifier source.' >&2; exit 1; }
[[ -s "${ENV_FILE}" ]] || { echo "Missing ${ENV_FILE}." >&2; exit 1; }

set_key() {
  local key="$1" value="$2"
  if grep -qE "^${key}=" "${ENV_FILE}"; then
    sed -i -E "s|^${key}=.*|${key}=${value}|" "${ENV_FILE}"
  else
    printf '%s=%s\n' "${key}" "${value}" >> "${ENV_FILE}"
  fi
}

install -d -m 0755 "${SECTION4_ROOT}/src" "$(dirname "${BUILD_ROOT}")"
install -m 0644 "${ROOT_DIR}/src/smart_guard_notifier.c" "${SECTION4_ROOT}/src/smart_guard_notifier.c"
install -m 0644 "${ROOT_DIR}/src/CMakeLists.txt" "${SECTION4_ROOT}/src/CMakeLists.txt"

stamp="$(date +%Y%m%d_%H%M%S)"
cp -a "${BIN}" "${BIN}.${stamp}.bak"
cp -a "${ENV_FILE}" "${ENV_FILE}.${stamp}.bak"

set_key SMART_GUARD_THERMAL_ENTER_SAMPLES 3
set_key SMART_GUARD_THERMAL_EXIT_SAMPLES 5
set_key SMART_GUARD_THERMAL_MIN_DWELL_SECONDS 60

rm -rf "${BUILD_ROOT}"
cmake -S "${SECTION4_ROOT}/src" -B "${BUILD_ROOT}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_ROOT}" --parallel
install -m 0755 "${BUILD_ROOT}/smart_guard_notifier" "${BIN}"

systemctl restart smart-guard-mqtt.service
sleep 3
systemctl --no-pager --full status smart-guard-mqtt.service
journalctl -u smart-guard-mqtt.service -n 30 --no-pager
