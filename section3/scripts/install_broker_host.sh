#!/usr/bin/env bash
set -Eeuo pipefail

STUDENT_ID="${SMART_GUARD_STUDENT_ID:-401102553}"
MQTT_USER="${SMART_GUARD_MQTT_USERNAME:-smartguard_${STUDENT_ID}}"
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

log(){ printf '\033[1;34m[broker]\033[0m %s\n' "$*"; }
die(){ printf '\033[1;31m[broker:error]\033[0m %s\n' "$*" >&2; exit 1; }
[[ ${EUID} -eq 0 ]] || die "Run with sudo on the physical host."

apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y mosquitto mosquitto-clients python3-paho-mqtt

install -m 0644 "${ROOT_DIR}/broker/mosquitto.conf" /etc/mosquitto/conf.d/smart-guard.conf
sed "s/smartguard_401102553/${MQTT_USER}/g; s/401102553/${STUDENT_ID}/g" \
  "${ROOT_DIR}/broker/acl-smart-guard" > /etc/mosquitto/acl-smart-guard
chmod 0640 /etc/mosquitto/acl-smart-guard
chown root:mosquitto /etc/mosquitto/acl-smart-guard

log "Create/update password for MQTT user ${MQTT_USER}."
if [[ -e /etc/mosquitto/passwd-smart-guard ]]; then
  mosquitto_passwd /etc/mosquitto/passwd-smart-guard "${MQTT_USER}"
else
  mosquitto_passwd -c /etc/mosquitto/passwd-smart-guard "${MQTT_USER}"
fi
chown root:mosquitto /etc/mosquitto/passwd-smart-guard
chmod 0640 /etc/mosquitto/passwd-smart-guard

mosquitto -c /etc/mosquitto/mosquitto.conf -t
systemctl enable mosquitto.service
systemctl restart mosquitto.service
systemctl --no-pager --full status mosquitto.service || true

HOST_IP="$(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for(i=1;i<=NF;i++) if($i=="src") {print $(i+1); exit}}')"
log "Broker ready on ${HOST_IP:-<host-ip>}:1883"
log "Copy the same username/password into /etc/smart-guard/mqtt.env on the VM."
