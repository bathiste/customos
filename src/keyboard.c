#include "keyboard.h"
#include "io.h"
#include <stdint.h>
#include <stdbool.h>

/* PS/2 Keyboard ports */
#define KEYBOARD_DATA_PORT    0x60
#define KEYBOARD_STATUS_PORT  0x64
#define KEYBOARD_COMMAND_PORT 0x64
#define KEYBOARD_OUT_BUFFER   0x01
#define KEYBOARD_AUX_BUFFER   0x20  /* Bit 5: 1 = mouse data, 0 = keyboard data */

/* Scan code set 2 - ESC=0x76, Backspace=0x0E */
static const uint8_t scancode_to_ascii[128] = {
    /* 0x00-0x0F */  0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    /* 0x10-0x1F */ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,   'a', 's',
    /* 0x20-0x2F */ 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
    /* 0x30-0x3F */ 'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
    /* 0x40-0x4F */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0x50-0x5F */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0x60-0x6F */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0x70-0x7F */ 0,   0,   0,   0,   0,   0,   0x1B, 0,   0,   0,   0,   0,   0,   0,   0,   0,
};

static bool shift_pressed = false, ctrl_pressed = false, alt_pressed = false;
static bool caps_lock = false, extended_code = false;
static char key_buffer[KEY_BUFFER_SIZE];
static int buffer_head = 0, buffer_tail = 0;

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void buffer_put(char c) {
    int next = (buffer_head + 1) % KEY_BUFFER_SIZE;
    if (next != buffer_tail) { key_buffer[buffer_head] = c; buffer_head = next; }
}

static char buffer_get(void) {
    if (buffer_tail == buffer_head) return 0;
    return key_buffer[buffer_tail++];
}

static bool buffer_empty(void) { return buffer_head == buffer_tail; }

static char shift_char(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    if (c >= '1' && c <= '0') { /* handled inline */ }
    switch (c) {
        case '`': return '~'; case '1': return '!'; case '2': return '@'; case '3': return '#';
        case '4': return '$'; case '5': return '%'; case '6': return '^'; case '7': return '&';
        case '8': return '*'; case '9': return '('; case '0': return ')'; case '-': return '_';
        case '=': return '+'; case '[': return '{'; case ']': return '}'; case '\\': return '|';
        case ';': return ':'; case '\'': return '"'; case ',': return '<'; case '.': return '>';
        case '/': return '?'; default: return c;
    }
}

static void keyboard_handler(uint8_t scancode) {
    if (scancode == 0xE0) { extended_code = true; return; }
    
    bool released = (scancode & 0x80) != 0;
    uint8_t code = scancode & 0x7F;
    
    switch (code) {
        case 0x2A: case 0x36: shift_pressed = !released; return;
        case 0x1D: ctrl_pressed = !released; return;
        case 0x38: alt_pressed = !released; return;
        case 0x3A: if (!released) caps_lock = !caps_lock; return;
    }
    
    if (released) return;
    
    if (extended_code) {
        extended_code = false;
        switch (code) {
            case 0x4B: buffer_put(KEY_LEFT);  return;
            case 0x4D: buffer_put(KEY_RIGHT); return;
            case 0x48: buffer_put(KEY_UP);    return;
            case 0x50: buffer_put(KEY_DOWN);  return;
            case 0x47: buffer_put(KEY_HOME);  return;
            case 0x4F: buffer_put(KEY_END);   return;
            case 0x53: buffer_put(KEY_DEL);   return;
            default: return;
        }
    }
    
    if (code < 128 && scancode_to_ascii[code] != 0) {
        char c = scancode_to_ascii[code];
        if (shift_pressed || caps_lock) c = shift_char(c);
        if (ctrl_pressed && c >= 'a' && c <= 'z') c = c - 'a' + 1;
        buffer_put(c);
    }
}

void keyboard_init(void) {
    buffer_head = buffer_tail = shift_pressed = ctrl_pressed = alt_pressed = caps_lock = extended_code = false;
    outb(KEYBOARD_COMMAND_PORT, 0xAE);
    (void)inb(KEYBOARD_DATA_PORT);
    outb(KEYBOARD_DATA_PORT, 0xF4);
    (void)inb(KEYBOARD_DATA_PORT);
}

int keyboard_has_input(void) {
    /* Drain any pending PS/2 data and route to keyboard or mouse */
    uint8_t status = inb(KEYBOARD_STATUS_PORT);
    if (status & KEYBOARD_OUT_BUFFER) {
        if (status & KEYBOARD_AUX_BUFFER) {
            /* Mouse data - forward to mouse handler */
            uint8_t data = inb(KEYBOARD_DATA_PORT);
            extern void mouse_handle_byte(uint8_t byte);
            mouse_handle_byte(data);
        } else {
            keyboard_handler(inb(KEYBOARD_DATA_PORT));
        }
    }
    return !buffer_empty();
}

char keyboard_getchar(void) {
    while (buffer_empty()) {
        uint8_t status = inb(KEYBOARD_STATUS_PORT);
        if (status & KEYBOARD_OUT_BUFFER) {
            if (status & KEYBOARD_AUX_BUFFER) {
                /* Mouse data - forward to mouse handler */
                uint8_t data = inb(KEYBOARD_DATA_PORT);
                extern void mouse_handle_byte(uint8_t byte);
                mouse_handle_byte(data);
            } else {
                keyboard_handler(inb(KEYBOARD_DATA_PORT));
            }
        }
    }
    return buffer_get();
}

int keyboard_shift_pressed(void) { return shift_pressed ? 1 : 0; }
int keyboard_ctrl_pressed(void)  { return ctrl_pressed ? 1 : 0; }
int keyboard_alt_pressed(void)   { return alt_pressed ? 1 : 0; }
