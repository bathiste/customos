#include "gui.h"
#include "io.h"
#include "keyboard.h"
#include "string.h"

/* Simple framebuffer-based GUI for VGA text mode 80x25 */

static uint16_t framebuffer[80*25];

void gui_init(void) {
    int i;
    for (i = 0; i < 80*25; i++) {
        framebuffer[i] = ((uint16_t)COLOR_BLACK << 8) | ' ';
    }
}

void gui_clear(uint8_t color) {
    int i;
    uint16_t entry = ((uint16_t)color << 8) | ' ';
    for (i = 0; i < 80*25; i++) {
        framebuffer[i] = entry;
    }
}

void draw_pixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= 80 || y < 0 || y >= 25) return;
    uint8_t ch = framebuffer[y*80+x] & 0xFF;
    framebuffer[y*80+x] = ((uint16_t)color << 8) | ch;
}

uint8_t get_pixel(int x, int y) {
    if (x < 0 || x >= 80 || y < 0 || y >= 25) return 0;
    return (framebuffer[y*80+x] >> 8) & 0xFF;
}

void draw_line(int x1, int y1, int x2, int y2, uint8_t color) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    int sx = (dx > 0) ? 1 : -1;
    int sy = (dy > 0) ? 1 : -1;
    int err;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dx > dy) {
        err = dx / 2;
        while (x1 != x2) {
            draw_pixel(x1, y1, color);
            err -= dy;
            if (err < 0) { y1 += sy; err += dx; }
            x1 += sx;
        }
    } else {
        err = dy / 2;
        while (y1 != y2) {
            draw_pixel(x1, y1, color);
            err -= dx;
            if (err < 0) { x1 += sx; err += dy; }
            y1 += sy;
        }
    }
    draw_pixel(x2, y2, color);
}

void draw_horizontal_line(int x, int y, int width, uint8_t color) {
    int i;
    for (i = 0; i < width; i++) draw_pixel(x+i, y, color);
}

void draw_vertical_line(int x, int y, int height, uint8_t color) {
    int i;
    for (i = 0; i < height; i++) draw_pixel(x, y+i, color);
}

void draw_rect(int x, int y, int w, int h, uint8_t color) {
    draw_horizontal_line(x, y, w, color);
    draw_horizontal_line(x, y+h-1, w, color);
    draw_vertical_line(x, y, h, color);
    draw_vertical_line(x+w-1, y, h, color);
}

void draw_rect_filled(int x, int y, int w, int h, uint8_t color) {
    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            draw_pixel(x+i, y+j, color);
        }
    }
}

void draw_circle(int x, int y, int radius, uint8_t color) {
    int f = 1 - radius;
    int ddF_x = 1;
    int ddF_y = -2 * radius;
    int xx = 0;
    int yy = radius;
    draw_pixel(x, y + radius, color);
    draw_pixel(x, y - radius, color);
    draw_pixel(x + radius, y, color);
    draw_pixel(x - radius, y, color);
    while (xx < yy) {
        if (f >= 0) { yy--; ddF_y += 2; f += ddF_y; }
        xx++; ddF_x += 2; f += ddF_x;
        draw_pixel(x + xx, y + yy, color);
        draw_pixel(x - xx, y + yy, color);
        draw_pixel(x + xx, y - yy, color);
        draw_pixel(x - xx, y - yy, color);
        draw_pixel(x + yy, y + xx, color);
        draw_pixel(x - yy, y + xx, color);
        draw_pixel(x + yy, y - xx, color);
        draw_pixel(x - yy, y - xx, color);
    }
}

void draw_circle_filled(int x, int y, int radius, uint8_t color) {
    int yy;
    for (yy = -radius; yy <= radius; yy++) {
        int dx_sq = radius*radius - yy*yy;
        if (dx_sq < 0) dx_sq = 0;
        int dx = 0;
        while ((dx+1)*(dx+1) <= dx_sq) dx++;
        draw_horizontal_line(x - dx, y + yy, 2*dx + 1, color);
    }
}

void gui_update(void) {
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    int i;
    for (i = 0; i < 80*25; i++) {
        vga[i] = framebuffer[i];
    }
}

void gui_delay(int ms) {
    int i, j;
    for (i = 0; i < ms; i++) {
        for (j = 0; j < 10000; j++) {
            __asm__ volatile ("nop");
        }
    }
}


void gui_demo(void) {
    int anim_x = 10, anim_y = 5;
    int dx = 1, dy = 1;
    int x;
    while (1) {
        gui_clear(COLOR_BLACK);
        draw_rect_filled(0, 0, 80, 1, COLOR_BLUE);
        framebuffer[35] = ((uint16_t)COLOR_LIGHT_CYAN << 8) | 'C';
        framebuffer[36] = ((uint16_t)COLOR_LIGHT_CYAN << 8) | 'u';
        framebuffer[37] = ((uint16_t)COLOR_LIGHT_CYAN << 8) | 's';
        framebuffer[38] = ((uint16_t)COLOR_LIGHT_CYAN << 8) | 't';
        framebuffer[39] = ((uint16_t)COLOR_LIGHT_CYAN << 8) | 'O';
        framebuffer[40] = ((uint16_t)COLOR_LIGHT_CYAN << 8) | 'S';
        draw_rect(5, 5, 15, 8, COLOR_LIGHT_GREEN);
        draw_rect_filled(25, 5, 12, 6, COLOR_RED);
        draw_circle(50, 8, 4, COLOR_YELLOW);
        draw_circle_filled(65, 8, 3, COLOR_LIGHT_CYAN);
        draw_line(5, 15, 75, 15, COLOR_LIGHT_MAGENTA);
        draw_line(40, 14, 40, 22, COLOR_LIGHT_GREY);
        draw_line(5, 20, 30, 14, COLOR_BROWN);
        draw_line(50, 20, 75, 14, COLOR_CYAN);
        draw_line(30, 22, 38, 17, COLOR_LIGHT_RED);
        draw_line(38, 17, 46, 22, COLOR_LIGHT_RED);
        draw_line(30, 22, 46, 22, COLOR_LIGHT_RED);
        draw_line(55, 18, 65, 18, COLOR_YELLOW);
        draw_line(60, 14, 60, 22, COLOR_YELLOW);
        draw_line(56, 16, 64, 20, COLOR_YELLOW);
        draw_line(64, 16, 56, 20, COLOR_YELLOW);
        draw_circle_filled(anim_x, anim_y, 2, COLOR_WHITE);
        anim_x += dx;
        anim_y += dy;
        if (anim_x <= 2 || anim_x >= 77) dx = -dx;
        if (anim_y <= 2 || anim_y >= 23) dy = -dy;
        draw_rect_filled(0, 24, 80, 1, COLOR_DARK_GREY);
        const char* msg = "CustomOS GUI v0.1 - BACKSPACE to exit";
        for (x = 0; msg[x] && (x + 2) < 80; x++) {
            framebuffer[(24 * 80) + x + 2] = 
                ((uint16_t)COLOR_DARK_GREY << 8) | (uint16_t)(uint8_t)msg[x];
        }
        gui_update();
        gui_delay(80);
        if (keyboard_has_input()) {
            int c = (unsigned char)keyboard_getchar();
            if (c == '\b') {
                gui_clear(COLOR_BLACK);
                gui_update();
                terminal_init();
                return;
            }
        }
    }
}
