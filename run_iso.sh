#!/bin/bash
cd /home/bathist/customos/tries/1
# Run QEMU with:
# - no Ctrl+Alt grabbing
# - no monitor on Ctrl+Alt+2
# - serial output to file
# - quit on power button
qemu-system-i386 \
    -cdrom customos.iso \
    -boot d \
    -m 64 \
    -vga std \
    -serial file:/tmp/qemu_serial.log \
    -display sdl \
    -no-quit \
    2>&1
