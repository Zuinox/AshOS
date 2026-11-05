// src/stage1/gui.c - Multi-window Desktop Environment, Dock, Scrollable Shell,
//                      Task Manager, System Info, and VFS File Viewer / Notes

#include "gui.h"
#include "video.h"
#include "pmm.h"
#include "heap.h"
#include "pci.h"
#include "rtc.h"
#include "timer.h"
#include "vfs.h"
#include "lib.h"

typedef void (*RenderFunc)(int x, int y, int w, int h, int is_focused);

typedef struct {
    int id;
    int x, y, w, h;
    const char* title;
    unsigned int accent_color;
    RenderFunc render;
    int is_open;
    int is_dragging;
    int drag_off_x, drag_off_y;
} Window;

#define MAX_WINDOWS 4
static Window windows[MAX_WINDOWS];
static int z_order[MAX_WINDOWS] = {0, 1, 2, 3}; // last element = top-most / focused
static unsigned char prev_mouse_buttons = 0;

// --- Shell State (Multi-line Scrollback) ---
#define SHELL_HISTORY_LINES 10
#define SHELL_LINE_LEN      68
static char shell_lines[SHELL_HISTORY_LINES][SHELL_LINE_LEN];
static int shell_line_count = 0;

static char cmd_buffer[128];
static int shell_cmd_idx = 0;

// --- Notes / File Viewer State ---
static char active_note_file[VFS_NAME_MAX] = "notes.txt";

static char to_lower_char(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static void shell_push_raw_line(const char* line) {
    if (shell_line_count < SHELL_HISTORY_LINES) {
        kstrlcpy(shell_lines[shell_line_count++], line, SHELL_LINE_LEN);
    } else {
        for (int i = 1; i < SHELL_HISTORY_LINES; i++) {
            kstrlcpy(shell_lines[i - 1], shell_lines[i], SHELL_LINE_LEN);
        }
        kstrlcpy(shell_lines[SHELL_HISTORY_LINES - 1], line, SHELL_LINE_LEN);
    }
}

static void shell_print(const char* text) {
    if (!text || !text[0]) return;
    size_t len = kstrlen(text);
    size_t pos = 0;
    const size_t wrap = 36;
    while (pos < len) {
        while (pos < len && text[pos] == ' ') pos++;
        if (pos >= len) break;
        size_t rem = len - pos;
        size_t n = rem;
        if (n > wrap) {
            n = wrap;
            // Word-wrap at last space if possible
            size_t back = n;
            while (back > 12 && text[pos + back] != ' ') back--;
            if (text[pos + back] == ' ') n = back;
        }
        char chunk[SHELL_LINE_LEN];
        for (size_t i = 0; i < n; i++) chunk[i] = text[pos + i];
        chunk[n] = '\0';
        shell_push_raw_line(chunk);
        pos += n;
    }
}

static void bring_to_front(int win_idx) {
    int pos = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (z_order[i] == win_idx) {
            pos = i;
            break;
        }
    }
    if (pos == -1) return;
    for (int i = pos; i < MAX_WINDOWS - 1; i++) {
        z_order[i] = z_order[i + 1];
    }
    z_order[MAX_WINDOWS - 1] = win_idx;
}

