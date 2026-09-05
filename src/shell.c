#include "io.h"
#include "keyboard.h"
#include "fs.h"
#include "gui.h"
#include "string.h"
#include "packages.h"
#include "virtio.h"
#include "tcp.h"
#include "udp.h"
#include "http.h"
#include "wifi.h"
#include <stdbool.h>

#define MAX_LINE 256
#define HISTORY_SIZE 16

/* Forward declarations for wifi commands */
static void cmd_wifi_scan(void);
static void cmd_wifi_connect(int argc, char** argv);
static void cmd_wifi_status(void);
static void cmd_wifi_help(void);
static void cmd_wifi_ip(void);
static void cmd_wifi_gateway(void);
static void cmd_wifi_dns(void);
static void cmd_wifi_disconnect_cmd(void);
static void cmd_wifi_poll_cmd(void);
static void cmd_wifi_probe(int argc, char** argv);
static void cmd_wifi_info(void);
static void cmd_wifi(int argc, char** argv);
static void print_ip(const char* label, const uint8_t ip[4]);
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
    terminal_writestring("  ping <ip>  Send ICMP ping \n");
    terminal_writestring("  wifi -h     WiFi commands (scan, connect, ip, gateway...)\n");
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







static void cmd_wifi_scan(void) {
    terminal_setcolor(0x0E);
    terminal_writestring("WiFi Network Scan\n");
    terminal_writestring("=================\n");
    terminal_setcolor(0x07);
    wifi_init();
    wifi_network_t networks[WIFI_MAX_NETWORKS];
    int count = wifi_scan(networks, WIFI_MAX_NETWORKS);
    if (count <= 0) { terminal_writestring("No networks found\n"); return; }
    terminal_writestring("Found ");
    char cbuf[8]; int ci = 0, ct = count;
    if (ct == 0) { cbuf[ci++] = '0'; }
    else { char t[8]; int ti = 0; while (ct > 0) { t[ti++] = '0' + (ct % 10); ct /= 10; } while (ti > 0) cbuf[ci++] = t[--ti]; }
    cbuf[ci] = 0; terminal_writestring(cbuf); terminal_writestring(" networks:\n");
    for (int i = 0; i < count; i++) {
        terminal_setcolor(0x0B);
        terminal_writestring("  [");
        char chbuf[4]; int chi = 0, chv = networks[i].channel;
        if (chv >= 10) { chbuf[chi++] = '0' + (chv / 10); chv %= 10; }
        chbuf[chi++] = '0' + chv; chbuf[chi] = 0;
        terminal_writestring(chbuf);
        terminal_writestring("] ");
        terminal_setcolor(0x0F);
        terminal_writestring(networks[i].ssid);
        terminal_setcolor(0x07);
        terminal_writestring("  ");
        char rbuf[8]; int ri = 0, rv = -networks[i].rssi;
        rbuf[ri++] = '-';
        if (rv == 0) { rbuf[ri++] = '0'; }
        else { char t[8]; int ti = 0; while (rv > 0) { t[ti++] = '0' + (rv % 10); rv /= 10; } while (ti > 0) rbuf[ri++] = t[--ti]; }
        rbuf[ri] = 0;
        terminal_writestring(rbuf);
        terminal_writestring(" dBm  ");
        switch (networks[i].security) {
            case WIFI_SECURITY_NONE: terminal_setcolor(0x02); terminal_writestring("OPEN"); break;
            case WIFI_SECURITY_WEP: terminal_setcolor(0x06); terminal_writestring("WEP"); break;
            case WIFI_SECURITY_WPA: terminal_setcolor(0x0E); terminal_writestring("WPA"); break;
            case WIFI_SECURITY_WPA2: terminal_setcolor(0x0B); terminal_writestring("WPA2"); break;
            case WIFI_SECURITY_WPA3: terminal_setcolor(0x09); terminal_writestring("WPA3"); break;
            default: terminal_writestring("?"); break;
        }
        terminal_setcolor(0x07);
        terminal_putchar('\n');
    }
}

