#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

/* PS/2 Mouse ports */
#define MOUSE_DATA_PORT    0x60
#define MOUSE_STATUS_PORT  0x64
#define MOUSE_COMMAND_PORT 0x64

/* Mouse status flags */
#define MOUSE_OUT_BUFFER   0x01
#define MOUSE_IN_BUFFER    0x02

/* Initialize the PS/2 mouse */
void mouse_init(void);

/* Get current mouse X coordinate (0-79) */
int mouse_get_x(void);

/* Get current mouse Y coordinate (0-24) */
int mouse_get_y(void);

/* Get mouse buttons (bit 0 = left, bit 1 = right, bit 2 = middle) */
int mouse_get_buttons(void);

/* Poll for mouse events - call regularly to keep position updated */
void mouse_poll(void);

/* Handle a byte received from the PS/2 controller (already known to be mouse data) */
void mouse_handle_byte(uint8_t byte);

/* Read and clear the click event (returns 1 if a click happened since last call) */
int mouse_consume_click(void);

#endif
