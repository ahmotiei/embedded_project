#!/usr/bin/env bash
set -Eeuo pipefail

[[ $# -eq 2 ]] || {
    echo "Usage: $0 <vm-user> <vm-ip>" >&2
    echo "Example: $0 amir 192.168.122.186" >&2
    exit 2
}

VM_USER="$1"
VM_IP="$2"
SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
REMOTE_DIR="~/embedded/embedded_project/section2"

rsync -av --delete -e ssh "${SOURCE_DIR}/" "${VM_USER}@${VM_IP}:${REMOTE_DIR}/"
cat <<EOF2
Copy completed.
On the VM run:
  cd ~/embedded/embedded_project/section2
  sudo bash scripts/install_vm_section2.sh
EOF2
