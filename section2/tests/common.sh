#!/usr/bin/env bash
set -Eeuo pipefail

# Experiment scripts run inside the VM, so Swagger is available locally.
VM_IP="${VM_IP:-127.0.0.1}"
API_BASE="${API_BASE:-https://${VM_IP}:8443}"
EVIDENCE_ROOT="${EVIDENCE_ROOT:-${HOME}/embedded/embedded_project/section2/evidence}"

CURL=(
    curl
    --fail
    --silent
    --show-error
    --insecure
    --connect-timeout 5
)

mkdir -p "${EVIDENCE_ROOT}"

json_get() {
    "${CURL[@]}" "$1"
}
