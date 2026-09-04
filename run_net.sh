#!/bin/bash
cd /home/bathist/customos/tries/1

# Run QEMU with virtio-net networking enabled.
# Our driver supports BOTH modern (virtio 1.0) and legacy register
# layouts, so no special device flag is needed.
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
