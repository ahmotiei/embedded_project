#!/usr/bin/env bash
set -Eeuo pipefail

# This script performs only the host-side staging step. It does not contact the VM.
SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
DEST_DIR="${1:-${HOME}/embedded/embedded_project/section2}"

mkdir -p "$(dirname -- "${DEST_DIR}")"
if [[ "${SOURCE_DIR}" != "${DEST_DIR}" ]]; then
    rm -rf "${DEST_DIR}"
    cp -a "${SOURCE_DIR}" "${DEST_DIR}"
fi

printf 'Section 2 files are staged on the host at:\n  %s\n' "${DEST_DIR}"
printf 'No VM files were changed.\n'
printf 'Next step later: bash %s/scripts/copy_to_vm.sh <vm-user> <vm-ip>\n' "${DEST_DIR}"
