#define _CRT_SECURE_NO_WARNINGS
#include "dashboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "thrd.h"
#include "color.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define ESC "\x1b["

/* Number of concurrent-worker rows the top panel always reserves (also caps
   how many concurrent downloads are shown); kept small so the panel is a
   stable fixed rectangle on typical terminals. */
#define DASH_TOP_MAX 16
/* Number of trailing operation lines shown in the bottom panel. */
#define DASH_LOG_LINES 14
/* Total lines the panel repaints each frame (stable bounding box). */
#define DASH_HEIGHT (DASH_TOP_MAX + DASH_LOG_LINES)

typedef struct {
    int active;
    char file[256];
    int index;
    int total;
    long done;
    long size;
    long resume;
    double speed;
    double eta;
    int have_speed;
} DashSlot;

static int g_active = 0;
static thrd_mutex g_mutex;

static DashSlot g_slots[DASH_MAX_SLOTS];

static char g_log[DASH_LOG_LINES][256];
static int g_log_count = 0;
static int g_log_next = 0;

static time_t g_last_render = 0;

static void render_locked(void);

static int stdout_is_tty(void) {
    if (getenv("IA_DASH_FORCE") != NULL) return 1;
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD mode = 0;
    return GetConsoleMode(h, &mode) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

static void enable_windows_vt(void) {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode)) return;
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

void dash_init(void) {
    if (!stdout_is_tty()) return;
    if (getenv("NO_COLOR") != NULL) return;
    enable_windows_vt();
    thrd_mutex_init(&g_mutex);
    memset(g_slots, 0, sizeof(g_slots));
    g_log_count = 0;
    g_log_next = 0;
    g_active = 1;
}

int dash_active(void) {
    return g_active;
}

static void fmt_eta(char *out, size_t n, double eta) {
    if (eta < 0 || !(eta == eta)) { snprintf(out, n, "--:--:--"); return; }
    long e = (long)eta;
    snprintf(out, n, "%02ld:%02ld:%02ld", e / 3600, (e % 3600) / 60, e % 60);
}

void dash_begin_worker(int slot, const char *name, int index, int total) {
    if (!g_active || slot < 0 || slot >= DASH_MAX_SLOTS) return;
    thrd_mutex_lock(&g_mutex);
    DashSlot *s = &g_slots[slot];
    memset(s, 0, sizeof(*s));
    s->active = 1;
    s->index = index;
    s->total = total;
    snprintf(s->file, sizeof(s->file), "%s", name ? name : "");
    render_locked();
    thrd_mutex_unlock(&g_mutex);
}

void dash_set_worker(int slot, long done, long total, long resume,
                     double speed, double eta) {
    if (!g_active || slot < 0 || slot >= DASH_MAX_SLOTS) return;
    thrd_mutex_lock(&g_mutex);
    DashSlot *s = &g_slots[slot];
    if (!s->active) { thrd_mutex_unlock(&g_mutex); return; }
    s->done = done;
    s->size = total;
    s->resume = resume;
    s->speed = speed;
    s->eta = eta;
    s->have_speed = (speed >= 0);
    thrd_mutex_unlock(&g_mutex);
}

void dash_end_worker(int slot) {
    if (!g_active || slot < 0 || slot >= DASH_MAX_SLOTS) return;
    thrd_mutex_lock(&g_mutex);
    g_slots[slot].active = 0;
    render_locked();
    thrd_mutex_unlock(&g_mutex);
}

void dash_log(const char *line) {
    if (!g_active) return;
    thrd_mutex_lock(&g_mutex);
    snprintf(g_log[g_log_next], sizeof(g_log[g_log_next]), "%s", line ? line : "");
    g_log_next = (g_log_next + 1) % DASH_LOG_LINES;
    if (g_log_count < DASH_LOG_LINES) g_log_count++;
    render_locked();
    thrd_mutex_unlock(&g_mutex);
}

void dash_tick(void) {
    if (!g_active) return;
    time_t now = time(NULL);
    thrd_mutex_lock(&g_mutex);
    if (now - g_last_render >= 1) {
        render_locked();
    }
    thrd_mutex_unlock(&g_mutex);
}

static void blank_line(void) {
    fputs(ESC "K", stdout);
    fputc('\n', stdout);
}

static void draw_line(const DashSlot *s) {
    char pct[16] = "";
    if (s->size > 0) {
        double cur = (double)s->resume + (double)s->done;
        double p = cur / (double)s->size * 100.0;
        if (p > 100.0) p = 100.0;
        snprintf(pct, sizeof(pct), " %6.1f%%", p);
    }
    char speed[32] = "";
    if (s->have_speed && s->speed >= 0) {
        double kb = s->speed / 1024.0;
        if (kb >= 1024.0) snprintf(speed, sizeof(speed), "%.1f MB/s", kb / 1024.0);
        else              snprintf(speed, sizeof(speed), "%.1f KB/s", kb);
    }
    char eta[16] = "";
    fmt_eta(eta, sizeof(eta), s->eta);

    fputs(ESC "K", stdout);
    printf("%s[GET]%s %s[%d/%d]%s %-40s%s  ETA %s  %s\n",
           color_start("cyan"), color_reset(),
           color_start("bold"), s->index, s->total, color_reset(),
           s->file, pct, eta, speed);
}

/* Repaints a stable bounding box of DASH_HEIGHT lines every frame so that
   shrinking/inflating the set of active workers never leaves stale rows. */
static void render_locked(void) {
    if (!g_active) return;
    g_last_render = time(NULL);
    fputs(ESC "H", stdout); /* cursor to home (0,0) */

    int drawn = 0;
    for (int i = 0; i < DASH_TOP_MAX; i++) {
        if (!g_slots[i].active) continue;
        draw_line(&g_slots[i]);
        drawn++;
    }
    if (!drawn) {
        fputs(ESC "K", stdout);
        printf("  %s[IDLE]%s  no active transfers\n",
               color_start("yellow"), color_reset());
        drawn = 1;
    }
    /* Blank the rest of the top panel so the frame has a fixed height. */
    for (int i = drawn; i < DASH_TOP_MAX; i++) {
        blank_line();
    }

    for (int i = 0; i < DASH_LOG_LINES; i++) {
        fputs(ESC "K", stdout);
        if (i < g_log_count) {
            int idx = (g_log_next - g_log_count + i + DASH_LOG_LINES) % DASH_LOG_LINES;
            printf("  %s\n", g_log[idx]);
        } else {
            fputc('\n', stdout);
        }
    }
    fflush(stdout);
}

void dash_shutdown(void) {
    if (!g_active) return;
    thrd_mutex_lock(&g_mutex);
    size_t last = (size_t)(g_log_count < DASH_LOG_LINES ? g_log_count : DASH_LOG_LINES);
    fputs(ESC "H", stdout);
    for (size_t i = 0; i < last; i++) {
        fputs(ESC "K", stdout);
        int idx = (g_log_next - (int)last + (int)i + DASH_LOG_LINES) % DASH_LOG_LINES;
        printf("  %s\n", g_log[idx]);
    }
    fflush(stdout);
    g_active = 0;
    thrd_mutex_unlock(&g_mutex);
    thrd_mutex_destroy(&g_mutex);
}