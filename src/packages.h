#ifndef PACKAGES_H
#define PACKAGES_H

/* Package compatibility database for CustomOS.
 *
 * This is a hand-written manifest describing Linux packages that
 * CustomOS can interoperate with, the minimum version required,
 * and the ABI/protocol compatibility notes.  The 'pkg' shell
 * command reads this table at runtime.
 */

typedef struct {
    const char* name;        /* package name (e.g. "grub-pc-bin") */
    const char* version;     /* minimum version (e.g. "2.04")     */
    const char* provides;    /* what this package provides        */
    const char* conflicts;   /* incompatible package (or "none")  */
    const char* protocol;    /* ABI / interface used              */
    const char* notes;       /* short compatibility note         */
} pkg_info_t;

/* The master package list.  Keep entries sorted alphabetically. */
static const pkg_info_t package_list[] = {
    {
        "build-essential",
        "12.9",
        "gcc, g++, make, libc-dev",
        "none",
        "POSIX.1-2017, ELF",
        "Provides the C/C++ toolchain used to build CustomOS."
    },
    {
        "gcc-multilib",
        "9.3.0",
        "i686-linux-gnu-gcc",
        "none",
        "i386 System V ABI",
        "Required for 32-bit freestanding kernel compilation."
    },
    {
        "nasm",
        "2.14",
        "nasm assembler",
        "yasm (incompatible syntax)",
        "x86 real/protected mode",
        "Assembles the boot sector (boot.s) into a flat binary."
    },
    {
        "qemu-system-x86",
        "5.0",
        "i386/x86_64 emulator",
        "none",
        "QEMU machine types, virtio-1.0",
        "Runs CustomOS in a sandbox; needed for -vga std and PS/2."
    },
    {
        "grub-pc-bin",
        "2.04",
        "GRUB bootloader (i386-pc)",
        "syslinux (different loader)",
        "Multiboot 1 / Multiboot 2",
        "Builds the bootable ISO via grub-mkrescue."
    },
    {
        "xorriso",
        "1.5.2",
        "ISO 9660 / Rock Ridge builder",
        "mkisofs (deprecated fork)",
        "El Torito boot catalog",
        "Generates the hybrid ISO image from the GRUB tree."
    },
    {
        "libc6-dev-i386",
        "2.31",
        "32-bit C library headers",
        "none",
        "GNU libc, i386 ABI",
        "Headers for 32-bit userspace builds."
    },
    {
        "binutils-i686-linux-gnu",
        "2.34",
        "i686 cross-binutils",
        "none",
        "ELF32 object format",
        "Alternative assembler/linker; ld is used for final link."
    },
    {
        "gdb",
        "9.2",
        "Source-level debugger",
        "none",
        "GDB remote serial protocol",
        "Can attach to QEMU's -g port to debug the kernel."
    },
    {
        "mtools",
        "4.0.24",
        "FAT filesystem tools",
        "none",
        "FAT12/16/32",
        "Optional: used to populate disk.img with files."
    },
    {
        "git",
        "2.25",
        "Version control",
        "none",
        "Git protocol v2",
        "Tracks CustomOS source tree; commit.py uses it."
    },
    {
        "python3",
        "3.8",
        "Python interpreter",
        "python2 (different ABI)",
        "CPython 3.8 ABI",
        "Powers build scripts (commit.py, gen_keyboard.py)."
    },
    {
        "make",
        "4.2",
        "Build automation",
        "none",
        "POSIX make",
        "Drives the CustomOS build via Makefile."
    }
};

#define PACKAGE_COUNT (sizeof(package_list) / sizeof(package_list[0]))

#endif