static int top_open_window(void) {
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        int idx = z_order[i];
        if (windows[idx].is_open) return idx;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Window 0: Interactive Shell
// ---------------------------------------------------------------------------
static void render_shell(int x, int y, int w, int h, int is_focused) {
    (void)h;
    int text_x = x + 12;
    int text_y = y + 42;

    for (int i = 0; i < shell_line_count; i++) {
        unsigned int col = (shell_lines[i][0] == '>') ? 0x66FF99 : 0xCCCCCC;
        draw_string(text_x, text_y, shell_lines[i], col, 2);
        text_y += 26;
    }

    // Current prompt
    draw_string(text_x, text_y, "> ", 0x00FF00, 2);
    int cur_x = text_x + 32;
    for (int i = 0; i < shell_cmd_idx; i++) {
        draw_char(cur_x, text_y, cmd_buffer[i], 0x00FF00, 2);
        cur_x += 16;
        if (cur_x > x + w - 28) {
            cur_x = text_x + 32;
            text_y += 24;
        }
    }
    // Blinking block cursor when Shell is focused
    if (is_focused && ((timer_ms() / 400) % 2 == 0)) {
        draw_rect(cur_x + 2, text_y, 10, 16, 0x00FF00);
    }
}

// ---------------------------------------------------------------------------
// Window 1: Task Manager (RAM + Heap + Uptime)
// ---------------------------------------------------------------------------
static void render_task_manager(int x, int y, int w, int h, int is_focused) {
    (void)h; (void)is_focused;
    draw_string(x + 12, y + 44, "Total RAM Blocks:", 0xAAAAAA, 2);
    draw_int(x + 292, y + 44, max_blocks, 0xFFFFFF, 2);

    draw_string(x + 12, y + 72, "Used RAM Blocks:", 0xAAAAAA, 2);
    draw_int(x + 292, y + 72, used_blocks, 0xFFFFFF, 2);

    int bar_w = w - 24, bar_h = 20;
    draw_string(x + 12, y + 104, "Physical Memory:", 0xAAAAAA, 2);
    draw_rect(x + 12, y + 128, bar_w, bar_h, 0x3A3A3A);
    int used_w = (max_blocks == 0) ? 0 : (int)(((unsigned long long)used_blocks * bar_w) / max_blocks);
    if (used_w > bar_w) used_w = bar_w;
    draw_rect(x + 12, y + 128, used_w, bar_h, 0xFF9900);

    unsigned int h_used = heap_used_bytes();
    unsigned int h_tot  = heap_total_bytes();
    draw_string(x + 12, y + 158, "Heap Used (B):", 0xAAAAAA, 2);
    draw_int(x + 250, y + 158, h_used, 0x55FF55, 2);

    draw_rect(x + 12, y + 182, bar_w, 14, 0x3A3A3A);
    int h_w = (h_tot == 0) ? 0 : (int)(((unsigned long long)h_used * bar_w) / h_tot);
    if (h_w > bar_w) h_w = bar_w;
    draw_rect(x + 12, y + 182, h_w, 14, 0x55FF55);

    draw_string(x + 12, y + 206, "Uptime (sec):", 0xAAAAAA, 2);
    draw_int(x + 250, y + 206, (unsigned int)(timer_ms() / 1000ULL), 0x55FFFF, 2);
}

// ---------------------------------------------------------------------------
// Window 2: System Info
// ---------------------------------------------------------------------------
static void render_sysinfo(int x, int y, int w, int h, int is_focused) {
    (void)w; (void)h; (void)is_focused;
    draw_string(x + 14, y + 46,  "OS:     SubhashishOS 64-bit", 0xFFFFFF, 2);
    draw_string(x + 14, y + 76,  "Author: Subhashish Reddy", 0x55FF55, 2);
    draw_string(x + 14, y + 106, "Boot:   Custom UEFI -> 0x1000000", 0xCCCCCC, 2);
    draw_string(x + 14, y + 136, "Paging: 4-Level (2MB HugePages)", 0xCCCCCC, 2);
    draw_string(x + 14, y + 166, "USB:    xHCI 3.0 HID Kbd+Mouse", 0xCCCCCC, 2);
    draw_string(x + 14, y + 196, "Video:  Double-Buffered GOP", 0xCCCCCC, 2);
}

// ---------------------------------------------------------------------------
// Window 3: VFS File Explorer & Notes Viewer
// ---------------------------------------------------------------------------
static void render_notes(int x, int y, int w, int h, int is_focused) {
    (void)h; (void)is_focused;
    draw_string(x + 12, y + 44, "VFS Files (click to view):", 0xAAAAAA, 2);

    int count = vfs_file_count();
    int tab_x = x + 12;
    int tab_y = y + 70;
    for (int i = 0; i < count && i < 4; i++) {
        const char* fname = 0;
        unsigned int fsize = 0;
        if (vfs_get_file_info(i, &fname, &fsize) == 0 && fname) {
            int active = (kstrcmp(fname, active_note_file) == 0);
            draw_rect(tab_x, tab_y, 118, 24, active ? 0x336633 : 0x333333);
            draw_string(tab_x + 6, tab_y + 4, fname, active ? 0xFFFF55 : 0xCCCCCC, 1);
            tab_x += 124;
        }
    }

    draw_rect(x + 12, y + 102, w - 24, 110, 0x141414);
    char content[256];
    vfs_read_file(active_note_file, content, sizeof(content));

    // Word-wrap file content across 3 lines of up to 36 chars
    size_t len = kstrlen(content);
    size_t pos = 0;
    int line_y = y + 112;
    for (int row = 0; row < 3 && pos < len; row++) {
        while (pos < len && content[pos] == ' ') pos++;
        if (pos >= len) break;
        size_t rem = len - pos;
        size_t n = rem;
        if (n > 36) {
            n = 36;
            size_t back = n;
            while (back > 12 && content[pos + back] != ' ') back--;
            if (content[pos + back] == ' ') n = back;
        }
        char line[40];
        for (size_t k = 0; k < n; k++) line[k] = content[pos + k];
        line[n] = '\0';
        draw_string(x + 18, line_y, line, 0xEEEEEE, 2);
        line_y += 28;
        pos += n;
    }
    draw_string(x + 12, y + 220, "Tip: write notes.txt <msg>", 0x888888, 2);
}

void gui_init(void) {
    vfs_init();

    windows[0].id = 0;
    windows[0].x = 24;  windows[0].y = 48;  windows[0].w = 640; windows[0].h = 370;
    windows[0].title = "SHELL";
    windows[0].accent_color = 0xFF5555;
    windows[0].render = render_shell;
    windows[0].is_open = 1;
    windows[0].is_dragging = 0;

    windows[1].id = 1;
    windows[1].x = 696; windows[1].y = 48;  windows[1].w = 556; windows[1].h = 240;
    windows[1].title = "TASK MANAGER";
    windows[1].accent_color = 0x55FF55;
    windows[1].render = render_task_manager;
    windows[1].is_open = 1;
    windows[1].is_dragging = 0;

    windows[2].id = 2;
    windows[2].x = 696; windows[2].y = 310; windows[2].w = 556; windows[2].h = 240;
    windows[2].title = "SYSTEM INFO";
    windows[2].accent_color = 0x5555FF;
    windows[2].render = render_sysinfo;
    windows[2].is_open = 1;
    windows[2].is_dragging = 0;

    windows[3].id = 3;
    windows[3].x = 24;  windows[3].y = 440; windows[3].w = 640; windows[3].h = 245;
    windows[3].title = "VFS NOTES";
    windows[3].accent_color = 0xFFFF55;
    windows[3].render = render_notes;
    windows[3].is_open = 1;
    windows[3].is_dragging = 0;

    z_order[0] = 2;
    z_order[1] = 3;
    z_order[2] = 1;
    z_order[3] = 0; // Shell starts focused

    shell_line_count = 0;
    shell_cmd_idx = 0;
    cmd_buffer[0] = '\0';

    shell_print("SubhashishOS v2.0 Ready. Type 'help'");
    shell_push_raw_line("> ls");
    char init_ls[128];
    vfs_list_files(init_ls, sizeof(init_ls));
    shell_print(init_ls);
    shell_push_raw_line("> lspci");
    int cnt = pci_get_device_count();
    for (int i = 0; i < cnt && i < 4; i++) {
        const pci_device_info_t* d = pci_get_device(i);
        if (!d) continue;
        const char* cname = pci_class_name(d->base_class, d->sub_class, d->prog_if);
        char line[64];
        char num[12];
        kstrlcpy(line, "PCI 0:", sizeof(line));
        kutoa(d->slot, num);
        kstrlcat(line, num, sizeof(line));
        kstrlcat(line, " ", sizeof(line));
        kstrlcat(line, cname, sizeof(line));
        shell_push_raw_line(line);
    }
}

void gui_handle_mouse(int mouse_x, int mouse_y, unsigned char mouse_buttons) {
    int left_down = (mouse_buttons & 1) != 0;
    int left_pressed = left_down && !(prev_mouse_buttons & 1);
    prev_mouse_buttons = mouse_buttons;

    // 1. Check dock clicks on fresh press
    int dock_w = 400, dock_h = 70;
    int dock_x = ((int)fb_width - dock_w) / 2;
    int dock_y = (int)fb_height - dock_h - 20;

    if (left_pressed) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            int ix = dock_x + 30 + i * 80;
            int iy = dock_y + 10;
            if (mouse_x >= ix && mouse_x <= ix + 50 &&
                mouse_y >= iy && mouse_y <= iy + 50) {
                if (!windows[i].is_open) {
                    windows[i].is_open = 1;
                    bring_to_front(i);
                } else if (top_open_window() == i) {
                    windows[i].is_open = 0;
                } else {
                    bring_to_front(i);
                }
                return;
            }
        }

        // 2. Check windows in front-to-back z-order
        for (int z = MAX_WINDOWS - 1; z >= 0; z--) {
            int idx = z_order[z];
            Window* w = &windows[idx];
            if (!w->is_open) continue;

            if (mouse_x >= w->x && mouse_x <= w->x + w->w &&
                mouse_y >= w->y && mouse_y <= w->y + w->h) {
                bring_to_front(idx);

                // Close button [X] in top-right of title bar
                int close_x = w->x + w->w - 26;
                int close_y = w->y + 5;
                if (mouse_x >= close_x && mouse_x <= close_x + 20 &&
                    mouse_y >= close_y && mouse_y <= close_y + 20) {
                    w->is_open = 0;
                    w->is_dragging = 0;
                    return;
                }

                // Title bar drag
                if (mouse_y <= w->y + 30) {
                    w->is_dragging = 1;
                    w->drag_off_x = mouse_x - w->x;
                    w->drag_off_y = mouse_y - w->y;
                    return;
                }

                // VFS Notes file tab click
                if (idx == 3 && mouse_y >= w->y + 70 && mouse_y <= w->y + 94) {
                    int count = vfs_file_count();
                    int tab_x = w->x + 12;
                    for (int f = 0; f < count && f < 4; f++) {
                        if (mouse_x >= tab_x && mouse_x <= tab_x + 118) {
                            const char* fname = 0;
                            if (vfs_get_file_info(f, &fname, 0) == 0 && fname) {
                                kstrlcpy(active_note_file, fname, sizeof(active_note_file));
                            }
                            break;
                        }
                        tab_x += 124;
                    }
                }
                return;
            }
        }
    }

    if (!left_down) {
        for (int i = 0; i < MAX_WINDOWS; i++) windows[i].is_dragging = 0;
    } else {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (windows[i].is_dragging) {
                windows[i].x = mouse_x - windows[i].drag_off_x;
                windows[i].y = mouse_y - windows[i].drag_off_y;
            }
        }
    }
}

