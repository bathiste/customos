; boot.s - Multiboot entry point for CustomOS
; NASM syntax - GRUB loads us in 32-bit protected mode

; Multiboot constants - do NOT request video mode (bit 16)
; Let GRUB keep the default 80x25 text mode
MULTIBOOT_MAGIC    equ 0x1BADB002
MULTIBOOT_FLAGS    equ 0x00000003  ; bit 0: align modules, bit 1: memory info
MULTIBOOT_CHECKSUM equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

section .multiboot
align 4
    dd MULTIBOOT_MAGIC
    dd MULTIBOOT_FLAGS
    dd MULTIBOOT_CHECKSUM

    ; a.out fields - must be present for the header to be valid
    dd 0    ; header_addr
    dd 0    ; load_addr
    dd 0    ; load_end_addr
    dd 0    ; bss_end_addr
    dd 0    ; entry_addr

    ; No video mode request - keep default 80x25 text

section .text

global _start
extern main

_start:
    ; Disable interrupts immediately
    cli

    ; Set up stack (16KB just below 1MB)
    mov esp, 0x90000

    ; Clear direction flag
    cld

    ; Call kernel main
    call main

halt_loop:
    cli
    hlt
    jmp halt_loop
