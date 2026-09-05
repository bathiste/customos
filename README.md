# CustomOS 0.2

A simple 32-bit x86 operating system built from scratch in C and Assembly, featuring an interactive shell, PS/2 keyboard and mouse drivers, a 2D graphics engine, an in-memory filesystem, and a full network stack with WiFi support.

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

### Network Stack (NEW in 0.2!)
- **Virtio-Net** driver for QEMU networking
- **TCP/IP stack** with socket API
- **UDP** for connectionless communication
- **HTTP client** for web requests
- **WiFi commands** for network management:
  - Scan, connect, status, info
  - IP, gateway, DNS queries
  - Host probing
  - `curl` command for HTTP requests

### Shell
- **Interactive command-line** with prompt
- **Command history** (UP/DOWN arrow navigation)
- **Backspace editing** with visual feedback
- **Line buffering** with echo control
- 25+ built-in commands including network tools

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
CustomOS 0.2 loaded successfully!
Type 'help' for commands.

customos> _
```

WiFi network scan:
```
WiFi Network Scan
=================
Found 5 networks:
  [1] Gateway      -45 dBm  WPA2
  [6] DNS-Server   -50 dBm  WPA2
  [11] Broadcast   -60 dBm  OPEN
  [3] LocalHost    -40 dBm  OPEN
  [9] Multicast    -70 dBm  OPEN
```

## Commands

### Basic
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

### Network (NEW in 0.2!)
| Command | Description |
|---------|-------------|
| `wifi -h` | Show WiFi commands help |
| `wifi scan` | Scan for WiFi networks |
| `wifi connect <ssid> [password]` | Connect to a network |
| `wifi status` | Show current WiFi status |
| `wifi disconnect` | Disconnect from network |
| `wifi ip` | Show local IP address |
| `wifi gateway` | Show default gateway |
| `wifi dns` | Show DNS server |
| `wifi probe <ip>` | Probe a host on the network |
| `wifi info` | Show full network info |
| `curl <url>` | Fetch content from HTTP URL |
| `tcp-test <ip> <port>` | Test TCP connection |
| `udp-test <ip> <port> [msg]` | Test UDP connection |
| `http-test <ip> [path]` | Test HTTP GET request |
| `ping <ip>` | Send ICMP ping |
| `net` | Show network status |

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
make run-net    # Build ISO and launch with network (recommended)
make run-debug  # Build ISO, launch QEMU, log to debug.log
make run-disk   # Run with a virtual hard disk image
make clean      # Remove all build artifacts
```

### Manual Run
```bash
qemu-system-i386 -cdrom customos.iso -boot d -m 64 -vga std
```

### Run with Network (recommended)
```bash
make run-net
# or manually:
qemu-system-i386 -cdrom customos.iso -boot d -m 64 -vga std -netdev user,id=net0 -device virtio-net-pci,netdev=net0
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
├── string.c/h    # Freestanding string utilities
├── virtio.c/h    # Virtio block & net device driver
├── pci.c/h       # PCI bus enumeration
├── tcp.c/h       # TCP/IP protocol implementation
├── udp.c/h       # UDP protocol implementation
├── http.c/h      # HTTP client
└── wifi.c/h      # WiFi network management
```

## Technical Details

- **Bootloader**: GRUB via Multiboot specification
- **Memory model**: 32-bit protected mode, kernel loaded at higher address
- **Linker**: Custom `linker.ld` placing kernel sections
- **Display**: VGA text mode 0x03 (80x25, 16 colors, 0xB8000 buffer)
- **Input**: PS/2 controller polling, with proper AUX bit arbitration
- **Interrupts**: Currently disabled (`cli; hlt` loop) — polling-based
- **Filesystem**: Volatile RAM-based tree
- **Network**: Virtio-Net over QEMU user-mode networking (SLIRP)
- **Compiler flags**: `-m32 -ffreestanding -fno-pie -no-pie -O2 -Wall -Wextra`

### PS/2 Driver Architecture
The keyboard and mouse share the PS/2 controller's data port (0x60). To avoid the mouse data corrupting keyboard input, the driver checks the **AUX status bit** (bit 5 of port 0x64) before reading.

### Network Architecture
The network stack uses QEMU's user-mode networking (`-netdev user`) which provides:
- NAT'd internet access via the host
- Default gateway: 10.0.2.2
- DNS server: 10.0.2.3
- DHCP: automatic IP assignment (typically 10.0.2.15)

## License

MIT License — feel free to learn from, modify, and distribute.

## Author

[bathiste](https://github.com/bathiste)

---

*"It's not a real OS until it has a shell. Then it's barely an OS."* — ancient proverb
