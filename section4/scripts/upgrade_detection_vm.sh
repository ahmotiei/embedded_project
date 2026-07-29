#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALL_ROOT="/opt/smart-guard/section4"
ENV_FILE="/etc/smart-guard/section4.env"

[[ ${EUID} -eq 0 ]] || { echo "Run with sudo on the VM." >&2; exit 1; }
[[ -s "${ROOT_DIR}/vision/models/deploy.prototxt" ]] || {
  echo "Missing vision/models/deploy.prototxt. Run scripts/download_vision_model.sh on the host first." >&2
  exit 1
}
[[ -s "${ROOT_DIR}/vision/models/mobilenet_iter_73000.caffemodel" ]] || {
  echo "Missing MobileNet-SSD model. Run scripts/download_vision_model.sh on the host first." >&2
  exit 1
}
[[ -s "${ENV_FILE}" ]] || { echo "Missing ${ENV_FILE}" >&2; exit 1; }

install -d -m 0755 "${INSTALL_ROOT}/vision/models"
install -m 0755 "${ROOT_DIR}/vision/person_detector.py" "${INSTALL_ROOT}/vision/person_detector.py"
install -m 0644 "${ROOT_DIR}/vision/models/deploy.prototxt" "${INSTALL_ROOT}/vision/models/deploy.prototxt"
install -m 0644 "${ROOT_DIR}/vision/models/mobilenet_iter_73000.caffemodel" \
  "${INSTALL_ROOT}/vision/models/mobilenet_iter_73000.caffemodel"
chown -R root:smartguard "${INSTALL_ROOT}/vision"

set_key() {
  local key="$1" value="$2"
  if grep -qE "^${key}=" "${ENV_FILE}"; then
    sed -i -E "s|^${key}=.*|${key}=${value}|" "${ENV_FILE}"
  else
    printf '%s=%s\n' "${key}" "${value}" >> "${ENV_FILE}"
  fi
}

set_key SMART_GUARD_VISION_BACKEND mobilenet_ssd
set_key SMART_GUARD_VISION_MODEL_DIR /opt/smart-guard/section4/vision/models
set_key SMART_GUARD_VISION_DNN_CONFIDENCE 0.42
set_key SMART_GUARD_VISION_DNN_NMS_THRESHOLD 0.35
set_key SMART_GUARD_VISION_DNN_MIN_BOX_HEIGHT 48
set_key SMART_GUARD_VISION_DNN_MIN_AREA_RATIO 0.006
set_key SMART_GUARD_DETECTION_HOLD_FRAMES 2
set_key SMART_GUARD_VISION_LOW_LIGHT_ENHANCE 1
set_key SMART_GUARD_VISION_LOW_LIGHT_THRESHOLD 75
set_key SMART_GUARD_NORMAL_DETECTION_WIDTH 640

python3 -m py_compile "${INSTALL_ROOT}/vision/person_detector.py"
systemctl restart smart-guard-vision.service
sleep 4
systemctl --no-pager --full status smart-guard-vision.service
journalctl -u smart-guard-vision.service -n 40 --no-pager