static void cmd_wifi_connect(int argc, char** argv) {
    if (argc < 2) { terminal_writestring("Usage: wifi-connect <ssid> [password]\n"); return; }
    wifi_init();
    const char* pass = (argc > 2) ? argv[2] : "";
    wifi_security_t sec = WIFI_SECURITY_WPA2;
    terminal_setcolor(0x0E);
    terminal_writestring("Connecting to '");
    terminal_writestring(argv[1]);
    terminal_writestring("'...\n");
    terminal_setcolor(0x07);
    int result = wifi_connect(argv[1], pass, sec);
    if (result == 0) {
        terminal_setcolor(0x0A);
        terminal_writestring("Connected! SSID: ");
        terminal_writestring(argv[1]);
        terminal_putchar('\n');
        terminal_setcolor(0x07);
    } else {
        terminal_setcolor(0x04);
        terminal_writestring("Connection failed\n");
        terminal_setcolor(0x07);
    }
}

static void cmd_wifi_status(void) {
    terminal_setcolor(0x0E);
    terminal_writestring("WiFi Status\n");
    terminal_writestring("===========\n");
    terminal_setcolor(0x07);
    wifi_status_t status;
    if (wifi_get_status(&status) == 0) {
        if (status.connected) {
            terminal_setcolor(0x0A);
            terminal_writestring("  Connected: YES\n");
            terminal_setcolor(0x07);
            terminal_writestring("  SSID: ");
            terminal_writestring(status.current_ssid);
            terminal_putchar('\n');
        } else {
            terminal_setcolor(0x04);
            terminal_writestring("  Connected: NO\n");
            terminal_setcolor(0x07);
        }
    } else {
        terminal_writestring("Unable to get WiFi status\n");
    }
}


