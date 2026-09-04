#!/bin/bash
pkill -9 qemu-system-i386 2>/dev/null
sleep 1
rm -f /tmp/qemu_serial.log
qemu-system-i386 -cdrom /home/bathist/customos/tries/1/customos.iso -boot d -m 128 -vga std -serial file:/tmp/qemu_serial.log -display none -netdev user,id=net0 -device virtio-net-pci,netdev=net0 &
QPID=$!
sleep 15
kill $QPID 2>/dev/null
wait $QPID 2>/dev/null
echo '=== SERIAL LOG ==='
cat /tmp/qemu_serial.log
