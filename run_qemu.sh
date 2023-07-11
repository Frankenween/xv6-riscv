#!/bin/bash

make all

QEMU=qemu-system-riscv64
K=kernel

if echo "$@" | grep -q "debug"; then
  make qemu-gdb
  exit 0
fi
if test -z "$CPUS"; then
  CPUS=3
fi

QEMUOPTS=("-machine" "virt" "-bios" "none" "-kernel" "$K/kernel" "-m" "128M" "-smp" "$CPUS")
QEMUOPTS+=("-global" "virtio-mmio.force-legacy=false")
QEMUOPTS+=("-drive" "file=fs.img,if=none,format=raw,id=x0")
QEMUOPTS+=("-device" "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0")

if ! echo "$@" | grep -q "graphic"; then
  QEMUOPTS+=("-nographic")
fi

$QEMU "${QEMUOPTS[@]}"