static char hid_to_ascii(unsigned char code, unsigned char mods) {
    int shift = (mods & (KEY_MOD_LSHIFT | KEY_MOD_RSHIFT)) != 0;
    if (code >= 0x04 && code <= 0x1D) {
        char base = shift ? 'A' : 'a';
        return (char)(base + (code - 0x04));
    }
    if (code >= 0x1E && code <= 0x26) {
        return (char)('1' + (code - 0x1E));
    }
    if (code == 0x27) return '0';
    if (code == 0x2C) return ' ';
    if (code == 0x2D) return shift ? '_' : '-';
    if (code == 0x36) return ',';
    if (code == 0x37 || code == 0x34) return '.';
    if (code == 0x38) return '/';
    return 0;
}

static void execute_shell_command(const char *raw_cmd) {
    char trimmed[128];
    int n = 0;
    while (*raw_cmd == ' ') raw_cmd++;
    while (*raw_cmd && n < 127) {
        trimmed[n++] = *raw_cmd++;
    }
    while (n > 0 && trimmed[n - 1] == ' ') n--;
    trimmed[n] = '\0';

    if (n == 0) return;

    // Echo command into scrollback
    char echo_line[SHELL_LINE_LEN];
    kstrlcpy(echo_line, "> ", sizeof(echo_line));
    kstrlcat(echo_line, trimmed, sizeof(echo_line));
    shell_push_raw_line(echo_line);

    // Lowercase copy for command keyword matching
    char lower[128];
    for (int i = 0; i <= n; i++) lower[i] = to_lower_char(trimmed[i]);

    char out[256];
    out[0] = '\0';

    if (kstrcmp(lower, "ls") == 0) {
        vfs_list_files(out, sizeof(out));
        shell_print(out);
    } else if (kstrcmp(lower, "help") == 0) {
        shell_print("ls, cat <f>, write <f> <txt>, rm <f>");
        shell_print("lspci, ps, mem, uptime, uname, clear");
    } else if (kstrcmp(lower, "ps") == 0) {
        shell_print("PID 0: kernel_idle   [RUNNING]");
        shell_print("PID 1: xhci_usb_poll [RUNNING]");
        shell_print("PID 2: gui_compositor[RUNNING]");
        shell_print("PID 3: vfs_service   [READY]");
    } else if (kstrcmp(lower, "lspci") == 0) {
        int cnt = pci_get_device_count();
        for (int i = 0; i < cnt && i < 5; i++) {
            const pci_device_info_t* d = pci_get_device(i);
            if (!d) continue;
            const char* cname = pci_class_name(d->base_class, d->sub_class, d->prog_if);
            char line[64];
            char num[12];
            kstrlcpy(line, "PCI 0:", sizeof(line));
            kutoa(d->slot, num);
            kstrlcat(line, num, sizeof(line));
            kstrlcat(line, " ", sizeof(line));
            kstrlcat(line, cname, sizeof(line));
            shell_push_raw_line(line);
        }
    } else if (kstrcmp(lower, "clear") == 0) {
        shell_line_count = 0;
    } else if (kstrcmp(lower, "uname") == 0) {
        shell_print("SubhashishOS 2.0 x86_64 (UEFI Bare-Metal)");
    } else if (kstrcmp(lower, "uptime") == 0) {
        char num[21];
        kutoa(timer_ms() / 1000ULL, num);
        kstrlcpy(out, "Uptime: ", sizeof(out));
        kstrlcat(out, num, sizeof(out));
        kstrlcat(out, " seconds", sizeof(out));
        shell_print(out);
    } else if (kstrcmp(lower, "mem") == 0) {
        char num[21];
        kstrlcpy(out, "RAM Blocks: ", sizeof(out));
        kutoa(used_blocks, num);
        kstrlcat(out, num, sizeof(out));
        kstrlcat(out, "/", sizeof(out));
        kutoa(max_blocks, num);
        kstrlcat(out, num, sizeof(out));
        kstrlcat(out, " | Heap: ", sizeof(out));
        kutoa(heap_used_bytes(), num);
        kstrlcat(out, num, sizeof(out));
        kstrlcat(out, "B", sizeof(out));
        shell_print(out);
    } else if (kstrcmp(lower, "sysinfo") == 0) {
        windows[2].is_open = 1;
        bring_to_front(2);
        shell_print("Opened System Info window.");
    } else if (kstrcmp(lower, "notes") == 0) {
        windows[3].is_open = 1;
        bring_to_front(3);
        shell_print("Opened VFS Notes window.");
    } else if (kstrncmp(lower, "cat ", 4) == 0) {
        const char *arg = lower + 4;
        while (*arg == ' ') arg++;
        vfs_read_file(arg, out, sizeof(out));
        shell_print(out);
    } else if (kstrncmp(lower, "touch ", 6) == 0) {
        const char *arg = lower + 6;
        while (*arg == ' ') arg++;
        if (vfs_write_file(arg, "") == 0) {
            shell_print("Created empty file.");
        } else {
            shell_print("Error: VFS full or invalid name.");
        }
    } else if (kstrncmp(lower, "rm ", 3) == 0) {
        const char *arg = lower + 3;
        while (*arg == ' ') arg++;
        if (vfs_delete_file(arg) == 0) {
            shell_print("Deleted file.");
        } else {
            shell_print("Error: File not found.");
        }
    } else if (kstrncmp(lower, "write ", 6) == 0) {
        // write <filename> <text>
        const char *p = lower + 6;
        const char *orig_p = trimmed + 6;
        while (*p == ' ') { p++; orig_p++; }
        char fname[VFS_NAME_MAX];
        int fi = 0;
        while (*p && *p != ' ' && fi < VFS_NAME_MAX - 1) {
            fname[fi++] = *p++;
            orig_p++;
        }
        fname[fi] = '\0';
        while (*orig_p == ' ') orig_p++;
        if (fname[0] && vfs_write_file(fname, orig_p) == 0) {
            kstrlcpy(active_note_file, fname, sizeof(active_note_file));
            shell_print("Saved to VFS.");
        } else {
            shell_print("Usage: write <file> <content>");
        }
    } else {
        shell_print("Unknown cmd. Type 'help' for list.");
    }
}

