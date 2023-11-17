#!/bin/bash

QEMU=qemu-system-riscv64
K=kernel
GDBPORT=26000

if test -z "$CPUS"; then
  CPUS=3
fi

if $QEMU -help | grep -q '^-gdb'; then
  QEMUGDB=("-gdb" "tcp::${GDBPORT}")
else
  QEMUGDB=("-s" "-p" "${GDBPORT}")
fi

QEMUOPTS=("-machine" "virt" "-bios" "none" "-kernel" "$K/kernel" "-m" "128M" "-smp" "$CPUS")
QEMUOPTS+=("-global" "virtio-mmio.force-legacy=false")
QEMUOPTS+=("-drive" "file=fs.img,if=none,format=raw,id=x0")
QEMUOPTS+=("-device" "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0")

if ! echo "$@" | grep -q "graphics"; then
  QEMUOPTS+=("-nographic")
fi

make all
if echo "$@" | grep -q "debug"; then
  echo "*** Now run 'gdb' in another window."
  $QEMU "${QEMUOPTS[@]}" -S "${QEMUGDB[@]}"
else
  $QEMU "${QEMUOPTS[@]}"
fi

