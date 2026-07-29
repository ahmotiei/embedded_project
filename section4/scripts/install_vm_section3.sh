#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALL_ROOT="/opt/smart-guard"
SECTION3_ROOT="${INSTALL_ROOT}/section3"
BIN_ROOT="${INSTALL_ROOT}/bin"
BUILD_ROOT="${INSTALL_ROOT}/build/section3"
ETC_ROOT="/etc/smart-guard"

log(){ printf '\033[1;34m[section3]\033[0m %s\n' "$*"; }
warn(){ printf '\033[1;33m[section3:warning]\033[0m %s\n' "$*" >&2; }
die(){ printf '\033[1;31m[section3:error]\033[0m %s\n' "$*" >&2; exit 1; }
[[ ${EUID} -eq 0 ]] || die "Run with sudo on the VM."
[[ -s "${ETC_ROOT}/vm.env" ]] || die "Section 1 is missing: ${ETC_ROOT}/vm.env"
[[ -s "${ETC_ROOT}/section2.env" ]] || die "Section 2 is missing: ${ETC_ROOT}/section2.env"
[[ -s "${ETC_ROOT}/tls/server.crt" && -s "${ETC_ROOT}/tls/server.key" ]] || \
  die "TLS certificate/key from Section 1 are missing."

log "Installing C, OpenCV, MQTT, SMTP, and test dependencies..."
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y \
  build-essential cmake pkg-config rsync jq curl ca-certificates \
  libmicrohttpd-dev libgnutls28-dev libmosquitto-dev libcjson-dev \
  libcurl4-openssl-dev python3 python3-opencv python3-numpy \
  python3-matplotlib mosquitto-clients openssh-client

if ! id smartguard >/dev/null 2>&1; then
  useradd --system --home-dir /nonexistent --no-create-home --shell /usr/sbin/nologin smartguard
fi

install -d -m 0755 "${SECTION3_ROOT}" "${BIN_ROOT}" "${BUILD_ROOT}"
rsync -a --delete "${ROOT_DIR}/vision/" "${SECTION3_ROOT}/vision/"
rsync -a --delete "${ROOT_DIR}/src/" "${SECTION3_ROOT}/src/"
rsync -a --delete "${ROOT_DIR}/web/" "${SECTION3_ROOT}/web/"
rsync -a --delete "${ROOT_DIR}/tests/" "${SECTION3_ROOT}/tests/"
rsync -a --delete "${ROOT_DIR}/docs/" "${SECTION3_ROOT}/docs/"

stamp="$(date +%Y%m%d_%H%M%S)"
for binary in smart_guard_web smart_guard_notifier smart_guard_mqtt; do
  if [[ -e "${BIN_ROOT}/${binary}" ]]; then
    cp -a "${BIN_ROOT}/${binary}" "${BIN_ROOT}/${binary}.${stamp}.bak"
  fi
done

log "Building the Section 2-compatible C web core with loopback vision relay support..."
rm -rf "${BUILD_ROOT}/web"
cmake -S "${SECTION3_ROOT}/web" -B "${BUILD_ROOT}/web" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_ROOT}/web" --parallel
install -m 0755 "${BUILD_ROOT}/web/smart_guard_web" "${BIN_ROOT}/smart_guard_web"

log "Building the Section 3 C MQTT/email notifier..."
rm -rf "${BUILD_ROOT}/notifier"
cmake -S "${SECTION3_ROOT}/src" -B "${BUILD_ROOT}/notifier" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_ROOT}/notifier" --parallel
install -m 0755 "${BUILD_ROOT}/notifier/smart_guard_notifier" "${BIN_ROOT}/smart_guard_notifier"

install -d -m 0755 "${ETC_ROOT}"
if [[ ! -s "${ETC_ROOT}/section3.env" ]]; then
  install -m 0644 "${ROOT_DIR}/config/section3.env.example" "${ETC_ROOT}/section3.env"
fi
if [[ ! -s "${ETC_ROOT}/mqtt.env" ]]; then
  install -m 0640 -o root -g smartguard "${ROOT_DIR}/config/mqtt.env.example" "${ETC_ROOT}/mqtt.env"
  warn "Created ${ETC_ROOT}/mqtt.env with a placeholder password. Configure it before starting MQTT."
fi
if [[ ! -s "${ETC_ROOT}/alerts.env" ]]; then
  install -m 0640 -o root -g smartguard "${ROOT_DIR}/config/alerts.env.example" "${ETC_ROOT}/alerts.env"
  warn "Created ${ETC_ROOT}/alerts.env. Configure SMTP credentials before the email test."
fi
chown root:smartguard "${ETC_ROOT}/mqtt.env" "${ETC_ROOT}/alerts.env"
chmod 0640 "${ETC_ROOT}/mqtt.env" "${ETC_ROOT}/alerts.env"
chmod 0644 "${ETC_ROOT}/section3.env"

chown -R root:smartguard "${SECTION3_ROOT}"
find "${SECTION3_ROOT}" -type d -exec chmod 0755 {} +
find "${SECTION3_ROOT}" -type f -exec chmod 0644 {} +
chmod 0755 "${SECTION3_ROOT}/vision/person_detector.py" "${SECTION3_ROOT}/tests/"*.sh \
  "${SECTION3_ROOT}/tests/"*.py 2>/dev/null || true

install -m 0644 "${ROOT_DIR}/systemd/smart-guard-web.service" /etc/systemd/system/smart-guard-web.service
install -m 0644 "${ROOT_DIR}/systemd/smart-guard-vision.service" /etc/systemd/system/smart-guard-vision.service
install -m 0644 "${ROOT_DIR}/systemd/smart-guard-mqtt.service" /etc/systemd/system/smart-guard-mqtt.service

systemctl daemon-reload
systemctl enable smart-guard-web.service smart-guard-vision.service smart-guard-mqtt.service
systemctl restart smart-guard-web.service
systemctl restart smart-guard-vision.service

if grep -q 'REPLACE_WITH_A_STRONG_PASSWORD' "${ETC_ROOT}/mqtt.env"; then
  warn "MQTT service was not started because mqtt.env still contains the placeholder password."
else
  systemctl restart smart-guard-mqtt.service
fi

sleep 3
systemctl --no-pager --full status smart-guard-web.service || true
systemctl --no-pager --full status smart-guard-vision.service || true
systemctl --no-pager --full status smart-guard-mqtt.service || true

VM_IP="$(sed -n 's/^SMART_GUARD_PUBLIC_HOST=//p' "${ETC_ROOT}/vm.env" | head -n1)"
log "VM installation complete."
log "Next, run scripts/configure_host_agent.sh on the physical host so CAMERA_PORT becomes 9200."
log "Dashboard: https://${VM_IP:-<vm-ip>}/"
log "Swagger:   https://${VM_IP:-<vm-ip>}:8443/docs"
log "Verify:    sudo bash ${SECTION3_ROOT}/tests/verify_section3.sh"
