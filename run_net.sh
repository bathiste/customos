#!/bin/bash
cd /home/bathist/customos/tries/1

# Run QEMU with virtio-net networking enabled
# -netdev user: QEMU's built-in NAT (no root required)
# -device virtio-net-pci: para-virtualized network driver
# This gives the guest 10.0.2.15 with gateway 10.0.2.2
qemu-system-i386 \
    -cdrom customos.iso \
    -boot d \
    -m 128 \
    -vga std \
    -serial file:/tmp/qemu_serial.log \
    -display sdl \
    -no-quit \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0 \
    2>&1
