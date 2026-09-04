#include "io.h"
#include "keyboard.h"
#include "fs.h"

extern void shell_run(void);

void main(void) {
    terminal_init();
    keyboard_init();
    fs_init();
    
    terminal_setcolor(VGA_COLOR_LIGHT_CYAN);
    terminal_writestring("CustomOS 0.1 loaded successfully!\n");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY);
    
    shell_run();
    
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