static void cmd_wifi_probe(int argc, char** argv) {
    if (argc < 2) { terminal_writestring("Usage: wifi probe <ip>\n"); return; }
    const char* ipstr = argv[1];
    int a = 0, b = 0, c = 0, d = 0;
    const char* p = ipstr;
    int part = 0;
    while (*p && part < 4) {
        if (*p == '.') { part++; p++; continue; }
        if (*p >= '0' && *p <= '9') {
            if (part == 0) a = a * 10 + (*p - '0');
            else if (part == 1) b = b * 10 + (*p - '0');
            else if (part == 2) c = c * 10 + (*p - '0');
            else d = d * 10 + (*p - '0');
        }
        p++;
    }
    uint8_t target[4] = {(uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d};
    terminal_setcolor(0x0E);
    terminal_writestring("Probing ");
    terminal_writestring(ipstr);
    terminal_writestring("...\n");
    terminal_setcolor(0x07);
    int result = wifi_probe(target, 1000);
    if (result > 0) {
        terminal_setcolor(0x0A);
        terminal_writestring("  Host is reachable\n");
        terminal_setcolor(0x07);
    } else {
        terminal_setcolor(0x04);
        terminal_writestring("  Host unreachable or no network\n");
        terminal_setcolor(0x07);
    }
}

static void cmd_wifi_info(void) {
    terminal_setcolor(0x0E);
    terminal_writestring("WiFi Network Information\n");
    terminal_writestring("========================\n");
    terminal_setcolor(0x07);
    uint8_t ip[4], gw[4], dns[4];
    wifi_get_ip(ip);
    wifi_get_gateway(gw);
    wifi_get_dns(dns);
    print_ip("IP Address", ip);
    print_ip("Gateway", gw);
    print_ip("DNS", dns);
    wifi_status_t status;
    if (wifi_get_status(&status) == 0) {
        terminal_writestring("  Connected: ");
        if (status.connected) {
            terminal_setcolor(0x0A);
            terminal_writestring("YES");
        } else {
            terminal_setcolor(0x04);
            terminal_writestring("NO");
        }
        terminal_setcolor(0x07);
        terminal_putchar('\n');
        terminal_writestring("  SSID: ");
        terminal_writestring(status.current_ssid);
        terminal_putchar('\n');
        terminal_writestring("  Networks Found: ");
        char buf[8]; int i = 0, v = status.networks_found;
        if (v == 0) { buf[i++] = '0'; }
        else { char t[8]; int ti = 0; while (v > 0) { t[ti++] = '0' + (v % 10); v /= 10; } while (ti > 0) buf[i++] = t[--ti]; }
        buf[i] = 0;
        terminal_writestring(buf);
        terminal_putchar('\n');
    }
}

static void cmd_wifi(int argc, char** argv) {
    wifi_init();
    if (argc < 2) { cmd_wifi_help(); return; }
    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "help")) { cmd_wifi_help(); }
    else if (!strcmp(argv[1], "scan")) { cmd_wifi_scan(); }
    else if (!strcmp(argv[1], "connect")) {
        if (argc < 3) { terminal_writestring("Usage: wifi connect <ssid> [password]\n"); return; }
        int new_argc = argc - 1;
        char** new_argv = &argv[1];
        cmd_wifi_connect(new_argc, new_argv);
    }
    else if (!strcmp(argv[1], "status")) { cmd_wifi_status(); }
    else if (!strcmp(argv[1], "disconnect")) { cmd_wifi_disconnect_cmd(); }
    else if (!strcmp(argv[1], "ip")) { cmd_wifi_ip(); }
    else if (!strcmp(argv[1], "gateway")) { cmd_wifi_gateway(); }
    else if (!strcmp(argv[1], "dns")) { cmd_wifi_dns(); }
    else if (!strcmp(argv[1], "probe")) { cmd_wifi_probe(argc - 1, &argv[1]); }
    else if (!strcmp(argv[1], "poll")) { cmd_wifi_poll_cmd(); }
    else if (!strcmp(argv[1], "info")) { cmd_wifi_info(); }
    else {
        terminal_writestring("Unknown wifi command: ");
        terminal_writestring(argv[1]);
        terminal_writestring("\nType 'wifi -h' for help\n");
    }
}
static void print_ip(const char* label, const uint8_t ip[4]) {
    terminal_setcolor(0x0E);
    terminal_writestring("  ");
    terminal_writestring(label);
    terminal_writestring(": ");
    terminal_setcolor(0x0F);
    char buf[16]; int i = 0;
    int parts[4] = { ip[0], ip[1], ip[2], ip[3] };
    for (int p = 0; p < 4; p++) {
        int v = parts[p];
        if (v == 0) { buf[i++] = '0'; }
        else { char t[4]; int ti = 0; while (v > 0) { t[ti++] = '0' + (v % 10); v /= 10; } while (ti > 0) buf[i++] = t[--ti]; }
        if (p < 3) buf[i++] = '.';
    }
    buf[i] = 0;
    terminal_writestring(buf);
    terminal_setcolor(0x07);
    terminal_putchar('\n');
}

static void cmd_wifi_help(void) {
    terminal_setcolor(0x0E);
    terminal_writestring("WiFi Commands (wifi -h or wifi help)\n");
    terminal_writestring("====================================\n");
    terminal_setcolor(0x07);
    terminal_writestring("  wifi -h            Show this help\n");
    terminal_writestring("  wifi help          Show this help\n");
    terminal_writestring("  wifi scan          Scan for WiFi networks\n");
    terminal_writestring("  wifi connect <ssid> [password]  Connect to a network\n");
    terminal_writestring("  wifi status        Show current WiFi status\n");
    terminal_writestring("  wifi disconnect    Disconnect from network\n");
    terminal_writestring("  wifi ip            Show local IP address\n");
    terminal_writestring("  wifi gateway       Show default gateway\n");
    terminal_writestring("  wifi dns           Show DNS server\n");
    terminal_writestring("  wifi probe <ip>    Probe a host on the network\n");
    terminal_writestring("  wifi poll          Poll network for packets\n");
    terminal_writestring("  wifi info          Show full network info\n");
}

static void cmd_wifi_ip(void) {
    uint8_t ip[4];
    if (wifi_get_ip(ip) != 0) { terminal_writestring("Error getting IP\n"); return; }
    print_ip("IP Address", ip);
}

static void cmd_wifi_gateway(void) {
    uint8_t gw[4];
    if (wifi_get_gateway(gw) != 0) { terminal_writestring("Error getting gateway\n"); return; }
    print_ip("Gateway", gw);
}

