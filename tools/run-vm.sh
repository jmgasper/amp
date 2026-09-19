#!/usr/bin/env bash
# Starts TasAmp's isolated Haiku R1/beta6 test VM (VNC 127.0.0.1:5907, SSH 2227).
set -euo pipefail
TA_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$TA_ROOT"
if [[ ! -f .vm/work.qcow2 ]]; then
    echo 'Missing .vm/work.qcow2. See docs/VM.md for the test VM setup.' >&2
    exit 1
fi
if [[ -S .vm/qmp.sock ]] && python3 tools/vm.py status >/dev/null 2>&1; then
    echo 'The TasAmp VM is already running.'
    exit 0
fi
if [[ -f .vm/qemu.pid ]] && kill -0 "$(cat .vm/qemu.pid)" 2>/dev/null; then
    echo 'VM process exists but QMP is not answering; inspect the existing process.' >&2
    exit 1
fi
rm -f .vm/qmp.sock
exec qemu-system-x86_64 -enable-kvm -cpu host -m "${TA_VM_MEMORY:-6144}" -smp "${TA_VM_CPUS:-6}" \
    -drive file=.vm/work.qcow2,format=qcow2,if=ide,index=0 \
    -nic user,model=e1000,hostfwd=tcp:127.0.0.1:2227-:22 \
    -device qemu-xhci -device usb-tablet -vga std -display none \
    -audiodev none,id=snd0 -device intel-hda -device hda-duplex,audiodev=snd0 \
    -vnc 127.0.0.1:7 -qmp unix:.vm/qmp.sock,server=on,wait=off \
    -pidfile .vm/qemu.pid -serial file:.vm/serial.log -daemonize
