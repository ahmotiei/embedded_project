#!/usr/bin/env bash
set -Eeuo pipefail
command -v stress-ng >/dev/null 2>&1 || { echo "Install stress-ng first." >&2; exit 1; }
duration="${1:-5m}"
echo "Starting CPU stress for ${duration}. Monitor temperature and stop early if hardware approaches unsafe limits."
stress-ng --cpu 0 --cpu-method all --timeout "$duration" --metrics-brief
