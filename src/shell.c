#include "io.h"
#include "keyboard.h"
#include "fs.h"
#include "gui.h"
#include "string.h"
#include <stdbool.h>

#define MAX_LINE 256
#define HISTORY_SIZE 16

/* Command history */
static char history[HISTORY_SIZE][MAX_LINE];
static int history_count = 0;
static int history_index = 0;

static void print_prompt(void) {
    terminal_setcolor(0x0A);
    terminal_writestring("customos");
    terminal_setcolor(0x0F);
    terminal_writestring("> ");
    terminal_setcolor(0x07);
}

static void cmd_help(void) {
    terminal_setcolor(0x0E);
    terminal_writestring("CustomOS 0.1 - Commands:\n");
    terminal_setcolor(0x07);
    terminal_writestring("  help       Show this help\n");
    terminal_writestring("  clear      Clear screen\n");
    terminal_writestring("  ls         List files\n");
    terminal_writestring("  touch <f>  Create file\n");
    terminal_writestring("  cat <f>    Display file\n");
    terminal_writestring("  edit <f>   Edit file\n");
    terminal_writestring("  rm <f>     Remove file\n");
    terminal_writestring("  mkdir <d>  Create directory\n");
    terminal_writestring("  echo <t> > <f>  Write text to file\n");
    terminal_writestring("  start-gui  Launch the graphical interface\n");
    terminal_writestring("  editkey   Test key inputs (ESC to exit)\n");
}

static void read_line(char* buf, int max) {
    int pos = 0;
    
    /* Start at the end of history */
    history_index = history_count;
    
    while (1) {
        /* Use int to handle values > 127 (arrow keys) */
        int c = (unsigned char)keyboard_getchar();
        
        /* Enter - submit command */
        if (c == '\n' || c == '\r') {
            buf[pos] = '\0';
            /* Add to history if not empty */
            if (pos > 0) {
                int i;
                for (i = 0; i < pos; i++) history[history_count % HISTORY_SIZE][i] = buf[i];
                history[history_count % HISTORY_SIZE][pos] = '\0';
                history_count++;
            }
            history_index = history_count;
            terminal_putchar('\n');
            return;
        }
        
        /* Backspace - delete character */
        else if (c == '\b') {
            if (pos > 0) {
                pos--;
                terminal_putchar('\b');
                terminal_putchar(' ');
                terminal_putchar('\b');
            }
        }
        
        /* Up arrow - previous command */
        else if (c == KEY_UP) {
            if (history_count > 0 && history_index > 0) {
                /* Clear current line */
                while (pos > 0) {
                    terminal_putchar('\b');
                    terminal_putchar(' ');
                    terminal_putchar('\b');
                    pos--;
                }
                /* Get previous command */
                history_index--;
                int i;
                for (i = 0; history[history_index % HISTORY_SIZE][i]; i++) {
                    buf[pos++] = history[history_index % HISTORY_SIZE][i];
                    terminal_putchar(buf[pos - 1]);
                }
                buf[pos] = '\0';
            }
        }
        
        /* Down arrow - next command */
        else if (c == KEY_DOWN) {
            if (history_index < history_count) {
                /* Clear current line */
                while (pos > 0) {
                    terminal_putchar('\b');
                    terminal_putchar(' ');
                    terminal_putchar('\b');
                    pos--;
                }
                /* Get next command */
                history_index++;
                if (history_index < history_count) {
                    int i;
                    for (i = 0; history[history_index % HISTORY_SIZE][i]; i++) {
                        buf[pos++] = history[history_index % HISTORY_SIZE][i];
                        terminal_putchar(buf[pos - 1]);
                    }
                }
                buf[pos] = '\0';
            }
        }
        
        /* Regular character */
        else if (c >= 32 && c < 127) {
            if (pos < max - 1) {
                buf[pos++] = c;
                terminal_putchar(c);
            }
        }
    }
}

static void cmd_touch(int argc, char** argv) {
    if (argc < 2) { terminal_writestring("Usage: touch <file>\n"); return; }
    if (create_file(argv[1]) == 0) { terminal_writestring("created: "); terminal_writestring(argv[1]); terminal_putchar('\n'); }
    else terminal_writestring("error: could not create file\n");
}

static void cmd_cat(int argc, char** argv) {
    if (argc < 2) { terminal_writestring("Usage: cat <file>\n"); return; }
    char buf[1024];
    int n = read_file(argv[1], buf, sizeof(buf));
    if (n < 0) terminal_writestring("error: file not found\n");
    else { terminal_writestring(buf); terminal_putchar('\n'); }
}

static void cmd_edit(int argc, char** argv) {
    if (argc < 2) { terminal_writestring("Usage: edit <file>\n"); return; }
    if (!file_exists(argv[1])) {
        if (create_file(argv[1]) != 0) { terminal_writestring("error: could not create file\n"); return; }
    }
    terminal_writestring("Enter text, press Ctrl+D:\n");
    char buf[MAX_FILE_SIZE];
    int pos = 0;
    while (pos < MAX_FILE_SIZE - 1) {
        char c = keyboard_getchar();
        if (c == 4) break;
        if (c == '\r') c = '\n';
        buf[pos++] = c;
        terminal_putchar(c);
    }
    buf[pos] = '\0';
    write_file(argv[1], buf, pos);
    terminal_putchar('\n');
    terminal_writestring("saved: "); terminal_writestring(argv[1]); terminal_putchar('\n');
}

