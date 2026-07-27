#!/usr/bin/env bash
set -Eeuo pipefail

# Install/upgrade Smart Guard Section 2 on the VM.
# Run only after copying the complete section2 directory to the VM.

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALL_ROOT="/opt/smart-guard"
SECTION2_ROOT="${INSTALL_ROOT}/section2"
BIN_ROOT="${INSTALL_ROOT}/bin"
BUILD_ROOT="${INSTALL_ROOT}/build/section2"
ETC_ROOT="/etc/smart-guard"
TLS_ROOT="${ETC_ROOT}/tls"

log() { printf '\033[1;34m[section2]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[section2:error]\033[0m %s\n' "$*" >&2; exit 1; }

[[ ${EUID} -eq 0 ]] || die "Run with sudo."
[[ -s "${TLS_ROOT}/server.crt" && -s "${TLS_ROOT}/server.key" ]] || \
    die "Section 1 TLS files are missing under ${TLS_ROOT}."
[[ -s "${ETC_ROOT}/vm.env" ]] || die "Section 1 VM environment file is missing: ${ETC_ROOT}/vm.env"

log "Installing build, Swagger, and experiment dependencies..."
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libmicrohttpd-dev \
    libgnutls28-dev \
    python3 \
    python3-venv \
    python3-pip \
    python3-matplotlib \
    jq \
    curl \
    openssl \
    rsync \
    ca-certificates

if ! id smartguard >/dev/null 2>&1; then
    useradd --system --home-dir /nonexistent --no-create-home --shell /usr/sbin/nologin smartguard
fi

install -d -m 0755 "${SECTION2_ROOT}" "${SECTION2_ROOT}/src" "${SECTION2_ROOT}/swagger" \
    "${SECTION2_ROOT}/tests" "${SECTION2_ROOT}/docs" "${BIN_ROOT}" "${BUILD_ROOT}"

if [[ -e "${BIN_ROOT}/smart_guard_web" ]]; then
    backup_stamp="$(date +%Y%m%d_%H%M%S)"
    cp -a "${BIN_ROOT}/smart_guard_web" "${BIN_ROOT}/smart_guard_web.section1.${backup_stamp}.bak"
    log "Backed up the Section 1 binary."
fi
if [[ -e "${INSTALL_ROOT}/source/smart_guard_web.c" ]]; then
    backup_stamp="${backup_stamp:-$(date +%Y%m%d_%H%M%S)}"
    cp -a "${INSTALL_ROOT}/source/smart_guard_web.c" \
        "${INSTALL_ROOT}/source/smart_guard_web.section1.${backup_stamp}.bak.c"
fi

rsync -a --delete "${ROOT_DIR}/vm/src/" "${SECTION2_ROOT}/src/"
rsync -a --delete "${ROOT_DIR}/vm/swagger/" "${SECTION2_ROOT}/swagger/"
rsync -a --delete "${ROOT_DIR}/tests/" "${SECTION2_ROOT}/tests/"
rsync -a --delete "${ROOT_DIR}/docs/" "${SECTION2_ROOT}/docs/"

log "Building the upgraded C server..."
rm -rf "${BUILD_ROOT}"
cmake -S "${SECTION2_ROOT}/src" -B "${BUILD_ROOT}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_ROOT}" --parallel
install -m 0755 "${BUILD_ROOT}/smart_guard_web" "${BIN_ROOT}/smart_guard_web"

log "Creating the isolated FastAPI environment..."
rm -rf "${SECTION2_ROOT}/venv"
python3 -m venv "${SECTION2_ROOT}/venv"
"${SECTION2_ROOT}/venv/bin/python" -m pip install --upgrade pip wheel
"${SECTION2_ROOT}/venv/bin/pip" install -r "${SECTION2_ROOT}/swagger/requirements.txt"

install -d -m 0755 "${ETC_ROOT}"
TOKEN=""
if [[ -s "${ETC_ROOT}/section2.env" ]]; then
    TOKEN="$(sed -n 's/^SMART_GUARD_COMMAND_TOKEN=//p' "${ETC_ROOT}/section2.env" | head -n1)"
fi
if [[ -z "${TOKEN}" || "${TOKEN}" == "REPLACE_WITH_RANDOM_TOKEN" ]]; then
    TOKEN="$(openssl rand -hex 32)"
fi

cat > "${ETC_ROOT}/section2.env" <<ENVEOF
SMART_GUARD_INTERNAL_API_PORT=18080
SMART_GUARD_C_CORE_URL=http://127.0.0.1:18080
SMART_GUARD_LATEST_FRAME_FILE=/run/smart-guard/latest.jpg
SMART_GUARD_COMMAND_TOKEN=${TOKEN}
SMART_GUARD_PROXY_TIMEOUT_SECONDS=5
ENVEOF
chown root:smartguard "${ETC_ROOT}/section2.env"
chmod 0640 "${ETC_ROOT}/section2.env"

chown -R root:smartguard "${SECTION2_ROOT}"
find "${SECTION2_ROOT}" -type d -exec chmod 0755 {} +
find "${SECTION2_ROOT}" -type f -exec chmod 0644 {} +
chmod 0755 "${SECTION2_ROOT}/venv/bin/"* "${SECTION2_ROOT}/tests/"*.sh 2>/dev/null || true
chown root:smartguard "${TLS_ROOT}/server.key" "${TLS_ROOT}/server.crt"
chmod 0640 "${TLS_ROOT}/server.key"
chmod 0644 "${TLS_ROOT}/server.crt"

install -m 0644 "${ROOT_DIR}/systemd/smart-guard-web.service" /etc/systemd/system/smart-guard-web.service
install -m 0644 "${ROOT_DIR}/systemd/smart-guard-swagger.service" /etc/systemd/system/smart-guard-swagger.service

log "Restarting the C service and enabling Swagger..."
systemctl daemon-reload
systemctl enable smart-guard-web.service smart-guard-swagger.service
systemctl restart smart-guard-web.service
systemctl restart smart-guard-swagger.service

sleep 3
systemctl --no-pager --full status smart-guard-web.service || true
systemctl --no-pager --full status smart-guard-swagger.service || true

VM_IP="$(sed -n 's/^SMART_GUARD_PUBLIC_HOST=//p' "${ETC_ROOT}/vm.env" | head -n1)"
VM_IP="${VM_IP:-127.0.0.1}"

log "Installation complete."
log "Dashboard:  https://${VM_IP}/"
log "Swagger:    https://${VM_IP}:8443/docs"
log "C loopback: http://127.0.0.1:18080/health"
log "Command token (store it securely): ${TOKEN}"
log "Verification command: sudo bash ${SECTION2_ROOT}/tests/verify_endpoints.sh"
