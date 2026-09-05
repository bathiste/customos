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


/* === Static Grid System ===
 * Cells at fixed grid positions. Only redraw when value changes.
 * Much more efficient than unconditional text rewriting each frame. */
typedef struct {
    int row;
    int col;
    int width;
    char last_val[16];
    uint8_t color;
} grid_cell_t;

static void grid_init(grid_cell_t* c, int row, int col, int width, uint8_t color) {
    c->row = row;
    c->col = col;
    c->width = width;
    c->color = color;
    c->last_val[0] = 0;
}

static void grid_clear_cell(const grid_cell_t* c) {
    int k;
    for (k = 0; k < c->width && (c->col + k) < 80; k++) {
        framebuffer[c->row * 80 + c->col + k] = ((uint16_t)c->color << 8) | ' ';
    }
}

static void grid_draw_str(const grid_cell_t* c, const char* s) {
    int k;
    for (k = 0; s[k] && k < c->width && (c->col + k) < 80; k++) {
        framebuffer[c->row * 80 + c->col + k] = ((uint16_t)c->color << 8) | s[k];
    }
}

static void grid_set_str(grid_cell_t* c, const char* s) {
    if (strcmp(c->last_val, s) == 0) return;
    grid_clear_cell(c);
    grid_draw_str(c, s);
    int i;
    for (i = 0; s[i] && i < 15; i++) c->last_val[i] = s[i];
    c->last_val[i] = 0;
}

static void grid_set_int2(grid_cell_t* c, int value) {
    char buf[4];
    if (value < 0) value = 0;
    if (value > 99) value = 99;
    buf[0] = '0' + ((value / 10) % 10);
    buf[1] = '0' + (value % 10);
    buf[2] = 0;
    grid_set_str(c, buf);
}

static void grid_label(int row, int col, const char* s, uint8_t color) {
    int i;
    for (i = 0; s[i] && (col + i) < 80; i++) {
        framebuffer[row * 80 + col + i] = ((uint16_t)color << 8) | s[i];
    }
}

/* === End Static Grid System ===*/

