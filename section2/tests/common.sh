#!/usr/bin/env bash
set -Eeuo pipefail

VM_IP="${VM_IP:-$(sed -n 's/^SMART_GUARD_PUBLIC_HOST=//p' /etc/smart-guard/vm.env 2>/dev/null | head -n1)}"
VM_IP="${VM_IP:-127.0.0.1}"
API_BASE="${API_BASE:-https://${VM_IP}:8443}"
EVIDENCE_ROOT="${EVIDENCE_ROOT:-${HOME}/embedded/embedded_project/section2/evidence}"
CURL=(curl --fail --silent --show-error --insecure --connect-timeout 5)

mkdir -p "${EVIDENCE_ROOT}"

json_get() {
    "${CURL[@]}" "$1"
}
