# CustomOS 0.1

A simple 32-bit x86 operating system built from scratch in C and Assembly, featuring an interactive shell, PS/2 keyboard and mouse drivers, a 2D graphics engine, and an in-memory filesystem.

![Status](https://img.shields.io/badge/Status-Working-brightgreen)
![Architecture](https://img.shields.io/badge/Arch-x86-blue)
![Language](https://img.shields.io/badge/Lang-C%20%2B%20ASM-orange)
![License](https://img.shields.io/badge/License-MIT-yellow)

## Features

### Core
- **Multiboot-compliant kernel** that boots via GRUB
- **Protected mode** x86 with a custom linker script
- **32-bit C kernel** with freestanding libc (no host OS dependencies)

### Display
- **VGA text-mode terminal** (80x25) with 16-color support
- **Custom cursor** and smooth scrolling
- **ANSI-style escape codes** (color, clear screen)

### Input Drivers
- **PS/2 keyboard** with full scancode set 2:
  - All printable keys (a-z, 0-9, symbols)
  - Shift, Ctrl, Alt modifiers
  - Caps Lock toggle
  - Arrow keys, Home, End, Delete, Page Up/Down
  - Function keys F1-F10
  - Buffered input with 256-byte ring buffer
- **PS/2 mouse** with proper dual-device arbitration:
  - 3-byte packet parsing (buttons, dx, dy)
  - Button state tracking (left, right, middle)
  - Position clamping to screen bounds
  - Shared PS/2 bus correctly distinguishes keyboard vs mouse data via the AUX status bit

### Filesystem
- **In-memory filesystem** with files and directories
- Volatile (lost on reboot) but fully functional
- Hierarchical directory structure

### Shell
- **Interactive command-line** with prompt
- **Command history** (UP/DOWN arrow navigation)
- **Backspace editing** with visual feedback
- **Line buffering** with echo control
- 16+ built-in commands

### GUI Demo (`start-gui`)
- **2D shape rendering**:
  - Filled and outlined rectangles
  - Filled and outlined circles
  - Lines (Bresenham's algorithm)
  - Triangles and stars
- **Animated bouncing ball**
- **Mouse cursor**:
  - Tracks mouse position in real-time
  - White ball by default
  - **Red** on left-click
  - **Blue** on right-click
  - **Magenta** when both buttons held
  - Crosshair outline for visibility
- **16-color VGA palette**
- **Title bar** and **status bar** with live mouse coordinates

## Screenshots

Boot screen with welcome message:
```
CustomOS 0.1 loaded successfully!
Type 'help' for commands.

customos> _
```

GUI demo mode (run `start-gui`):
- Decorative shapes (rectangles, circles, triangle, star)
- Auto-bouncing white ball
- Mouse-tracking ball (changes color on click)
- Live coordinates in the status row
- Press **BACKSPACE** to return to the shell

## Commands

| Command | Description |
|---------|-------------|
| `help` | Show all available commands |
| `clear` | Clear the screen |
| `ls` | List files in the current directory |
| `cd <dir>` | Change directory |
| `pwd` | Print current working directory |
| `touch <file>` | Create an empty file |
| `cat <file>` | Display file contents |
| `edit <file>` | Edit a file (Ctrl+D to save, Ctrl+C to cancel) |
| `rm <file>` | Remove a file |
| `mkdir <dir>` | Create a directory |
| `rmdir <dir>` | Remove an empty directory |
| `echo <text> > <file>` | Write text to a file |
| `start-gui` | Launch the graphical demo |
| `editkey` | Display raw key scancodes (ESC to exit) |
| `reboot` | Reboot the system |
| `exit` | Exit the shell |

## Building

### Requirements (Debian/Ubuntu)
```bash
sudo apt install build-essential nasm qemu-system-x86 grub-pc-bin xorriso gcc-multilib
```

### Build Targets
```bash
make            # Build kernel.bin only
make iso        # Build bootable customos.iso
make run        # Build ISO and launch in QEMU
make run-debug  # Build ISO, launch QEMU, log to debug.log
make run-disk   # Run with a virtual hard disk image
make clean      # Remove all build artifacts
```

### Manual Run
If you prefer to run the ISO directly:
```bash
qemu-system-i386 -cdrom customos.iso -boot d -m 64 -vga std
```

## Architecture

```
src/
├── boot.s        # Multiboot header and assembly entry point
├── kernel.c      # Main kernel initialization
├── io.c/h        # VGA text-mode terminal driver
├── keyboard.c/h  # PS/2 keyboard driver (scancode set 2)
├── mouse.c/h     # PS/2 mouse driver (3-byte packets)
├── fs.c/h        # In-memory filesystem
├── shell.c       # Interactive command shell
├── gui.c/h       # 2D graphics engine + demo
└── string.c/h    # Freestanding string utilities
```

## Technical Details

- **Bootloader**: GRUB via Multiboot specification
- **Memory model**: 32-bit protected mode, kernel loaded at higher address
- **Linker**: Custom `linker.ld` placing kernel sections
- **Display**: VGA text mode 0x03 (80x25, 16 colors, 0xB8000 buffer)
- **Input**: PS/2 controller polling, with proper AUX bit arbitration
- **Interrupts**: Currently disabled (`cli; hlt` loop) — polling-based
- **Filesystem**: Volatile RAM-based tree
- **Compiler flags**: `-m32 -ffreestanding -fno-pie -no-pie -O2 -Wall -Wextra`

### PS/2 Driver Architecture
The keyboard and mouse share the PS/2 controller's data port (0x60). To avoid the mouse data corrupting keyboard input (which previously caused random characters to appear in the shell when the mouse moved), the driver checks the **AUX status bit** (bit 5 of port 0x64) before reading. If the bit is set, the byte came from the mouse and is forwarded to `mouse_handle_byte()`. Otherwise, it's keyboard data and goes to `keyboard_handler()`.

## License

MIT License — feel free to learn from, modify, and distribute.

## Author

[bathiste](https://github.com/bathiste)

---

*"It's not a real OS until it has a shell. Then it's barely an OS."* — ancient proverb