void gui_handle_keyboard(unsigned char latest_keycode, unsigned char mods) {
    // Ensure Shell window is visible and focused when typing
    if (!windows[0].is_open) {
        windows[0].is_open = 1;
        bring_to_front(0);
    }
    if (latest_keycode == 0x2A) { // Backspace
        if (shell_cmd_idx > 0) {
            shell_cmd_idx--;
            cmd_buffer[shell_cmd_idx] = '\0';
        }
    } else if (latest_keycode == 0x28) { // Enter
        cmd_buffer[shell_cmd_idx] = '\0';
        execute_shell_command(cmd_buffer);
        shell_cmd_idx = 0;
        cmd_buffer[0] = '\0';
    } else {
        char ch = hid_to_ascii(latest_keycode, mods);
        if (ch && shell_cmd_idx < (int)sizeof(cmd_buffer) - 1) {
            cmd_buffer[shell_cmd_idx++] = ch;
            cmd_buffer[shell_cmd_idx] = '\0';
        }
    }
}

static void draw_dock(void) {
    static const char* const dock_labels[MAX_WINDOWS] = {"SH", "TM", "SI", "FS"};
    int dock_w = 400, dock_h = 70;
    int dock_x = ((int)fb_width - dock_w) / 2;
    int dock_y = (int)fb_height - dock_h - 20;
    draw_rect(dock_x - 2, dock_y - 2, dock_w + 4, dock_h + 4, 0x444444);
    draw_rect(dock_x, dock_y, dock_w, dock_h, 0x222222);

    int focused_id = top_open_window();
    for (int i = 0; i < MAX_WINDOWS; i++) {
        int ix = dock_x + 30 + i * 80;
        int iy = dock_y + 10;
        draw_rect(ix, iy, 50, 50, windows[i].accent_color);
        draw_string(ix + 10, iy + 17, dock_labels[i], 0x111111, 2);

        // Indicator dot beneath open windows (brighter if focused)
        if (windows[i].is_open) {
            unsigned int dot_col = (focused_id == i) ? 0xFFFFFF : 0x888888;
            draw_rect(ix + 18, iy + 54, 14, 4, dot_col);
        }
    }
}

