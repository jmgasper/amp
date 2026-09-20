#!/usr/bin/env bash
# Runs a command inside Amp's Haiku test VM over SSH (port 2227).
set -euo pipefail
AMP_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
exec ssh -i "$AMP_ROOT/.vm/id_ed25519" -p 2227 \
    -o IdentitiesOnly=yes -o BatchMode=yes \
    -o UserKnownHostsFile="$AMP_ROOT/.vm/known_hosts" -o StrictHostKeyChecking=accept-new \
    -o ConnectTimeout=10 user@127.0.0.1 "$@"
