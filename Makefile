.PHONY: all clean run iso disk

CC = gcc
AS = nasm
LD = ld
RM = rm -f

CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -fno-pie -no-pie
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld --no-pie

ISO = iso
SRC = src
BUILD = build
KERNEL = $(BUILD)/kernel.bin

OBJS = $(BUILD)/boot.o $(BUILD)/kernel.o $(BUILD)/io.o $(BUILD)/fs.o $(BUILD)/shell.o $(BUILD)/keyboard.o $(BUILD)/string.o $(BUILD)/gui.o $(BUILD)/mouse.o

all: $(KERNEL)

$(BUILD):
	mkdir -p $@

$(ISO)/boot/grub:
	mkdir -p $@

GRUB_CFG = $(ISO)/boot/grub/grub.cfg
$(GRUB_CFG): $(ISO)/boot/grub
	@echo 'set timeout=0' > $@
	@echo 'set default=0' >> $@
	@echo 'menuentry "CustomOS 0.1" {' >> $@
	@echo '    multiboot /boot/kernel.bin' >> $@
	@echo '    boot' >> $@
	@echo '}' >> $@

$(BUILD)/boot.o: $(SRC)/boot.s | $(BUILD)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/kernel.o: $(SRC)/kernel.c $(SRC)/io.h $(SRC)/fs.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/io.o: $(SRC)/io.c $(SRC)/io.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fs.o: $(SRC)/fs.c $(SRC)/fs.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/shell.o: $(SRC)/shell.c $(SRC)/io.h $(SRC)/keyboard.h $(SRC)/fs.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/keyboard.o: $(SRC)/keyboard.c $(SRC)/keyboard.h $(SRC)/io.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/string.o: $(SRC)/string.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/gui.o: $(SRC)/gui.c $(SRC)/gui.h $(SRC)/io.h $(SRC)/keyboard.h $(SRC)/string.h $(SRC)/mouse.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/mouse.o: $(SRC)/mouse.c $(SRC)/mouse.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(OBJS) | $(BUILD)
	$(LD) $(LDFLAGS) -o $@ $^

disk.img: $(GRUB_CFG)
	dd if=/dev/zero of=$@ bs=1M count=10 2>/dev/null
	@if command -v mkfs.fat >/dev/null 2>&1; then mkfs.fat -F 16 $@; elif command -v mkfs.vfat >/dev/null 2>&1; then mkfs.vfat -F 16 $@; else echo "WARNING: no mkfs.fat"; fi

iso: $(KERNEL) $(GRUB_CFG)
	mkdir -p $(ISO)/boot/grub
	cp $(KERNEL) $(ISO)/boot/
	grub-mkrescue -o customos.iso $(ISO) 2>&1

run-debug: iso
	qemu-system-i386 -cdrom customos.iso -boot d -m 64 -vga std -d int,cpu_reset -D debug.log

run: iso
	qemu-system-i386 -cdrom customos.iso -boot d -m 64 -vga std -serial file:/tmp/qemu_serial.log -display sdl

run-disk: iso disk.img
	qemu-system-i386 -cdrom customos.iso -boot d -hda disk.img -m 64 -vga std

clean:
	$(RM) -r $(BUILD) $(ISO) customos.iso disk.img
