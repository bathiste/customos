# CustomOS 0.1

A simple 32-bit x86 operating system built from scratch in C and Assembly.

![CustomOS Boot Screen](https://img.shields.io/badge/Status-Working-brightgreen)
![Architecture](https://img.shields.io/badge/Arch-x86-blue)
![Language](https://img.shields.io/badge/Lang-C%20%2B%20ASM-orange)

## Features

- **Multiboot-compliant kernel** boots via GRUB
- **VGA text-mode terminal** (80x25) with custom cursor and color support
- **PS/2 keyboard driver** with full scancode set 2 support
  - All printable keys
  - Shift, Ctrl, Alt modifiers
  - Arrow keys, Home, End, Delete, Page Up/Down
  - Function keys F1-F10
- **PS/2 mouse driver** with position tracking and button state
- **In-memory filesystem** with files and directories
  - Create, read, write, delete files
  - `touch`, `cat`, `edit`, `rm`, `mkdir`, `ls`, `echo` commands
- **Interactive shell** with:
  - Command history (UP/DOWN arrows)
  - Backspace editing with visual feedback
  - 16 commands built-in
- **Graphical demo mode** (`start-gui`):
  - 2D shape rendering (rectangles, circles, lines, triangles, stars)
  - Animated bouncing ball
  - Mouse position tracking with on-screen ball
  - Full 16-color VGA palette
  - Title bar, status bar

## Screenshots

The kernel boots directly into an interactive shell:
```
CustomOS 0.1 loaded successfully!
Type 'help' for commands.

customos> _
```

The GUI demo (`start-gui`) shows shapes, animations, and tracks the mouse:
- Press **BACKSPACE** to exit
- Mouse ball follows your mouse with colored states for clicks

## Commands

| Command | Description |
|---------|-------------|
| `help` | Show all commands |
| `clear` | Clear the screen |
| `ls` | List files in current directory |
| `touch <file>` | Create an empty file |
| `cat <file>` | Display file contents |
| `edit <file>` | Edit a file (Ctrl+D to save) |
| `rm <file>` | Remove a file |
| `mkdir <dir>` | Create a directory |
| `echo <text> > <file>` | Write text to a file |
| `start-gui` | Launch the graphical demo |
| `editkey` | Show key codes as you press them (ESC to exit) |
| `exit` | Exit the shell |

## Building

### Requirements
- `gcc` with 32-bit support (`gcc-multilib` on Debian/Ubuntu)
- `nasm` (Netwide Assembler)
- `qemu-system-i386` (for testing)
- `grub-mkrescue` (for creating bootable ISO)

### Build and Run
```bash
make iso    # Build bootable ISO
make run    # Build and run in QEMU
```

The ISO will be created as `customos.iso`.

## Architecture

```
src/
├── boot.s        # Multiboot header and entry point
├── kernel.c      # Main kernel initialization
├── io.c/h        # VGA terminal driver
├── keyboard.c/h  # PS/2 keyboard driver
├── mouse.c/h     # PS/2 mouse driver
├── fs.c/h        # In-memory filesystem
├── shell.c       # Interactive command shell
├── gui.c/h       # 2D graphics engine
└── string.c/h    # String utilities
```

## Technical Details

- **Boot**: GRUB loads the kernel via Multiboot specification
- **Memory**: 32-bit protected mode, kernel linked at higher address
- **Display**: VGA text mode 0x03 (80x25, 16 colors)
- **Input**: PS/2 controller polling (no interrupts yet)
- **Filesystem**: Volatile, lost on reboot (RAM-based)

## License

MIT License - feel free to learn from, modify, and distribute.

## Author

[bathiste](https://github.com/bathiste)
