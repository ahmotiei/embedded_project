#!/usr/bin/env bash
set -Eeuo pipefail
[[ ${EUID} -eq 0 ]] || { echo "Run with sudo on the physical host." >&2; exit 1; }
duration="${1:-45}"
echo "Stopping physical camera sender for ${duration}s..."
systemctl stop smart-guard-host-agent.service
sleep "$duration"
echo "Restarting physical camera sender..."
systemctl start smart-guard-host-agent.service
systemctl --no-pager --full status smart-guard-host-agent.service || true
