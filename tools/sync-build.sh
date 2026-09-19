#!/usr/bin/env bash
# Sync sources to the TasAmp VM and build there, printing compiler errors only.
# Usage: tools/sync-build.sh [make-target] [max-lines]
set -uo pipefail
TA_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$TA_ROOT"
TARGET=${1:-all}
tar -czf - Makefile src tests tools resources vendor 2>/dev/null |
    bash tools/haiku.sh "mkdir -p /boot/home/tasamp && tar xzf - --warning=no-timestamp -C /boot/home/tasamp 2>/dev/null; cd /boot/home/tasamp && make -j6 $TARGET 2>&1 | grep -E -B1 -A4 'error|Error|undefined|warning: unused' | head -${2:-80}; echo \"exit: \${PIPESTATUS[0]}\""