void gui_demo(void) {
    int anim_x = 10, anim_y = 5;
    int dx = 1, dy = 1;
    
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
    int boxed_btn = -1;        /* button that currently has a colored box around it */
    int last_clicked = -1;
    int click_x = -1, click_y = -1;
    int click_happened = 0;
    int hovered_btn = -1;
    int prev_left = 0;        /* track previous left button state for click detection */
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
        
        /* Detect click using direct button state tracking (more reliable) */
        int left_now = (btns & 0x01) ? 1 : 0;
        if (prev_left == 0 && left_now == 1) {
            click_x = mx;
            click_y = my;
        }
        if (prev_left == 1 && left_now == 0) {
            click_happened = 1;
        }
        prev_left = left_now;

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
        if (left_now) {
            pressed_btn = hovered_btn;
            boxed_btn = hovered_btn;  /* show box while pressing */
        } else {
            pressed_btn = -1;
            boxed_btn = hovered_btn;  /* show box when hovering */
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
                    int band = click_x / 5;
                    if (band < 0) band = 0;
                    if (band > 15) band = 15;
                    bg_color = palette[band];
                }
            }
            click_happened = 0;
        }
        
        gui_clear(bg_color);
        
        /* Title bar: blue background with "CustomOS" centered in white at column 36 */
        draw_rect_filled(0, 0, 80, 1, COLOR_BLUE);
        {
            const char* title = "CustomOS";
            int tlen = 8;
            int tcol = 36;
            int ti;
            for (ti = 0; ti < tlen; ti++) {
                if (tcol + ti >= 0 && tcol + ti < 80) {
                    framebuffer[tcol + ti] = ((uint16_t)COLOR_WHITE << 8) | (uint8_t)title[ti];
                }
            }
        }
        
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
        
        /* Draw detection box AFTER all buttons (so it's on top) */
        if (boxed_btn >= 0 && boxed_btn < NUM_BUTTONS) {
            uint8_t box_color = btn_colors[boxed_btn];
            int box_pad = 1;
            int box_x = btn_x_start + boxed_btn * (btn_w + btn_spacing) - box_pad;
            int box_y = btn_y - box_pad;
            int box_w = btn_w + box_pad * 2;
            int box_h = btn_h + box_pad * 2;
            /* Clamp to screen bounds */
            if (box_x < 0) box_x = 0;
            if (box_y < 0) box_y = 0;
            if (box_x + box_w > 80) box_w = 80 - box_x;
            if (box_y + box_h > 25) box_h = 25 - box_y;
            /* Draw the box as a filled rectangle in the button's color (background) */
            /* Then draw the button on top of the filled area */
            draw_rect_filled(box_x, box_y, box_w, box_h, box_color);
            /* Redraw the button on top so it's visible inside the box */
            {
                int bx = btn_x_start + boxed_btn * (btn_w + btn_spacing);
                int by = btn_y;
                draw_rect_filled(bx, by, btn_w, btn_h, box_color);
                /* Redraw the white/black 3D borders */
                draw_horizontal_line(bx, by, btn_w, COLOR_WHITE);
                draw_vertical_line(bx, by, btn_h, COLOR_WHITE);
                draw_horizontal_line(bx, by + btn_h - 1, btn_w, COLOR_BLACK);
                draw_vertical_line(bx + btn_w - 1, by, btn_h, COLOR_BLACK);
                /* Redraw the label */
                const char* lbl = btn_labels[boxed_btn];
                int lbl_len = 0;
                while (lbl[lbl_len]) lbl_len++;
                int text_col = bx + (btn_w - lbl_len) / 2;
                int text_row = by + btn_h / 2;
                int k;
                for (k = 0; lbl[k] && (text_col + k) < 80; k++) {
                    framebuffer[text_row * 80 + text_col + k] =
                        ((uint16_t)COLOR_WHITE << 8) | lbl[k];
                }
            }
            /* Draw a contrasting border around the filled box using BLACK for visibility */
            draw_rect(box_x, box_y, box_w, box_h, COLOR_BLACK);
        }
        
        /* BIG Mouse cursor - a large circle that follows the mouse */
        /* This is intentionally much bigger than the QEMU window's mouse cursor
         * so you can see exactly where the OS thinks the mouse is. */
        uint8_t mouse_color = COLOR_WHITE;
        if (btns & 0x01) mouse_color = COLOR_RED;  /* Left click */
        if (btns & 0x02) mouse_color = COLOR_BLUE; /* Right click */
        if ((btns & 0x03) == 0x03) mouse_color = COLOR_LIGHT_MAGENTA;
        
        /* Outer dark ring (big circle outline) */
        draw_circle(mx, my, 8, COLOR_DARK_GREY);
        /* Middle light ring */
        draw_circle(mx, my, 7, COLOR_LIGHT_GREY);
        /* Inner colored ring */
        draw_circle(mx, my, 6, mouse_color);
        /* Filled center ball */
        draw_circle_filled(mx, my, 4, mouse_color);
        /* Center crosshair dot */
        draw_pixel(mx, my, COLOR_BLACK);
        /* Center crosshair (small + shape) */
        if (mx > 0) draw_pixel(mx - 1, my, COLOR_BLACK);
        if (mx < 79) draw_pixel(mx + 1, my, COLOR_BLACK);
        if (my > 0) draw_pixel(mx, my - 1, COLOR_BLACK);
        if (my < 24) draw_pixel(mx, my + 1, COLOR_BLACK);
        /* White highlight in upper-left */
        if (mx > 1 && my > 1)
            draw_pixel(mx - 2, my - 2, COLOR_WHITE);
        
        /* === STATIC GRID: Status Bar ===
         * Grid cells at fixed positions. Calculated once (first frame),
         * then only updated when values change. Much faster than unconditional rewrite. */
        static int grid_init_done = 0;
        static grid_cell_t cell_mx, cell_my, cell_status;
        static int last_mx = -1, last_my = -1;
        static int last_pressed = -1, last_last_clicked = -1;
        
        if (!grid_init_done) {
            draw_rect_filled(0, 23, 80, 1, COLOR_DARK_GREY);
            draw_rect_filled(0, 24, 80, 1, COLOR_DARK_GREY);
            grid_label(23, 0, "X:", COLOR_WHITE);
            grid_label(23, 3, "Y:", COLOR_WHITE);
            grid_label(23, 6, "Status:", COLOR_WHITE);
            grid_label(24, 0, "Click anywhere to change BG - BACKSPACE:exit", COLOR_WHITE);
            grid_init(&cell_mx, 23, 2, 2, COLOR_WHITE);
            grid_init(&cell_my, 23, 5, 2, COLOR_WHITE);
            grid_init(&cell_status, 23, 13, 20, COLOR_WHITE);
            grid_set_int2(&cell_mx, mx);
            grid_set_int2(&cell_my, my);
            grid_set_str(&cell_status, "Click anywhere!");
            last_mx = mx;
            last_my = my;
            last_pressed = -1;
            last_last_clicked = -1;
            grid_init_done = 1;
        }
        
        if (mx != last_mx) {
            grid_set_int2(&cell_mx, mx);
            last_mx = mx;
        }
        if (my != last_my) {
            grid_set_int2(&cell_my, my);
            last_my = my;
        }
        
        if (pressed_btn >= 0) {
            if (last_pressed != pressed_btn || last_last_clicked != -2) {
                char buf[16];
                const char* cn = btn_labels[pressed_btn];
                int i;
                buf[0] = 'H'; buf[1] = ':'; buf[2] = ' ';
                for (i = 0; cn[i] && i < 12; i++) buf[3 + i] = cn[i];
                buf[3 + i] = 0;
                grid_set_str(&cell_status, buf);
                last_pressed = pressed_btn;
                last_last_clicked = -2;
            }
        } else if (last_clicked >= 0) {
            if (last_last_clicked != last_clicked || last_pressed != -2) {
                char buf[16];
                const char* cn = btn_labels[last_clicked];
                int i;
                buf[0] = 'B'; buf[1] = ':'; buf[2] = ' ';
                for (i = 0; cn[i] && i < 12; i++) buf[3 + i] = cn[i];
                buf[3 + i] = 0;
                grid_set_str(&cell_status, buf);
                last_last_clicked = last_clicked;
                last_pressed = -2;
            }
        } else {
            if (last_pressed != -1 && last_last_clicked != -1) {
                grid_set_str(&cell_status, "Click anywhere!");
                last_pressed = -1;
                last_last_clicked = -1;
            }
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
