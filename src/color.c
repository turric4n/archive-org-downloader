#define _CRT_SECURE_NO_WARNINGS
#include "color.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define ESC "\x1b["

static int g_enabled = 0;
static int g_forced = 0; /* set via color_enable()/color_disable() */

/* Turn on VT/ANSI processing for the Windows console (if available) so the
 * escape sequences actually render. Safe no-op on modern Windows 10+ where it
 * is on by default, and harmless elsewhere. */
static void enable_windows_vt(void) {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode)) return;
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

static int stdout_is_tty(void) {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD mode = 0;
    return GetConsoleMode(h, &mode) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

int color_init(void) {
    g_enabled = stdout_is_tty();
    if (g_enabled) enable_windows_vt();
    if (getenv("NO_COLOR") != NULL) g_enabled = 0; /* https://no-color.org/ */
    return g_enabled;
}

int color_enabled(void) {
    return g_enabled;
}

void color_enable(void) {
    g_forced = 1;
    g_enabled = 1;
    enable_windows_vt();
}

void color_disable(void) {
    g_forced = 1;
    g_enabled = 0;
}

static const char *ansi_code(const char *name) {
    if (!name) return "";
    if (strcmp(name, "reset") == 0)  return "0";
    if (strcmp(name, "bold") == 0)   return "1";
    if (strcmp(name, "red") == 0)    return "31";
    if (strcmp(name, "green") == 0)  return "32";
    if (strcmp(name, "yellow") == 0) return "33";
    if (strcmp(name, "blue") == 0)   return "34";
    if (strcmp(name, "magenta") == 0)return "35";
    if (strcmp(name, "cyan") == 0)   return "36";
    if (strcmp(name, "white") == 0)  return "37";
    if (strncmp(name, "fg:", 3) == 0) {
        static char buf[12];
        int n = atoi(name + 3);
        if (n >= 0 && n <= 255) {
            snprintf(buf, sizeof(buf), "38;5;%d", n);
            return buf;
        }
    }
    return "";
}

const char *color_start(const char *name) {
    if (!g_enabled) return "";
    const char *code = ansi_code(name);
    if (!code[0]) return "";
    static char out[16];
    snprintf(out, sizeof(out), ESC "%sm", code);
    return out;
}

const char *color_reset(void) {
    if (!g_enabled) return "";
    return ESC "0m";
}