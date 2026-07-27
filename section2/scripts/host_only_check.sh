#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

required=(
  vm/src/smart_guard_web.c
  vm/src/CMakeLists.txt
  vm/swagger/swagger_api.py
  vm/swagger/requirements.txt
  systemd/smart-guard-web.service
  systemd/smart-guard-swagger.service
  scripts/install_vm_section2.sh
  tests/verify_endpoints.sh
)

for item in "${required[@]}"; do
    [[ -s "${ROOT_DIR}/${item}" ]] || { echo "Missing: ${item}" >&2; exit 1; }
done

python3 -m py_compile "${ROOT_DIR}/vm/swagger/swagger_api.py"
bash -n "${ROOT_DIR}/scripts/install_vm_section2.sh"
bash -n "${ROOT_DIR}/scripts/copy_to_vm.sh"

echo "Host-side package validation passed. No VM changes were made."
