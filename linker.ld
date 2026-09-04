OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386:intel)

ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text : {
        *(.multiboot)
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.note.GNU-stack)
        *(.comment)
    }
}
