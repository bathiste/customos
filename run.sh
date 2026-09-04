#!/bin/bash
cd /home/bathist/customos/tries/1
qemu-system-i386 -cdrom customos.iso -boot d -m 64 -vga std -serial file:/tmp/qemu_serial.log 2>&1
