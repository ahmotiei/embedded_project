#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
MODEL_DIR="${ROOT_DIR}/vision/models"
PROTOTXT="${MODEL_DIR}/deploy.prototxt"
MODEL="${MODEL_DIR}/mobilenet_iter_73000.caffemodel"

mkdir -p "${MODEL_DIR}"

curl -fL --retry 4 --retry-all-errors \
  -o "${PROTOTXT}.tmp" \
  'https://raw.githubusercontent.com/chuanqi305/MobileNet-SSD/master/deploy.prototxt'

curl -fL --retry 4 --retry-all-errors \
  -o "${MODEL}.tmp" \
  'https://github.com/chuanqi305/MobileNet-SSD/raw/refs/heads/master/mobilenet_iter_73000.caffemodel'

[[ $(stat -c %s "${PROTOTXT}.tmp") -gt 40000 ]] || {
  echo "Downloaded prototxt is unexpectedly small." >&2
  exit 1
}
[[ $(stat -c %s "${MODEL}.tmp") -gt 20000000 ]] || {
  echo "Downloaded Caffe model is unexpectedly small." >&2
  exit 1
}

mv -f "${PROTOTXT}.tmp" "${PROTOTXT}"
mv -f "${MODEL}.tmp" "${MODEL}"
chmod 0644 "${PROTOTXT}" "${MODEL}"

echo "MobileNet-SSD model installed in ${MODEL_DIR}"
ls -lh "${PROTOTXT}" "${MODEL}"
