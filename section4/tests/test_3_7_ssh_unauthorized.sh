#!/usr/bin/env bash
set -Eeuo pipefail
VM_HOST="${SMART_GUARD_VM_IP:?Set SMART_GUARD_VM_IP to the VM address}"
VM_USER="${SMART_GUARD_SSH_TEST_USER:-root}"
OUT="${SMART_GUARD_EVIDENCE_DIR:-./test_3_7_ssh_auth_$(date +%Y%m%d_%H%M%S)}"
mkdir -p "$OUT"
key="${OUT}/unauthorized_test_key"
ssh-keygen -q -t ed25519 -N '' -f "$key"

set +e
ssh -vvv -i "$key" -o IdentitiesOnly=yes -o BatchMode=yes \
  -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 \
  "${VM_USER}@${VM_HOST}" true >"${OUT}/unauthorized_ssh.log" 2>&1
rc=$?
set -e
rm -f "$key" "$key.pub"
cat "${OUT}/unauthorized_ssh.log"
printf 'exit_code=%d\n' "$rc" | tee "${OUT}/result.txt"
if (( rc == 0 )); then
  echo 'FAIL: unauthorized SSH key was accepted.' | tee -a "${OUT}/result.txt" >&2
  exit 1
fi
if grep -Eqi 'permission denied|authentication failed|no supported authentication methods' "${OUT}/unauthorized_ssh.log"; then
  echo 'PASS: unauthorized SSH login failed.' | tee -a "${OUT}/result.txt"
else
  echo 'WARNING: SSH failed, but confirm the log shows authentication rejection rather than a network error.' | tee -a "${OUT}/result.txt"
fi
