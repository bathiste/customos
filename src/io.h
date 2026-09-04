#ifndef IO_H
#define IO_H

#include <stddef.h>
#include <stdint.h>

/* VGA text mode */
#define VGA_MEMORY       ((volatile uint16_t*)0xB8000)
#define VGA_WIDTH        80
#define VGA_HEIGHT       25
#define VGA_DEFAULT_COLOR 0x07

/* VGA color codes */
#define VGA_COLOR_BLACK     0
#define VGA_COLOR_BLUE      1
#define VGA_COLOR_GREEN     2
#define VGA_COLOR_CYAN      3
#define VGA_COLOR_RED       4
#define VGA_COLOR_MAGENTA   5
#define VGA_COLOR_BROWN     6
#define VGA_COLOR_LIGHT_GREY 7
#define VGA_COLOR_DARK_GREY  8
#define VGA_COLOR_LIGHT_BLUE 9
#define VGA_COLOR_LIGHT_GREEN 10
#define VGA_COLOR_LIGHT_CYAN 11
#define VGA_COLOR_LIGHT_RED  12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_YELLOW    14
#define VGA_COLOR_WHITE     15

/* Terminal functions */
void terminal_init(void);
void terminal_clear(void);
void terminal_setcolor(int color);
void terminal_putchar(char c);
void terminal_write(const char* data, size_t size);
void terminal_writestring(const char* data);
void terminal_movecursor(int x, int y);
void terminal_getcursor(int* x, int* y);

/* Printf-style output */
void printf(const char* format, ...);
void print_int(int value);
void print_hex(unsigned int value);

/* Utility */
char tohex(uint8_t nibble);

#endif /* IO_H */
