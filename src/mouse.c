#include "mouse.h"
#include <stdint.h>

/* PS/2 ports */
#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_COMMAND 0x64

#define PS2_OUT_BUF  0x01
#define PS2_IN_BUF   0x02

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Wait for mouse to be ready (output buffer full or input buffer empty) */
static void mouse_wait_write(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if ((inb(PS2_STATUS) & PS2_IN_BUF) == 0) return;
    }
}

static void mouse_wait_read(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if ((inb(PS2_STATUS) & PS2_OUT_BUF) != 0) return;
    }
}

/* Send command to mouse and wait for ACK (0xFA) */
static void mouse_write(uint8_t data) {
    mouse_wait_write();
    outb(PS2_COMMAND, 0xD4);  /* Tell controller we want to talk to mouse */
    mouse_wait_write();
    outb(PS2_DATA, data);
    mouse_wait_read();
    (void)inb(PS2_DATA);  /* Read ACK */
}

static uint8_t mouse_read(void) {
    mouse_wait_read();
    return inb(PS2_DATA);
}

/* Mouse state */
static int mouse_x = 40;
static int mouse_y = 12;
static int mouse_buttons = 0;
static uint8_t mouse_cycle = 0;
static int8_t mouse_dx[3];
static int8_t mouse_dy[3];

/* Enable auxiliary mouse device */
void mouse_init(void) {
    /* Enable auxiliary mouse port */
    mouse_wait_write();
    outb(PS2_COMMAND, 0xA8);
    
    /* Enable interrupts */
    mouse_wait_write();
    outb(PS2_COMMAND, 0x20);
    mouse_wait_read();
    uint8_t status = inb(PS2_DATA);
    status |= 0x02;  /* Enable IRQ12 */
    status &= ~0x20; /* Enable mouse clock */
    mouse_wait_write();
    outb(PS2_COMMAND, 0x60);
    mouse_wait_write();
    outb(PS2_DATA, status);
    
    /* Use default settings */
    mouse_write(0xF6);
    (void)mouse_read();
    
    /* Enable data reporting */
    mouse_write(0xF4);
    (void)mouse_read();
}

int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }
int mouse_get_buttons(void) { return mouse_buttons; }

/* Call this regularly to keep mouse state updated */
void mouse_poll(void) {
    /* Check if mouse data is available */
    if ((inb(PS2_STATUS) & PS2_OUT_BUF) == 0) return;
    
    uint8_t status = inb(PS2_DATA);
    
    /* First byte must have bit 3 set (sync) */
    if (mouse_cycle == 0 && !(status & 0x08)) return;
    
    /* Store byte */
    if (mouse_cycle == 0) {
        mouse_dx[0] = (int8_t)status;
    } else if (mouse_cycle == 1) {
        mouse_dx[1] = (int8_t)status;
    } else {
        mouse_dx[2] = (int8_t)status;
    }
    mouse_cycle++;
    
    if (mouse_cycle >= 3) {
        mouse_cycle = 0;
        
        /* Extract buttons */
        mouse_buttons = mouse_dx[0] & 0x07;
        
        /* Extract signed deltas */
        int dx = mouse_dx[1];
        int dy = -mouse_dx[2];  /* Y is inverted */
        
        /* Update position with bounds */
        mouse_x += dx;
        mouse_y += dy;
        
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_x > 79) mouse_x = 79;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_y > 24) mouse_y = 24;
    }
}
