#ifndef GUI_H
#define GUI_H

#include <stdint.h>

/* Simple 2D shapes for the GUI */

/* Shape types */
#define SHAPE_RECT    0
#define SHAPE_CIRCLE  1
#define SHAPE_LINE    2

/* Color definitions (VGA colors) */
#define COLOR_BLACK       0x00
#define COLOR_BLUE        0x01
#define COLOR_GREEN       0x02
#define COLOR_CYAN        0x03
#define COLOR_RED         0x04
#define COLOR_MAGENTA     0x05
#define COLOR_BROWN       0x06
#define COLOR_LIGHT_GREY  0x07
#define COLOR_DARK_GREY   0x08
#define COLOR_LIGHT_BLUE  0x09
#define COLOR_LIGHT_GREEN 0x0A
#define COLOR_LIGHT_CYAN  0x0B
#define COLOR_LIGHT_RED   0x0C
#define COLOR_LIGHT_MAGENTA 0x0D
#define COLOR_YELLOW      0x0E
#define COLOR_WHITE       0x0F

/* Point structure */
typedef struct {
    int x;
    int y;
} Point;

/* Rectangle structure */
typedef struct {
    int x;
    int y;
    int width;
    int height;
    uint8_t color;
    uint8_t filled;
} Rect;

/* Circle structure */
typedef struct {
    int x;
    int y;
    int radius;
    uint8_t color;
    uint8_t filled;
} Circle;

/* Line structure */
typedef struct {
    int x1;
    int y1;
    int x2;
    int y2;
    uint8_t color;
} Line;

/* GUI initialization */
void gui_init(void);
void gui_clear(uint8_t color);

/* Drawing functions */
void draw_pixel(int x, int y, uint8_t color);
uint8_t get_pixel(int x, int y);

void draw_rect(int x, int y, int w, int h, uint8_t color);
void draw_rect_filled(int x, int y, int w, int h, uint8_t color);

void draw_circle(int x, int y, int radius, uint8_t color);
void draw_circle_filled(int x, int y, int radius, uint8_t color);

void draw_line(int x1, int y1, int x2, int y2, uint8_t color);
void draw_horizontal_line(int x, int y, int width, uint8_t color);
void draw_vertical_line(int x, int y, int height, uint8_t color);

/* Simple animation demo */
void gui_demo(void);

/* Update display (copy buffer to screen) */
void gui_update(void);

/* Simple delay */
void gui_delay(int ms);

#endif
