#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALL_ROOT="/opt/smart-guard"
SECTION4_ROOT="${INSTALL_ROOT}/section4"
BIN_ROOT="${INSTALL_ROOT}/bin"
BUILD_ROOT="${INSTALL_ROOT}/build/section4"
ETC_ROOT="/etc/smart-guard"

log(){ printf '\033[1;34m[section4]\033[0m %s\n' "$*"; }
warn(){ printf '\033[1;33m[section4:warning]\033[0m %s\n' "$*" >&2; }
die(){ printf '\033[1;31m[section4:error]\033[0m %s\n' "$*" >&2; exit 1; }
[[ ${EUID} -eq 0 ]] || die "Run with sudo on the VM/OrangePi."
[[ -s "${ETC_ROOT}/vm.env" ]] || die "Section 1 configuration is missing: ${ETC_ROOT}/vm.env"
[[ -s "${ETC_ROOT}/section2.env" ]] || die "Section 2 configuration is missing: ${ETC_ROOT}/section2.env"
[[ -s "${ETC_ROOT}/tls/server.crt" && -s "${ETC_ROOT}/tls/server.key" ]] || \
  die "TLS certificate/key from Section 1 are missing."

log "Installing C, SQLite, OpenCV, MQTT, SMTP, Swagger, and test dependencies..."
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y \
  build-essential cmake pkg-config rsync jq curl ca-certificates \
  libmicrohttpd-dev libgnutls28-dev libmosquitto-dev libcjson-dev \
  libcurl4-openssl-dev libsqlite3-dev sqlite3 \
  python3 python3-venv python3-opencv python3-numpy python3-matplotlib \
  mosquitto-clients openssh-client stress-ng

if ! id smartguard >/dev/null 2>&1; then
  useradd --system --home-dir /nonexistent --no-create-home --shell /usr/sbin/nologin smartguard
fi

install -d -m 0755 "${SECTION4_ROOT}" "${BIN_ROOT}" "${BUILD_ROOT}" "${ETC_ROOT}"
for directory in vision src web watchdog swagger tests docs host broker; do
  if [[ -d "${ROOT_DIR}/${directory}" ]]; then
    install -d -m 0755 "${SECTION4_ROOT}/${directory}"
    rsync -a --delete "${ROOT_DIR}/${directory}/" "${SECTION4_ROOT}/${directory}/"
  fi
done

stamp="$(date +%Y%m%d_%H%M%S)"
for binary in smart_guard_web smart_guard_notifier smart_guard_watchdog; do
  if [[ -e "${BIN_ROOT}/${binary}" ]]; then
    cp -a "${BIN_ROOT}/${binary}" "${BIN_ROOT}/${binary}.${stamp}.bak"
  fi
done

log "Building the Section 4 C web core..."
rm -rf "${BUILD_ROOT}/web"
cmake -S "${SECTION4_ROOT}/web" -B "${BUILD_ROOT}/web" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_ROOT}/web" --parallel
install -m 0755 "${BUILD_ROOT}/web/smart_guard_web" "${BIN_ROOT}/smart_guard_web"

log "Building the Section 4 C controller (MQTT/email/SQLite/thermal)..."
rm -rf "${BUILD_ROOT}/controller"
cmake -S "${SECTION4_ROOT}/src" -B "${BUILD_ROOT}/controller" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_ROOT}/controller" --parallel
install -m 0755 "${BUILD_ROOT}/controller/smart_guard_notifier" "${BIN_ROOT}/smart_guard_notifier"

log "Building the root-only processed-frame watchdog..."
rm -rf "${BUILD_ROOT}/watchdog"
cmake -S "${SECTION4_ROOT}/watchdog" -B "${BUILD_ROOT}/watchdog" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_ROOT}/watchdog" --parallel
install -m 0755 "${BUILD_ROOT}/watchdog/smart_guard_watchdog" "${BIN_ROOT}/smart_guard_watchdog"

