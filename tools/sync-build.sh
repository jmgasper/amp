#!/usr/bin/env bash
# Sync sources to the Amp VM and build there, printing compiler errors only.
# Usage: tools/sync-build.sh [make-target] [max-lines]
set -uo pipefail
AMP_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$AMP_ROOT"
TARGET=${1:-all}
# Everything the build and the packaging step read, so `make package` also works
# in the guest.
tar -czf - Makefile README.md LICENSE docs src tests tools resources vendor 2>/dev/null |
    bash tools/haiku.sh "mkdir -p /boot/home/amp && tar xzf - --warning=no-timestamp -C /boot/home/amp 2>/dev/null; cd /boot/home/amp && make -j6 $TARGET 2>&1 | grep -E -B1 -A4 'error|Error|undefined|warning: unused' | head -${2:-80}; echo \"exit: \${PIPESTATUS[0]}\""
