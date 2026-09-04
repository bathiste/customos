#!/bin/bash
cd /home/bathist/customos/tries/1
# Run QEMU and capture monitor commands
echo "start" | qemu-system-i386 -cdrom customos.iso -boot d -m 64 -vga std -serial file:/tmp/qemu_serial.log -monitor stdio -display none 2>&1 | head -50