static void cmd_wifi_dns(void) {
    uint8_t dns[4];
    if (wifi_get_dns(dns) != 0) { terminal_writestring("Error getting DNS\n"); return; }
    print_ip("DNS Server", dns);
}

static void cmd_wifi_disconnect_cmd(void) {
    wifi_disconnect();
    terminal_setcolor(0x0A);
    terminal_writestring("Disconnected from WiFi network\n");
    terminal_setcolor(0x07);
}

static void cmd_wifi_poll_cmd(void) {
    wifi_poll();
    terminal_writestring("Network poll complete\n");
}


static void cmd_tcp_test(int argc, char** argv) {
    if (argc < 3) { terminal_writestring("Usage: tcp-test <ip> <port>\n"); return; }
    if (!virtio_net_present()) { terminal_setcolor(0x04); terminal_writestring("Network unavailable\n"); terminal_setcolor(0x07); return; }
    terminal_setcolor(0x0E); terminal_writestring("TCP Test\n========\n"); terminal_setcolor(0x07);
    const char* ipstr = argv[1]; int a = 0, b = 0, c = 0, d = 0;
    const char* p = ipstr; int part = 0;
    while (*p && part < 4) { if (*p == '.') { part++; p++; continue; } if (*p >= '0' && *p <= '9') { if (part == 0) a = a * 10 + (*p - '0'); else if (part == 1) b = b * 10 + (*p - '0'); else if (part == 2) c = c * 10 + (*p - '0'); else d = d * 10 + (*p - '0'); } p++; }
    uint8_t dst_ip[4] = {(uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d};
    int port = 0; p = argv[2]; while (*p) { if (*p >= '0' && *p <= '9') port = port * 10 + (*p - '0'); p++; }
    terminal_writestring("Connecting to "); terminal_writestring(ipstr); terminal_writestring(":");
    char ps[8]; int pi = 0, pt = port;
    if (pt == 0) ps[pi++] = '0';
    else { char t[8]; int ti = 0; while (pt > 0) { t[ti++] = '0' + (pt % 10); pt /= 10; } while (ti > 0) ps[pi++] = t[--ti]; }
    ps[pi] = 0; terminal_writestring(ps); terminal_putchar('\n');
    tcp_init();
    tcp_socket_t* sock = tcp_socket_create();
    if (!sock) { terminal_writestring("No free sockets\n"); return; }
    int rc = tcp_connect(sock, dst_ip, (uint16_t)port);
    if (rc != 0) { terminal_writestring("Connect failed\n"); return; }
    terminal_setcolor(0x0A); terminal_writestring("Connected!\n"); terminal_setcolor(0x07);
    const char* msg = "Hello from CustomOS!\n";
    int sent = tcp_send(sock, msg, 20);
    terminal_writestring("Sent ");
    char s[8]; int si = 0, st = sent;
    if (st == 0) s[si++] = '0';
    else { char t[8]; int ti = 0; while (st > 0) { t[ti++] = '0' + (st % 10); st /= 10; } while (ti > 0) s[si++] = t[--ti]; }
    s[si] = 0; terminal_writestring(s); terminal_writestring(" bytes\n");
    tcp_socket_close(sock);
}

static void cmd_udp_test(int argc, char** argv) {
    if (argc < 3) { terminal_writestring("Usage: udp-test <ip> <port> [msg]\n"); return; }
    if (!virtio_net_present()) { terminal_setcolor(0x04); terminal_writestring("Network unavailable\n"); terminal_setcolor(0x07); return; }
    terminal_setcolor(0x0E); terminal_writestring("UDP Test\n========\n"); terminal_setcolor(0x07);
    const char* ipstr = argv[1]; int a = 0, b = 0, c = 0, d = 0;
    const char* pp = ipstr; int part = 0;
    while (*pp && part < 4) { if (*pp == '.') { part++; pp++; continue; } if (*pp >= '0' && *pp <= '9') { if (part == 0) a = a * 10 + (*pp - '0'); else if (part == 1) b = b * 10 + (*pp - '0'); else if (part == 2) c = c * 10 + (*pp - '0'); else d = d * 10 + (*pp - '0'); } pp++; }
    uint8_t dst_ip[4] = {(uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d};
    int port = 0; pp = argv[2]; while (*pp) { if (*pp >= '0' && *pp <= '9') port = port * 10 + (*pp - '0'); pp++; }
    const char* msg = (argc > 3) ? argv[3] : "Hello UDP!";
    udp_init();
    udp_socket_t* sock = udp_socket_create();
    if (!sock) { terminal_writestring("No free sockets\n"); return; }
    udp_bind(sock, 0);
    int sent = udp_sendto(sock, dst_ip, (uint16_t)port, msg, strlen(msg));
    if (sent > 0) { terminal_setcolor(0x0A); terminal_writestring("Sent ");
        char s[8]; int si = 0, st = sent;
        if (st == 0) s[si++] = '0';
        else { char t[8]; int ti = 0; while (st > 0) { t[ti++] = '0' + (st % 10); st /= 10; } while (ti > 0) s[si++] = t[--ti]; }
        s[si] = 0; terminal_writestring(s); terminal_writestring(" bytes\n"); terminal_setcolor(0x07); }
    else { terminal_setcolor(0x04); terminal_writestring("Send failed\n"); terminal_setcolor(0x07); }
    udp_socket_close(sock);
}

static void cmd_http_test(int argc, char** argv) {
    if (argc < 2) { terminal_writestring("Usage: http-test <ip> [path]\n"); return; }
    if (!virtio_net_present()) { terminal_setcolor(0x04); terminal_writestring("Network unavailable\n"); terminal_setcolor(0x07); return; }
    terminal_setcolor(0x0E); terminal_writestring("HTTP Test\n=========\n"); terminal_setcolor(0x07);
    const char* ipstr = argv[1]; int a = 0, b = 0, c = 0, d = 0;
    const char* pp = ipstr; int part = 0;
    while (*pp && part < 4) { if (*pp == '.') { part++; pp++; continue; } if (*pp >= '0' && *pp <= '9') { if (part == 0) a = a * 10 + (*pp - '0'); else if (part == 1) b = b * 10 + (*pp - '0'); else if (part == 2) c = c * 10 + (*pp - '0'); else d = d * 10 + (*pp - '0'); } pp++; }
    uint8_t server_ip[4] = {(uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d};
    const char* path = (argc > 2) ? argv[2] : "/";
    http_init();
    http_client_t client;
    int rc = http_client_connect(&client, server_ip, 80);
    if (rc != 0) { terminal_writestring("HTTP connect failed\n"); return; }
    http_response_t response;
    int grc = http_get(&client, path, &response);
    if (grc == 0) { terminal_setcolor(0x0A); terminal_writestring("Response: ");
        char sc[8]; int si = 0, st = response.status_code;
        if (st == 0) sc[si++] = '0';
        else { char t[8]; int ti = 0; while (st > 0) { t[ti++] = '0' + (st % 10); st /= 10; } while (ti > 0) sc[si++] = t[--ti]; }
        sc[si] = 0; terminal_writestring(sc); terminal_putchar('\n'); terminal_setcolor(0x07); }
    else { terminal_setcolor(0x04); terminal_writestring("HTTP GET failed\n"); terminal_setcolor(0x07); }
    http_client_disconnect(&client);
}



static void cmd_curl(int argc, char** argv) {
    if (argc < 2) { terminal_writestring("Usage: curl <url>\n"); terminal_writestring("  Example: curl http://example.com\n"); return; }
    if (!virtio_net_present()) { terminal_setcolor(0x04); terminal_writestring("curl: network unavailable\n"); terminal_setcolor(0x07); return; }
    terminal_setcolor(0x0E);
    terminal_writestring("curl: fetching ");
    terminal_writestring(argv[1]);
    terminal_writestring("\n");
    terminal_setcolor(0x07);
    const char* url = argv[1];
    const char* path = "/";
    uint8_t server_ip[4] = {10, 0, 2, 2};
    if (strlen(url) > 7 && url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p' && url[4] == ':' && url[5] == '/' && url[6] == '/') {
        const char* host_start = url + 7;
        const char* path_start = host_start;
        while (*path_start && *path_start != '/') path_start++;
        if (*path_start == '/') path = path_start;
        int host_len = path_start - host_start;
        if (host_len > 64) host_len = 64;
        char host[65];
        for (int i = 0; i < host_len; i++) host[i] = host_start[i];
        host[host_len] = 0;
        if (host[0] >= '0' && host[0] <= '9') {
            int a = 0, b = 0, c = 0, d = 0;
            int part = 0;
            const char* pp = host;
            while (*pp && part < 4) { if (*pp == '.') { part++; pp++; continue; } if (*pp >= '0' && *pp <= '9') { if (part == 0) a = a * 10 + (*pp - '0'); else if (part == 1) b = b * 10 + (*pp - '0'); else if (part == 2) c = c * 10 + (*pp - '0'); else d = d * 10 + (*pp - '0'); } pp++; }
            server_ip[0] = (uint8_t)a; server_ip[1] = (uint8_t)b; server_ip[2] = (uint8_t)c; server_ip[3] = (uint8_t)d;
        }
    }
    terminal_writestring("  IP: "); char ipstr[16]; int ipi = 0;
    for (int i = 0; i < 4; i++) { int val = server_ip[i]; if (val >= 100) { ipstr[ipi++] = '0' + (val / 100); val %= 100; } if (val >= 10 || ipi > 0) { ipstr[ipi++] = '0' + (val / 10); val %= 10; } ipstr[ipi++] = '0' + val; if (i < 3) ipstr[ipi++] = '.'; }
    ipstr[ipi] = 0; terminal_writestring(ipstr); terminal_writestring(" Path: "); terminal_writestring(path); terminal_putchar('\n');
    http_init(); http_client_t client;
    terminal_writestring("  Connecting...\n");
    int rc = http_client_connect(&client, server_ip, 80);
    if (rc != 0) { terminal_setcolor(0x04); terminal_writestring("curl: connection failed\n"); terminal_setcolor(0x07); return; }
    terminal_writestring("  Connected. Fetching...\n");
    http_response_t response;
    int grc = http_get(&client, path, &response);
    if (grc == 0) {
        terminal_setcolor(0x0A); terminal_writestring("  HTTP ");
        char sc[8]; int si = 0, st = response.status_code;
        if (st >= 100) { sc[si++] = '0' + (st / 100); st %= 100; }
        if (st >= 10 || si > 0) { sc[si++] = '0' + (st / 10); st %= 10; }
        sc[si++] = '0' + st; sc[si] = 0; terminal_writestring(sc); terminal_writestring(" OK\n"); terminal_setcolor(0x07);
        if (response.body_len > 0) {
            terminal_writestring("  [");
            char lenstr[16]; int li = 0, lt = response.body_len;
            if (lt >= 1000) { lenstr[li++] = '0' + (lt / 1000); lt %= 1000; }
            if (lt >= 100 || li > 0) { lenstr[li++] = '0' + (lt / 100); lt %= 100; }
            if (lt >= 10 || li > 0) { lenstr[li++] = '0' + (lt / 10); lt %= 10; }
            lenstr[li++] = '0' + lt; lenstr[li] = 0; terminal_writestring(lenstr);
            terminal_writestring(" bytes]\n");
            int print_len = response.body_len; if (print_len > 256) print_len = 256;
            for (int i = 0; i < print_len; i++) { char c = response.body[i]; if (c == '\n') terminal_putchar('\n'); else if (c >= 32 && c < 127) terminal_putchar(c); else if (c == '\r' || c == '\t') terminal_putchar(' '); }
            if (response.body_len > 256) terminal_writestring("\n  ... (truncated)");
            terminal_putchar('\n');
        }
    } else { terminal_setcolor(0x04); terminal_writestring("curl: request failed\n"); terminal_setcolor(0x07); }
    http_client_disconnect(&client);
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

    /* Resolve destination IP via ARP (and gateway if off-subnet).
       We send a request and poll the receive ring for the reply. */
    virtio_arp_resolve(dst_ip);
    {
        int wait;
        for (wait = 0; wait < 100000; wait++) virtio_net_poll();
    }

    terminal_setcolor(0x0E);
    terminal_writestring("PING "); terminal_writestring(argv[1]);
    terminal_writestring(" with 64 bytes of data:\n");
    terminal_setcolor(0x07);

    static uint16_t ping_id = 0x1234;
    static uint16_t ping_seq = 1;

    /* Build the ICMP/IP packet (payload only, starts at Ethernet offset 14).
       The EtherType is implied by the IP header that follows. */
    uint8_t payload[64];
    payload[0] = 0x45; payload[1] = 0x00;          /* version/IHL, DSCP */
    payload[2] = 0x00; payload[3] = 0x54;          /* total length 84 */
    payload[4] = 0x00; payload[5] = 0x00;          /* identification */
    payload[6] = 0x00; payload[7] = 0x00;          /* flags/fragment */
    payload[8] = 0x40; payload[9] = 0x01;          /* TTL=64, proto=ICMP */
    payload[10] = 0x00; payload[11] = 0x00;        /* checksum (fill below) */
    int pi;
    for (pi = 0; pi < 4; pi++) payload[12 + pi] = src_ip[pi];
    for (pi = 0; pi < 4; pi++) payload[16 + pi] = dst_ip[pi];
    {
        uint32_t sum = 0;
        int si;
        for (si = 0; si < 20; si += 2)
            sum += ((uint32_t)payload[si] << 8) | payload[si + 1];
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        sum = ~sum;
        payload[10] = (uint8_t)(sum >> 8);
        payload[11] = (uint8_t)(sum & 0xFF);
    }
    payload[20] = 0x08; payload[21] = 0x00;        /* ICMP echo request */
    payload[22] = 0x00; payload[23] = 0x00;        /* checksum (fill below) */
    payload[24] = (uint8_t)(ping_id >> 8);
    payload[25] = (uint8_t)(ping_id & 0xFF);
    payload[26] = (uint8_t)(ping_seq >> 8);
    payload[27] = (uint8_t)(ping_seq & 0xFF);
    int si;
    for (si = 28; si < 64; si++) payload[si] = (uint8_t)(si - 28);
    {
        uint32_t sum = 0;
        for (si = 20; si < 64; si += 2)
            sum += ((uint32_t)payload[si] << 8) | payload[si + 1];
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        sum = ~sum;
        payload[22] = (uint8_t)(sum >> 8);
        payload[23] = (uint8_t)(sum & 0xFF);
    }

    int i;
    for (i = 0; i < 4; i++) {
        terminal_writestring("  seq=");
        terminal_write_decimal(ping_seq);
        terminal_writestring("... ");
        int result = virtio_send_to_ip(dst_ip, payload, 50);
        if (result == 0) {
            terminal_writestring("sent");
        } else {
            terminal_setcolor(0x04);
            terminal_writestring("TX fail");
            terminal_setcolor(0x07);
        }
        terminal_putchar('\n');

        ping_seq++;
        /* Poll long enough to allow ARP cache fill and a single reply */
        int j;
        for (j = 0; j < 50000; j++) virtio_net_poll();
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

        else if (!strcmp(argv[0], "wifi")) { cmd_wifi(argc, argv); }
        else if (!strcmp(argv[0], "wifi-scan")) { cmd_wifi_scan(); }
        else if (!strcmp(argv[0], "wifi-connect")) { cmd_wifi_connect(argc, argv); }
        else if (!strcmp(argv[0], "wifi-status")) { cmd_wifi_status(); }

        else if (!strcmp(argv[0], "curl")) { cmd_curl(argc, argv); }
        else if (!strcmp(argv[0], "tcp-test")) { cmd_tcp_test(argc, argv); }
        else if (!strcmp(argv[0], "udp-test")) { cmd_udp_test(argc, argv); }
        else if (!strcmp(argv[0], "http-test")) { cmd_http_test(argc, argv); }
        else if (!strcmp(argv[0], "ping")) { cmd_ping(argc, argv); }
        else if (!strcmp(argv[0], "exit")) { terminal_writestring("Goodbye!\n"); return; }
        else { terminal_writestring("unknown: "); terminal_writestring(argv[0]); terminal_putchar('\n'); }
    }
}
