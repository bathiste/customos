#include "io.h"
#include "string.h"
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

static uint8_t terminal_color;
static uint16_t terminal_row;
static uint16_t terminal_column;

volatile uint16_t* vga_memory = (volatile uint16_t*)0xB8000;

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

#define VGA_CRTC_ADDR 0x3D4
#define VGA_CRTC_DATA 0x3D5

static inline uint16_t make_vgaentry(char c, uint8_t color) {
    uint16_t ch = (uint16_t)c;
    uint16_t attr = (uint16_t)color << 8;
    return ch | attr;
}

static void terminal_scroll(void) {
    int i;
    for (i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        vga_memory[i] = vga_memory[i + VGA_WIDTH];
    }
    for (i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
        vga_memory[i] = make_vgaentry(' ', terminal_color);
    }
}

void terminal_movecursor(int x, int y) {
    terminal_column = x;
    terminal_row = y;
    /* Move the hardware cursor to the new position */
    uint16_t pos = y * VGA_WIDTH + x;
    outb(VGA_CRTC_ADDR, 14);  /* High byte */
    outb(VGA_CRTC_DATA, (uint8_t)(pos >> 8));
    outb(VGA_CRTC_ADDR, 15);  /* Low byte */
    outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
}

void terminal_getcursor(int* x, int* y) {
    *x = terminal_column;
    *y = terminal_row;
}

void terminal_init(void) {
    terminal_color = VGA_DEFAULT_COLOR;
    terminal_row = 0;
    terminal_column = 0;
    /* Clear VGA memory */
    int i;
    for (i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) {
        vga_memory[i] = make_vgaentry(' ', terminal_color);
    }
    terminal_row = 0;
    terminal_column = 0;
    /* Move the hardware cursor to (0,0) */
    terminal_movecursor(0, 0);
}

void terminal_clear(void) {
    int i;
    for (i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) {
        vga_memory[i] = make_vgaentry(' ', terminal_color);
    }
    terminal_row = 0;
    terminal_column = 0;
    terminal_movecursor(0, 0);
}

void terminal_setcolor(int color) {
    terminal_color = (uint8_t)color;
}

void terminal_putchar(char c) {
    if (c == '\b') {
        if (terminal_column > 0) {
            terminal_column--;
            vga_memory[terminal_row * VGA_WIDTH + terminal_column] = make_vgaentry(' ', terminal_color);
        }
        terminal_movecursor(terminal_column, terminal_row);
        return;
    }
    if (c == '\t') {
        terminal_column = (terminal_column + 8) & ~7;
        if (terminal_column >= VGA_WIDTH) { terminal_column = 0; terminal_row++; }
        terminal_movecursor(terminal_column, terminal_row);
        return;
    }
    if (c == '\n') {
        terminal_column = 0;
        terminal_row++;
        if (terminal_row >= VGA_HEIGHT) { terminal_scroll(); terminal_row = VGA_HEIGHT - 1; }
        terminal_movecursor(terminal_column, terminal_row);
        return;
    }
    if (c == '\r') {
        terminal_column = 0;
        terminal_movecursor(terminal_column, terminal_row);
        return;
    }
    vga_memory[terminal_row * VGA_WIDTH + terminal_column] = make_vgaentry(c, terminal_color);
    terminal_column++;
    if (terminal_column >= VGA_WIDTH) {
        terminal_column = 0;
        terminal_row++;
        if (terminal_row >= VGA_HEIGHT) { terminal_scroll(); terminal_row = VGA_HEIGHT - 1; }
    }
    terminal_movecursor(terminal_column, terminal_row);
}

void terminal_write(const char* data, size_t size) {
    size_t i;
    for (i = 0; i < size; i++) terminal_putchar(data[i]);
}

void terminal_writestring(const char* data) {
    terminal_write(data, strlen(data));
}

char tohex(uint8_t nibble) {
    if (nibble < 10) return '0' + nibble;
    return 'a' + (nibble - 10);
}

void print_int(int value) {
    char buffer[32]; int i = 0;
    if (value == 0) { terminal_putchar('0'); return; }
    if (value < 0) { terminal_putchar('-'); value = -value; }
    while (value > 0) { buffer[i++] = '0' + (value % 10); value /= 10; }
    while (i > 0) terminal_putchar(buffer[--i]);
}

void print_hex(unsigned int value) {
    char buffer[16]; int i = 0;
    terminal_putchar('0'); terminal_putchar('x');
    if (value == 0) { terminal_putchar('0'); return; }
    while (value > 0) { buffer[i++] = tohex(value & 0xF); value >>= 4; }
    while (i > 0) terminal_putchar(buffer[--i]);
}

void printf(const char* format, ...) {
    va_list args; va_start(args, format);
    const char* p = format;
    while (*p) {
        if (*p == '%') {
            p++;
            switch (*p) {
                case 's': { const char* s = va_arg(args, const char*); terminal_writestring(s ? s : "(null)"); break; }
                case 'd': case 'i': { int val = va_arg(args, int); print_int(val); break; }
                case 'x': case 'X': { unsigned int val = va_arg(args, unsigned int); print_hex(val); break; }
                case 'c': { char c = (char)va_arg(args, int); terminal_putchar(c); break; }
                case '%': terminal_putchar('%'); break;
                case '\0': goto done;
                default: terminal_putchar('%'); terminal_putchar(*p); break;
            }
        } else { terminal_putchar(*p); }
        p++;
    }
done:
    va_end(args);
}
