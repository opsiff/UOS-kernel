#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Boot a kernel under QEMU with an emulated NVMe controller, load the Rust
# M1 driver, and print the identity it reported. Exits non-zero when the
# driver did not probe or the controller was not identified.
#
# usage: nvme_m1_qemu.sh <bzImage> <nvme_rust_m1.ko> [workdir]

set -euo pipefail

KERNEL=${1:?usage: $0 <bzImage> <module.ko> [workdir]}
MODULE=${2:?usage: $0 <bzImage> <module.ko> [workdir]}
WORK=${3:-$(mktemp -d)}
BUSYBOX=${BUSYBOX:-/bin/busybox}

log() { printf "[nvme-m1-qemu] %s\n" "$*"; }

[ -f "$KERNEL" ] || { log "kernel not found: $KERNEL"; exit 1; }
[ -f "$MODULE" ] || { log "module not found: $MODULE"; exit 1; }
[ -x "$BUSYBOX" ] || { log "busybox not found: $BUSYBOX"; exit 1; }

ROOT="$WORK/rootfs"
INITRD="$WORK/initramfs.cpio.gz"
DISK="$WORK/nvme.img"
OUT="$WORK/serial.log"

rm -rf "$ROOT"; mkdir -p "$ROOT"/{bin,sbin,proc,sys,dev}
cp "$BUSYBOX" "$ROOT/bin/busybox"
for applet in sh insmod mount umount sleep poweroff dmesg cat grep; do
    ln -sf /bin/busybox "$ROOT/bin/$applet"
done
ln -sf /bin/busybox "$ROOT/sbin/insmod"
cp "$MODULE" "$ROOT/nvme_rust_m1.ko"

cat > "$ROOT/init" <<"INIT"
#!/bin/busybox sh
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev 2>/dev/null || true
echo "=== nvme-m1: loading module ==="
insmod /nvme_rust_m1.ko && echo "=== nvme-m1: insmod ok ===" || echo "=== nvme-m1: insmod FAILED ==="
sleep 2
echo "=== nvme-m1: dmesg ==="
dmesg | grep -iE "nvme|CAP=" || true
echo "=== nvme-m1: done ==="
poweroff -f
INIT
chmod +x "$ROOT/init"

( cd "$ROOT" && find . | cpio -o -H newc 2>/dev/null | gzip -9 ) > "$INITRD"
truncate -s 64M "$DISK"

log "booting QEMU (kernel=$KERNEL)"
"timeout" 180 qemu-system-x86_64 \
    -m 2048 -smp 2 \
    -kernel "$KERNEL" \
    -initrd "$INITRD" \
    -append "console=ttyS0 rdinit=/init panic=-1 loglevel=7" \
    -nographic -no-reboot \
    -drive file="$DISK",format=raw,if=none,id=nvmedrv \
    -device nvme,drive=nvmedrv,serial=deadbeef \
    > "$OUT" 2>&1 || true

log "serial log: $OUT"
grep -E "nvme-m1|nvme-rust|CAP=" "$OUT" || true

if grep -q "nvme-rust: model=" "$OUT"; then
    log "PASS: controller identified"
    exit 0
fi
log "FAIL: driver did not report controller identity"
tail -40 "$OUT"
exit 1