log "Installing the thin FastAPI/Swagger gateway..."
rm -rf "${SECTION4_ROOT}/venv"
python3 -m venv "${SECTION4_ROOT}/venv"
"${SECTION4_ROOT}/venv/bin/python" -m pip install --upgrade pip wheel
"${SECTION4_ROOT}/venv/bin/pip" install -r "${SECTION4_ROOT}/swagger/requirements.txt"

if [[ ! -s "${ETC_ROOT}/section4.env" ]]; then
  install -m 0644 "${ROOT_DIR}/config/section4.env.example" "${ETC_ROOT}/section4.env"
else
  warn "Keeping existing ${ETC_ROOT}/section4.env. Compare it with config/section4.env.example."
fi
if [[ ! -s "${ETC_ROOT}/mqtt.env" ]]; then
  install -m 0640 -o root -g smartguard "${ROOT_DIR}/config/mqtt.env.example" "${ETC_ROOT}/mqtt.env"
  warn "Created ${ETC_ROOT}/mqtt.env with a placeholder. Configure it before the MQTT/alarm tests; the controller will keep retrying until authentication succeeds."
fi
if [[ ! -s "${ETC_ROOT}/alerts.env" ]]; then
  install -m 0640 -o root -g smartguard "${ROOT_DIR}/config/alerts.env.example" "${ETC_ROOT}/alerts.env"
  warn "Created ${ETC_ROOT}/alerts.env with email disabled. Add SMTP credentials and set SMART_GUARD_EMAIL_ENABLED=1."
fi
chown root:smartguard "${ETC_ROOT}/mqtt.env" "${ETC_ROOT}/alerts.env"
chmod 0640 "${ETC_ROOT}/mqtt.env" "${ETC_ROOT}/alerts.env"
chmod 0644 "${ETC_ROOT}/section4.env"

install -d -m 0750 -o smartguard -g smartguard /var/lib/smart-guard /var/lib/smart-guard/blackbox
if [[ ! -e /var/lib/smart-guard/guard_mode ]]; then
  printf '0\n' > /var/lib/smart-guard/guard_mode
fi
chown smartguard:smartguard /var/lib/smart-guard/guard_mode
chmod 0640 /var/lib/smart-guard/guard_mode

chown -R root:smartguard "${SECTION4_ROOT}"
find "${SECTION4_ROOT}" -type d -exec chmod 0755 {} +
find "${SECTION4_ROOT}" -type f -exec chmod 0644 {} +
chmod 0755 "${SECTION4_ROOT}/vision/person_detector.py" "${SECTION4_ROOT}/tests/"*.sh \
  "${SECTION4_ROOT}/tests/"*.py "${SECTION4_ROOT}/scripts/"*.sh 2>/dev/null || true
chmod 0755 "${SECTION4_ROOT}/venv/bin/"* 2>/dev/null || true

for unit in smart-guard-web.service smart-guard-vision.service smart-guard-mqtt.service \
            smart-guard-watchdog.service smart-guard-swagger.service; do
  install -m 0644 "${ROOT_DIR}/systemd/${unit}" "/etc/systemd/system/${unit}"
done

systemctl daemon-reload
systemctl enable smart-guard-web.service smart-guard-vision.service smart-guard-mqtt.service \
                 smart-guard-watchdog.service smart-guard-swagger.service
systemctl restart smart-guard-web.service
systemctl restart smart-guard-vision.service
systemctl restart smart-guard-mqtt.service
systemctl restart smart-guard-watchdog.service
systemctl restart smart-guard-swagger.service

sleep 4
for unit in smart-guard-web.service smart-guard-vision.service smart-guard-mqtt.service \
            smart-guard-watchdog.service smart-guard-swagger.service; do
  systemctl --no-pager --full status "${unit}" || true
done

VM_IP="$(sed -n 's/^SMART_GUARD_PUBLIC_HOST=//p' "${ETC_ROOT}/vm.env" | head -n1)"
log "Section 4 installation complete."
log "Dashboard: https://${VM_IP:-<vm-ip>}/"
log "Swagger:   https://${VM_IP:-<vm-ip>}:8443/docs"
log "Verify:    sudo bash ${SECTION4_ROOT}/tests/verify_section4.sh"
log "The physical host agent remains the Section 3 camera sender on TCP port 9200."