static void draw_top_bar(void) {
    draw_rect(0, 0, fb_width, 32, 0x181818);
    draw_string(14, 8, "SubhashishOS v2.0", 0x00FF00, 2);

    int h, m, s;
    rtc_read_time(&h, &m, &s);
    char time_str[13];
    time_str[0] = (char)('0' + (h / 10));
    time_str[1] = (char)('0' + (h % 10));
    time_str[2] = ':';
    time_str[3] = (char)('0' + (m / 10));
    time_str[4] = (char)('0' + (m % 10));
    time_str[5] = ':';
    time_str[6] = (char)('0' + (s / 10));
    time_str[7] = (char)('0' + (s % 10));
    time_str[8] = ' ';
    time_str[9] = 'I';
    time_str[10] = 'S';
    time_str[11] = 'T';
    time_str[12] = '\0';

    draw_string(fb_width - 210, 8, time_str, 0xFFFFFF, 2);
}

void gui_render_frame(int mouse_x, int mouse_y) {
    clear_screen(0x0F0F0F);

    draw_top_bar();
    draw_dock();

    int focused_id = top_open_window();

    for (int z = 0; z < MAX_WINDOWS; z++) {
        int idx = z_order[z];
        Window* w = &windows[idx];
        if (!w->is_open) continue;

        int is_focused = (idx == focused_id);
        unsigned int border_col = is_focused ? 0x00FF00 : 0x555555;
        unsigned int title_bg   = is_focused ? 0x333333 : 0x252525;

        // Drop shadow for depth
        draw_rect(w->x + 6, w->y + 6, w->w + 4, w->h + 4, 0x070707);
        draw_rect(w->x - 2, w->y - 2, w->w + 4, w->h + 4, border_col);
        draw_rect(w->x, w->y, w->w, w->h, 0x1E1E1E);
        draw_rect(w->x, w->y, w->w, 30, title_bg);
        draw_rect(w->x + 6, w->y + 7, 6, 16, w->accent_color);
        draw_string(w->x + 18, w->y + 7, w->title, 0xFFFFFF, 2);

        // Close button [X]
        int close_x = w->x + w->w - 26;
        int close_y = w->y + 5;
        draw_rect(close_x, close_y, 20, 20, 0xCC3333);
        draw_char(close_x + 2, close_y + 2, 'X', 0xFFFFFF, 2);

        if (w->render) w->render(w->x, w->y, w->w, w->h, is_focused);
    }

    draw_cursor(mouse_x, mouse_y);
    video_swap();
}
