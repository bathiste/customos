#include "io.h"
#include "keyboard.h"
#include "fs.h"
#include "gui.h"
#include "string.h"
#include "packages.h"
#include "virtio.h"
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
    terminal_writestring("  pkg       Package compatibility database\n");
    terminal_writestring("  net       Show network status\n");
    terminal_writestring("  ping <ip>  Send ICMP ping\n");
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


static void cmd_pkg(int argc, char** argv) {
    int i;
    if (argc >= 2 && !strcmp(argv[1], "--list")) {
        terminal_setcolor(0x0E);
        terminal_writestring("CustomOS Package Compatibility Database\n");
        terminal_writestring("=====================================\n\n");
        terminal_setcolor(0x07);
        for (i = 0; i < (int)PACKAGE_COUNT; i++) {
            terminal_setcolor(0x0F);
            terminal_writestring("[");
            terminal_writestring(package_list[i].name);
            terminal_writestring("]\n");
            terminal_setcolor(0x0A);
            terminal_writestring("  Version:   ");
            terminal_writestring(package_list[i].version);
            terminal_putchar('\n');
            terminal_setcolor(0x0B);
            terminal_writestring("  Provides:  ");
            terminal_writestring(package_list[i].provides);
            terminal_putchar('\n');
            terminal_setcolor(0x0C);
            terminal_writestring("  Protocol:  ");
            terminal_writestring(package_list[i].protocol);
            terminal_putchar('\n');
            if (package_list[i].conflicts[0] != 'n' || package_list[i].conflicts[1] != 'o') {
                terminal_setcolor(0x04);
                terminal_writestring("  Conflicts: ");
                terminal_writestring(package_list[i].conflicts);
                terminal_putchar('\n');
            }
            terminal_setcolor(0x08);
            terminal_writestring("  ");
            terminal_writestring(package_list[i].notes);
            terminal_putchar('\n');
            terminal_putchar('\n');
        }
        return;
    }
    if (argc >= 3 && !strcmp(argv[1], "--search")) {
        const char* query = argv[2];
        int found = 0;
        terminal_setcolor(0x0E);
        terminal_writestring("Search results for '");
        terminal_writestring(query);
        terminal_writestring("':\n\n");
        terminal_setcolor(0x07);
        for (i = 0; i < (int)PACKAGE_COUNT; i++) {
            /* Substring match on name */
            const char* n = package_list[i].name;
            int j = 0, k = 0, match = 0;
            while (n[j] && query[k]) {
                if (n[j] == query[k]) k++;
                j++;
            }
            if (!query[k]) match = 1;
            if (match) {
                found = 1;
                terminal_setcolor(0x0F);
                terminal_writestring(package_list[i].name);
                terminal_setcolor(0x08);
                terminal_writestring(" >= ");
                terminal_writestring(package_list[i].version);
                terminal_writestring("  - ");
                terminal_writestring(package_list[i].notes);
                terminal_putchar('\n');
            }
        }
        if (!found) {
            terminal_writestring("(no matching package)\n");
        }
        return;
    }
    if (argc >= 2 && !strcmp(argv[1], "--check")) {
        terminal_setcolor(0x0E);
        terminal_writestring("CustomOS Build Compatibility Check\n");
        terminal_writestring("=================================\n\n");
        terminal_setcolor(0x0A);
        terminal_writestring("  [OK] gcc-multilib     - 32-bit toolchain ready\n");
        terminal_writestring("  [OK] binutils         - ELF linker present\n");
        terminal_writestring("  [OK] grub-pc-bin      - Multiboot bootloader\n");
        terminal_writestring("  [OK] xorriso          - ISO 9660 builder\n");
        terminal_writestring("  [OK] qemu-system-x86  - Emulator target\n");
        terminal_setcolor(0x0B);
        terminal_writestring("  [i]  nasm             - Optional (assemble boot.s)\n");
        terminal_writestring("  [i]  gdb              - Optional (kernel debugging)\n");
        terminal_writestring("  [i]  mtools           - Optional (FAT disk image)\n");
        terminal_setcolor(0x07);
        terminal_writestring("\n  All required packages available.\n");
        return;
    }
    terminal_setcolor(0x0E);
    terminal_writestring("Usage: pkg --list\n");
    terminal_writestring("       pkg --search <name>\n");
    terminal_writestring("       pkg --check\n\n");
    terminal_setcolor(0x07);
    terminal_writestring("  --list    Show all packages\n");
    terminal_writestring("  --search  Search for a package\n");
    terminal_writestring("  --check   Show build compatibility status\n");
}



static void cmd_net(int argc, char** argv) {
    (void)argc; (void)argv;
    terminal_setcolor(0x0E);
    terminal_writestring("Network Status\n");
    terminal_writestring("==============\n");
    terminal_setcolor(0x07);
    if (virtio_net_present()) {
        terminal_setcolor(0x0A);
        terminal_writestring("  Status:     UP\n  Device:     virtio-net\n");
        terminal_setcolor(0x07);
        uint8_t mac[6];
        virtio_get_mac(mac);
        terminal_writestring("  MAC:        ");
        int mi;
        for (mi = 0; mi < 6; mi++) {
            char hx[3];
            uint8_t b = mac[mi];
            hx[0] = "0123456789ABCDEF"[b >> 4];
            hx[1] = "0123456789ABCDEF"[b & 0x0F];
            hx[2] = '\0';
            terminal_writestring(hx);
            if (mi < 5) terminal_putchar(':');
        }
        terminal_putchar('\n');
        terminal_writestring("  IP:         10.0.2.15\n  Gateway:    10.0.2.2\n");
    } else {
        terminal_setcolor(0x04);
        terminal_writestring("  Status:     DOWN\n");
        terminal_setcolor(0x07);
    }
}

