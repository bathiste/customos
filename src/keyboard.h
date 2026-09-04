#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

#define KEY_BUFFER_SIZE 256

/* Initialize keyboard */
void keyboard_init(void);

/* Get a character from the keyboard buffer (blocks until available) */
char keyboard_getchar(void);

/* Check if a character is available */
int keyboard_has_input(void);

/* Special keys */
#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83
#define KEY_HOME  0x84
#define KEY_END   0x85
#define KEY_DEL   0x86
/* Additional special keys */
#define KEY_PGUP  0x87
#define KEY_PGDN  0x88
#define KEY_F1    0x90
#define KEY_F2    0x91
#define KEY_F3    0x92
#define KEY_F4    0x93
#define KEY_F5    0x94
#define KEY_F6    0x95
#define KEY_F7    0x96
#define KEY_F8    0x97
#define KEY_F9    0x98
#define KEY_F10   0x99
#define KEY_CTRL_A 0x01
#define KEY_CTRL_A 0x01
#define KEY_CTRL_C 0x03
#define KEY_CTRL_D 0x04
#define KEY_CTRL_L 0x0C
#define KEY_TAB    0x09
#define KEY_ESC    0x1B

/* Modifier key queries */
int keyboard_shift_pressed(void);
int keyboard_ctrl_pressed(void);
int keyboard_alt_pressed(void);

#endif
