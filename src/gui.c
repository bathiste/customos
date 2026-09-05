#include "gui.h"
#include "io.h"
#include "keyboard.h"
#include "mouse.h"
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
            framebuffer[(y+j)*80 + (x+i)] = ((uint16_t)color << 8) | ' ';
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
    
    /* Button layout: 6 buttons in a row */
    #define NUM_BUTTONS 6
    int btn_w = 12;
    int btn_h = 3;
    int btn_y = 19;
    int btn_x_start = 2;
    int btn_spacing = 1;
    uint8_t btn_colors[NUM_BUTTONS] = {
        COLOR_RED, COLOR_GREEN, COLOR_BLUE,
        COLOR_YELLOW, COLOR_CYAN, COLOR_LIGHT_MAGENTA
    };
    const char* btn_labels[NUM_BUTTONS] = {
        "RED", "GREEN", "BLUE", "YELLOW", "CYAN", "MAGENTA"
    };
    int pressed_btn = -1;
    int last_clicked = -1;
    int click_x = -1, click_y = -1;
    int click_happened = 0;
    int hovered_btn = -1;    /* which button the mouse is currently over */
    uint8_t bg_color = COLOR_BLACK;
    
    while (1) {
        /* Poll mouse multiple times to catch fast events */
        int poll_iter;
        for (poll_iter = 0; poll_iter < 16; poll_iter++) {
            mouse_poll();
        }
        int mx = mouse_get_x();
        int my = mouse_get_y();
        int btns = mouse_get_buttons();
        
        /* Consume any click event from the mouse driver (with position) */
        int tmp_x, tmp_y;
        if (mouse_consume_click(&tmp_x, &tmp_y)) {
            click_happened = 1;
            click_x = tmp_x;
            click_y = tmp_y;
        }
        
        /* Determine which button (if any) the mouse is currently over */
        hovered_btn = -1;
        {
            int i;
            for (i = 0; i < NUM_BUTTONS; i++) {
                int bx = btn_x_start + i * (btn_w + btn_spacing);
                if (mx >= bx && mx < bx + btn_w &&
                    my >= btn_y && my < btn_y + btn_h) {
                    hovered_btn = i;
                    break;
                }
            }
        }
        
        /* Track which button is currently being pressed (for visual feedback) */
        int left_now = (btns & 0x01) ? 1 : 0;
        if (left_now) {
            pressed_btn = hovered_btn;
        } else {
            pressed_btn = -1;
        }
        
        /* Process a click event: click anywhere to change the whole screen color */
        if (click_happened) {
            /* Find which button (if any) the click was on */
            int clicked_btn = -1;
            int i;
            for (i = 0; i < NUM_BUTTONS; i++) {
                int bx = btn_x_start + i * (btn_w + btn_spacing);
                if (click_x >= bx && click_x < bx + btn_w &&
                    click_y >= btn_y && click_y < btn_y + btn_h) {
                    clicked_btn = i;
                    break;
                }
            }
            if (clicked_btn >= 0) {
                /* Button click - use button color */
                last_clicked = clicked_btn;
                bg_color = btn_colors[clicked_btn];
            } else {
                /* Click on the colored area - use the color at the click position */
                uint8_t picked = get_pixel(click_x, click_y);
                if (picked != COLOR_BLACK) {
                    bg_color = picked;
                } else {
                    /* Map X position to one of 16 VGA colors */
                    uint8_t palette[16] = {
                        COLOR_BLACK, COLOR_BLUE, COLOR_GREEN, COLOR_CYAN,
                        COLOR_RED, COLOR_MAGENTA, COLOR_BROWN, COLOR_LIGHT_GREY,
                        COLOR_DARK_GREY, COLOR_LIGHT_BLUE, COLOR_LIGHT_GREEN, COLOR_LIGHT_CYAN,
                        COLOR_LIGHT_RED, COLOR_LIGHT_MAGENTA, COLOR_YELLOW, COLOR_WHITE
                    };
                    int idx = (click_x * 16) / 80;
                    if (idx < 0) idx = 0;
                    if (idx > 15) idx = 15;
                    bg_color = palette[idx];
                }
            }
            click_happened = 0;
        }
        
        gui_clear(bg_color);
        
        /* Title bar */
        draw_rect_filled(0, 0, 80, 1, COLOR_BLUE);
        framebuffer[35] = ((uint16_t)COLOR_LIGHT_CYAN << 8) | 'C';
        framebuffer[36] = ((uint16_t)COLOR_LIGHT_CYAN << 8) | 'u';
        framebuffer[37] = ((uint16_t)COLOR_LIGHT_CYAN << 8) | 's';
        framebuffer[38] = ((uint16_t)COLOR_LIGHT_CYAN << 8) | 't';
        framebuffer[39] = ((uint16_t)COLOR_LIGHT_CYAN << 8) | 'O';
        framebuffer[40] = ((uint16_t)COLOR_LIGHT_CYAN << 8) | 'S';
        
        /* Decorative shapes */
        draw_rect(5, 5, 15, 8, COLOR_LIGHT_GREEN);
        draw_rect_filled(25, 5, 12, 6, COLOR_RED);
        draw_circle(50, 8, 4, COLOR_YELLOW);
        draw_circle_filled(65, 8, 3, COLOR_LIGHT_CYAN);
        
        /* Lines and triangle */
        draw_line(5, 15, 75, 15, COLOR_LIGHT_MAGENTA);
        draw_line(40, 14, 40, 18, COLOR_LIGHT_GREY);
        draw_line(5, 17, 30, 11, COLOR_BROWN);
        draw_line(50, 17, 75, 11, COLOR_CYAN);
        draw_line(30, 18, 38, 13, COLOR_LIGHT_RED);
        draw_line(38, 13, 46, 18, COLOR_LIGHT_RED);
        draw_line(30, 18, 46, 18, COLOR_LIGHT_RED);
        draw_line(55, 14, 65, 14, COLOR_YELLOW);
        draw_line(60, 10, 60, 18, COLOR_YELLOW);
        draw_line(56, 12, 64, 16, COLOR_YELLOW);
        draw_line(64, 12, 56, 16, COLOR_YELLOW);
        
        /* Auto-bouncing ball */
        draw_circle_filled(anim_x, anim_y, 2, COLOR_WHITE);
        anim_x += dx;
        anim_y += dy;
        if (anim_x <= 2 || anim_x >= 77) dx = -dx;
        if (anim_y <= 2 || anim_y >= 18) dy = -dy;
        
        /* Draw the buttons */
        {
            int i;
            for (i = 0; i < NUM_BUTTONS; i++) {
                int bx = btn_x_start + i * (btn_w + btn_spacing);
                int by = btn_y;
                uint8_t fill_color = btn_colors[i];
                
                /* If button is being pressed, darken it (inset effect) */
                if (i == pressed_btn) {
                    draw_rect_filled(bx, by, btn_w, btn_h, COLOR_DARK_GREY);
                    draw_rect_filled(bx + 1, by, btn_w - 2, btn_h, fill_color);
                } else {
                    draw_rect_filled(bx, by, btn_w, btn_h, fill_color);
                }
                
                /* Button border (3D effect) */
                draw_horizontal_line(bx, by, btn_w, COLOR_WHITE);
                draw_vertical_line(bx, by, btn_h, COLOR_WHITE);
                draw_horizontal_line(bx, by + btn_h - 1, btn_w, COLOR_BLACK);
                draw_vertical_line(bx + btn_w - 1, by, btn_h, COLOR_BLACK);
                
                /* Button label - centered */
                const char* lbl = btn_labels[i];
                int lbl_len = 0;
                while (lbl[lbl_len]) lbl_len++;
                int text_col = bx + (btn_w - lbl_len) / 2;
                int text_row = by + btn_h / 2;
                /* Always use WHITE text for maximum contrast and visibility */
                uint8_t text_color = COLOR_WHITE;
                int k;
                for (k = 0; lbl[k] && (text_col + k) < 80; k++) {
                    framebuffer[text_row * 80 + text_col + k] = 
                        ((uint16_t)text_color << 8) | lbl[k];
                }
            }
        }
        
        /* Mouse ball - tracks mouse position */
        /* Draw a bigger mouse ball */
        uint8_t mouse_color = COLOR_WHITE;
        if (btns & 0x01) mouse_color = COLOR_RED;  /* Left click */
        if (btns & 0x02) mouse_color = COLOR_BLUE; /* Right click */
        if ((btns & 0x03) == 0x03) mouse_color = COLOR_LIGHT_MAGENTA;
        
        /* Outer ring (3D shadow) */
        draw_circle(mx, my, 4, COLOR_DARK_GREY);
        /* Outline */
        draw_circle(mx, my, 3, COLOR_LIGHT_GREY);
        /* Big filled ball */
        draw_circle_filled(mx, my, 2, mouse_color);
        /* Center dot */
        draw_pixel(mx, my, COLOR_BLACK);
        /* Highlight pixel */
        if (mx > 0 && my > 0)
            draw_pixel(mx - 1, my - 1, COLOR_WHITE);
        
        /* Clear row 23 first, then draw the position display */
        draw_rect_filled(0, 23, 80, 1, COLOR_DARK_GREY);
        
        /* Mouse position display - shows coords from top-left (0,0) */
        const char* pos_msg = "Mouse X,Y: ";
        int label_col = 0;
        for (x = 0; pos_msg[x] && (label_col + x) < 80; x++) {
            framebuffer[23 * 80 + label_col + x] = 
                ((uint16_t)COLOR_DARK_GREY << 8) | pos_msg[x];
        }
        int col = label_col + x;  /* column after label */
        
        /* Write X with fixed width (2 digits, zero-padded) */
        {
            int v = mx;
            char digits[3];
            digits[0] = '0' + ((v / 10) % 10);
            digits[1] = '0' + (v % 10);
            digits[2] = '\0';
            for (x = 0; digits[x] && col < 80; x++) {
                framebuffer[23 * 80 + col++] = 
                    ((uint16_t)COLOR_DARK_GREY << 8) | digits[x];
            }
        }
        
        /* Comma separator */
        if (col < 80) {
            framebuffer[23 * 80 + col++] = 
                ((uint16_t)COLOR_DARK_GREY << 8) | ',';
        }
        
        /* Write Y with fixed width (2 digits, zero-padded) */
        {
            int v = my;
            char digits[3];
            digits[0] = '0' + ((v / 10) % 10);
            digits[1] = '0' + (v % 10);
            digits[2] = '\0';
            for (x = 0; digits[x] && col < 80; x++) {
                framebuffer[23 * 80 + col++] = 
                    ((uint16_t)COLOR_DARK_GREY << 8) | digits[x];
            }
        }
        
        /* Show which button was clicked last or currently being held */
        if (pressed_btn >= 0) {
            const char* pressing_msg = "  Holding: ";
            for (x = 0; pressing_msg[x] && col < 80; x++) {
                framebuffer[23 * 80 + col++] = 
                    ((uint16_t)COLOR_DARK_GREY << 8) | pressing_msg[x];
            }
            const char* cname = btn_labels[pressed_btn];
            for (x = 0; cname[x] && col < 80; x++) {
                framebuffer[23 * 80 + col++] = 
                    ((uint16_t)COLOR_DARK_GREY << 8) | cname[x];
            }
        } else if (last_clicked >= 0) {
            const char* clicked_msg = "  BG: ";
            for (x = 0; clicked_msg[x] && col < 80; x++) {
                framebuffer[23 * 80 + col++] = 
                    ((uint16_t)COLOR_DARK_GREY << 8) | clicked_msg[x];
            }
            const char* cname = btn_labels[last_clicked];
            for (x = 0; cname[x] && col < 80; x++) {
                framebuffer[23 * 80 + col++] = 
                    ((uint16_t)COLOR_DARK_GREY << 8) | cname[x];
            }
        } else {
            const char* hint_msg = "  Click anywhere!";
            for (x = 0; hint_msg[x] && col < 80; x++) {
                framebuffer[23 * 80 + col++] = 
                    ((uint16_t)COLOR_DARK_GREY << 8) | hint_msg[x];
            }
        }
        
        /* Status bar */
        draw_rect_filled(0, 24, 80, 1, COLOR_DARK_GREY);
        const char* msg = "Click anywhere to change BG - BACKSPACE:exit";
        for (x = 0; msg[x] && (x + 2) < 80; x++) {
            framebuffer[(24 * 80) + x + 2] = 
                ((uint16_t)COLOR_DARK_GREY << 8) | (uint16_t)(uint8_t)msg[x];
        }
        
        gui_update();
        gui_delay(80);
        
        /* Check keyboard for exit */
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