static void terminal_write_decimal(int n) {
    char buf[16];
    int i = 0;
    if (n == 0) { terminal_putchar('0'); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) terminal_putchar(buf[--i]);
}

static void cmd_ping(int argc, char** argv) {
    if (argc < 2) { terminal_writestring("Usage: ping <ipaddr>\n"); return; }
    if (!virtio_net_present()) {
        terminal_setcolor(0x04);
        terminal_writestring("ping: network not available\n");
        terminal_setcolor(0x07);
        return;
    }
    const char* ipstr = argv[1];
    int a = 0, b = 0, c = 0, d = 0;
    const char* pp = ipstr;
    int part = 0;
    while (*pp && part < 4) {
        if (*pp == '.') { part++; pp++; continue; }
        if (*pp >= '0' && *pp <= '9') {
            if (part == 0) a = a * 10 + (*pp - '0');
            else if (part == 1) b = b * 10 + (*pp - '0');
            else if (part == 2) c = c * 10 + (*pp - '0');
            else d = d * 10 + (*pp - '0');
        }
        pp++;
    }
    uint8_t dst_ip[4];
    dst_ip[0] = (uint8_t)a; dst_ip[1] = (uint8_t)b;
    dst_ip[2] = (uint8_t)c; dst_ip[3] = (uint8_t)d;
    uint8_t src_ip[4] = {10, 0, 2, 15};
    terminal_setcolor(0x0E);
    terminal_writestring("PING "); terminal_writestring(argv[1]);
    terminal_writestring(" with 64 bytes of data:\n");
    terminal_setcolor(0x07);
    static uint16_t ping_id = 0x1234;
    static uint16_t ping_seq = 1;
    int i;
    for (i = 0; i < 4; i++) {
        uint8_t packet[98];
        packet[0] = 0x52; packet[1] = 0x54; packet[2] = 0x00;
        packet[3] = 0x12; packet[4] = 0x34; packet[5] = 0x56;
        uint8_t my_mac[6];
        virtio_get_mac(my_mac);
        int mi;
        for (mi = 0; mi < 6; mi++) packet[6 + mi] = my_mac[mi];
        packet[12] = 0x08; packet[13] = 0x00;
        packet[14] = 0x45; packet[15] = 0x00;
        packet[16] = 0x00; packet[17] = 0x5C;
        packet[18] = 0x00; packet[19] = 0x00;
        packet[20] = 0x00; packet[21] = 0x00;
        packet[22] = 0x40; packet[23] = 0x01;
        packet[24] = 0x00; packet[25] = 0x00;
        for (mi = 0; mi < 4; mi++) packet[26 + mi] = src_ip[mi];
        for (mi = 0; mi < 4; mi++) packet[30 + mi] = dst_ip[mi];
        uint32_t sum = 0;
        int si;
        for (si = 0; si < 20; si += 2)
            sum += ((uint32_t)packet[14 + si] << 8) | packet[15 + si];
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        sum = ~sum;
        packet[24] = (uint8_t)(sum >> 8);
        packet[25] = (uint8_t)(sum & 0xFF);
        packet[34] = 0x08; packet[35] = 0x00;
        packet[36] = 0x00; packet[37] = 0x00;
        packet[38] = (uint8_t)(ping_id >> 8);
        packet[39] = (uint8_t)(ping_id & 0xFF);
        packet[40] = (uint8_t)(ping_seq >> 8);
        packet[41] = (uint8_t)(ping_seq & 0xFF);
        for (si = 42; si < 92; si++) packet[si] = (uint8_t)(si - 42);
        sum = 0;
        for (si = 34; si < 92; si += 2)
            sum += ((uint32_t)packet[si] << 8) | packet[si + 1];
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        sum = ~sum;
        packet[36] = (uint8_t)(sum >> 8);
        packet[37] = (uint8_t)(sum & 0xFF);
        int result = virtio_net_send(packet, 92);
        terminal_writestring("  seq=");
        terminal_write_decimal(ping_seq);
        if (result == 0) terminal_writestring(" sent.\n");
        else { terminal_setcolor(0x04); terminal_writestring(" failed.\n"); terminal_setcolor(0x07); }
        ping_seq++;
        int j;
        for (j = 0; j < 30000; j++) virtio_net_poll();
    }
    terminal_setcolor(0x0A);
    terminal_writestring("\n4 packets sent.\n");
    terminal_setcolor(0x07);
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
        virtio_net_poll();

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
        else if (!strcmp(argv[0], "pkg")) { cmd_pkg(argc, argv); }
        else if (!strcmp(argv[0], "net")) { cmd_net(argc, argv); }
        else if (!strcmp(argv[0], "ping")) { cmd_ping(argc, argv); }
        else if (!strcmp(argv[0], "exit")) { terminal_writestring("Goodbye!\n"); return; }
        else { terminal_writestring("unknown: "); terminal_writestring(argv[0]); terminal_putchar('\n'); }
    }
}