static void cmd_rm(int argc, char** argv) {
    if (argc < 2) { terminal_writestring("Usage: rm <file>\n"); return; }
    if (delete_file(argv[1]) == 0) { terminal_writestring("removed: "); terminal_writestring(argv[1]); terminal_putchar('\n'); }
    else terminal_writestring("error: could not remove file\n");
}

static void cmd_mkdir(int argc, char** argv) {
    if (argc < 2) { terminal_writestring("Usage: mkdir <dir>\n"); return; }
    if (mkdir(argv[1]) == 0) { terminal_writestring("created: "); terminal_writestring(argv[1]); terminal_putchar('\n'); }
    else terminal_writestring("error: could not create directory\n");
}

static void cmd_echo(int argc, char** argv) {
    if (argc < 4 || strcmp(argv[2], ">") != 0) { terminal_writestring("Usage: echo <text> > <file>\n"); return; }
    if (write_file(argv[3], argv[1], strlen(argv[1])) == 0) { terminal_writestring("written: "); terminal_writestring(argv[3]); terminal_putchar('\n'); }
    else terminal_writestring("error: could not write file\n");
}

static void cmd_edit_keys(void) {
    terminal_setcolor(0x0E);
    terminal_writestring("\n=== Edit Keys Mode ===\n");
    terminal_setcolor(0x07);
    terminal_writestring("Press any key to see its value.\n");
    terminal_writestring("Press ESC to exit.\n\n");

    while (1) {
        int c = (unsigned char)keyboard_getchar();
        int shift = keyboard_shift_pressed();
        int ctrl = keyboard_ctrl_pressed();
        int alt = keyboard_alt_pressed();

        terminal_setcolor(0x0A);
        terminal_writestring("Key: ");
        terminal_setcolor(0x0F);

        if (c == 0x1B) {
            terminal_writestring("ESC");
        } else if (c == '\n') {
            terminal_writestring("ENTER");
        } else if (c == '\t') {
            terminal_writestring("TAB");
        } else if (c == '\b') {
            terminal_writestring("BACKSPACE");
        } else if (c >= 32 && c < 127) {
            terminal_putchar(c);
        } else if (c == 0) {
            terminal_writestring("(null)");
        } else {
            terminal_putchar('0');
            terminal_putchar('x');
            unsigned char v = (unsigned char)c;
            char h1 = "0123456789ABCDEF"[(v >> 4) & 0x0F];
            char h2 = "0123456789ABCDEF"[v & 0x0F];
            terminal_putchar(h1);
            terminal_putchar(h2);
        }

        terminal_setcolor(0x08);
        terminal_writestring("  [");
        if (shift) terminal_writestring("SHIFT ");
        if (ctrl) terminal_writestring("CTRL ");
        if (alt) terminal_writestring("ALT");
        terminal_writestring("]");

        terminal_setcolor(0x0D);
        terminal_writestring("  dec:");
        int val = (unsigned char)c;
        char dec[16];
        int d = 0;
        if (val == 0) {
            dec[d++] = '0';
        } else {
            char tmp[16];
            int t = 0;
            while (val > 0) {
                tmp[t++] = '0' + (val % 10);
                val /= 10;
            }
            while (t > 0) {
                dec[d++] = tmp[--t];
            }
        }
        dec[d] = '\0';
        terminal_writestring(dec);

        terminal_writestring("  hex:0x");
        unsigned char uv = (unsigned char)c;
        char h1 = "0123456789ABCDEF"[(uv >> 4) & 0x0F];
        char h2 = "0123456789ABCDEF"[uv & 0x0F];
        terminal_putchar(h1);
        terminal_putchar(h2);

        terminal_setcolor(0x07);
        terminal_putchar('\n');

        if (c == 0x1B) {
            terminal_setcolor(0x0E);
            terminal_writestring("\nExiting Edit Keys Mode.\n\n");
            terminal_setcolor(0x07);
            return;
        }
    }
}

static int parse_line(char* line, char** argv, int max_argc) {
    int argc = 0;
    char* p = line;
    while (*p && argc < max_argc) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) { *p = '\0'; p++; }
    }
    return argc;
}

void shell_run(void) {
    char line[MAX_LINE];
    char* argv[8];
    int argc;
    
    terminal_writestring("CustomOS 0.1 - Welcome!\n");
    terminal_writestring("Type 'help' for commands.\n\n");
    
    while (1) {
        print_prompt();
        read_line(line, sizeof(line));
        argc = parse_line(line, argv, 8);
        if (argc == 0) continue;
        
        if (!strcmp(argv[0], "help")) cmd_help();
        else if (!strcmp(argv[0], "clear")) terminal_clear();
        else if (!strcmp(argv[0], "ls")) ls("/");
        else if (!strcmp(argv[0], "touch")) cmd_touch(argc, argv);
        else if (!strcmp(argv[0], "cat")) cmd_cat(argc, argv);
        else if (!strcmp(argv[0], "edit")) cmd_edit(argc, argv);
        else if (!strcmp(argv[0], "rm")) cmd_rm(argc, argv);
        else if (!strcmp(argv[0], "mkdir")) cmd_mkdir(argc, argv);
        else if (!strcmp(argv[0], "echo")) cmd_echo(argc, argv);
        else if (!strcmp(argv[0], "start-gui")) { gui_init(); gui_demo(); }
        else if (!strcmp(argv[0], "editkey")) { cmd_edit_keys(); }
        else if (!strcmp(argv[0], "exit")) { terminal_writestring("Goodbye!\n"); return; }
        else { terminal_writestring("unknown: "); terminal_writestring(argv[0]); terminal_putchar('\n'); }
    }
}
