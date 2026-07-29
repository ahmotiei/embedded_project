#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
[[ ${EUID} -eq 0 ]] || { echo "Run with sudo on the physical host." >&2; exit 1; }
# Section 4 does not change the camera wire protocol. Rebuild and install the
# bundled C host agent so the host keeps sending JPEG frames to VM TCP 9200.
BUILD_DIR=/opt/smart-guard/build/section4-host
BIN_DIR=/opt/smart-guard/bin
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential cmake libjpeg-dev pkg-config stress-ng
install -d -m 0755 "${BUILD_DIR}" "${BIN_DIR}"
rm -rf "${BUILD_DIR:?}/"*
cmake -S "${ROOT_DIR}/host" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --parallel
install -m 0755 "${BUILD_DIR}/smart_guard_host_agent" "${BIN_DIR}/smart_guard_host_agent"
systemctl restart smart-guard-host-agent.service
systemctl --no-pager --full status smart-guard-host-agent.service || true
