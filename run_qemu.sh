#!/bin/bash


QEMU=qemu-system-riscv64
K=kernel


if test -z "$CPUS"; then
  CPUS=3
fi

if $QEMU -help | grep -q '^-gdb'; then
  QEMUGDB=("-gdb" "tcp::26000")
else
  QEMUGDB=("-s" "-p" 26000)
fi

QEMUOPTS=("-machine" "virt" "-bios" "none" "-kernel" "$K/kernel" "-m" "128M" "-smp" "$CPUS")
QEMUOPTS+=("-global" "virtio-mmio.force-legacy=false")
QEMUOPTS+=("-drive" "file=fs.img,if=none,format=raw,id=x0")
QEMUOPTS+=("-device" "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0")

if ! echo "$@" | grep -q "graphic"; then
  QEMUOPTS+=("-nographic")
fi

if echo "$@" | grep -q "debug"; then
  make qemu-gdb
  echo "*** Now run 'gdb' in another window."
  $QEMU "${QEMUOPTS[@]}" -S "${QEMUGDB[@]}"
else
  make all
  $QEMU "${QEMUOPTS[@]}"
fi

