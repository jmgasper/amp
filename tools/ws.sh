#!/usr/bin/env bash
# Runs a command on the owner's Haiku workstation (Threadripper 1950X, GeForce GTX 1070)
# over SSH with Amp's key. Mirrors tools/haiku.sh, which does the same for the QEMU VM.
set -euo pipefail
AMP_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
exec ssh -i "$AMP_ROOT/.vm/ws_id_ed25519" -o IdentitiesOnly=yes -o BatchMode=yes \
    -o UserKnownHostsFile="$AMP_ROOT/.vm/ws_known_hosts" -o StrictHostKeyChecking=accept-new \
    -o LogLevel=ERROR -o ConnectTimeout=15 -o ServerAliveInterval=30 \
    "${AMP_WS_USER:-user}@${AMP_WS_HOST:-192.168.1.244}" "$@"
