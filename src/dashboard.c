#define _CRT_SECURE_NO_WARNINGS
#include "dashboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "thrd.h"
#include "color.h"
#include "util.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/ioctl.h>
#endif

#define ESC "\x1b["

#define WIDTH_MIN 80
#define WIDTH_MAX 220
#define HEIGHT_MIN 20
#define HEIGHT_MAX 80

/* Fixed launch-panel content rows (2-column key/value). */
#define LAUNCH_IN 6
/* Minimum content rows reserved for the active-downloads and log panels. */
#define DL_MIN 2
#define LG_MIN 3

/* Box drawing (UTF-8). */
#define B_H  "\xE2\x94\x80"
#define B_V  "\xE2\x94\x82"
#define B_TL "\xE2\x94\x8C"
#define B_TR "\xE2\x94\x90"
#define B_LT "\xE2\x94\x9C"
#define B_RT "\xE2\x94\xA4"
#define B_BL "\xE2\x94\x94"
#define B_BR "\xE2\x94\x98"
#define B_PS "\xE2\x94\xB1"

typedef struct {
    int active;
    char file[80];
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

static int g_width = WIDTH_MIN;
static int g_height = HEIGHT_MIN;
static time_t g_start = 0;
static time_t g_last_render = 0;

static DashSlot g_slots[DASH_MAX_SLOTS];
static DashCounters g_agg;

static char g_log[512][256];
static int g_log_count = 0;
static int g_log_next = 0;

static DashLaunch g_launch;

typedef struct {
    int dl;  /* active-downloads content rows */
    int lg;  /* operations-log content rows */
} Layout;

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

static void probe_term(void) {
    int w = WIDTH_MIN, h = HEIGHT_MIN;
#ifdef _WIN32
    HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hCon != INVALID_HANDLE_VALUE) {
        CONSOLE_SCREEN_BUFFER_INFO csi;
        if (GetConsoleScreenBufferInfo(hCon, &csi)) {
            w = (int)(csi.srWindow.Right - csi.srWindow.Left + 1);
            h = (int)(csi.srWindow.Bottom - csi.srWindow.Top + 1);
        }
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_col) w = ws.ws_col;
        if (ws.ws_row) h = ws.ws_row;
    }
    const char *cols = getenv("COLUMNS");
    const char *rows = getenv("LINES");
    if (cols && atoi(cols) > 0) w = atoi(cols);
    if (rows && atoi(rows) > 0) h = atoi(rows);
#endif
    if (w < WIDTH_MIN) w = WIDTH_MIN;
    if (w > WIDTH_MAX) w = WIDTH_MAX;
    if (h < HEIGHT_MIN) h = HEIGHT_MIN;
    if (h > HEIGHT_MAX) h = HEIGHT_MAX;
    g_width = w;
    g_height = h;
}

void dash_init(void) {
    if (!stdout_is_tty()) return;
    enable_windows_vt();
    thrd_mutex_init(&g_mutex);
    memset(g_slots, 0, sizeof(g_slots));
    memset(&g_agg, 0, sizeof(g_agg));
    g_log_count = 0;
    g_log_next = 0;
    probe_term();
    g_start = time(NULL);
    g_last_render = 0;
    g_active = 1;
}

int dash_active(void) {
    return g_active;
}

void dash_set_launch(const DashLaunch *launch) {
    if (!launch) return;
    if (g_active) return;
    memset(&g_launch, 0, sizeof(g_launch));
    if (launch->version[0])     snprintf(g_launch.version, sizeof(g_launch.version), "%s", launch->version);
    if (launch->source_url[0])  snprintf(g_launch.source_url, sizeof(g_launch.source_url), "%s", launch->source_url);
    if (launch->destination[0]) snprintf(g_launch.destination, sizeof(g_launch.destination), "%s", launch->destination);
    if (launch->identifier[0])  snprintf(g_launch.identifier, sizeof(g_launch.identifier), "%s", launch->identifier);
    if (launch->user[0])        snprintf(g_launch.user, sizeof(g_launch.user), "%s", launch->user);
    if (launch->filter[0])      snprintf(g_launch.filter, sizeof(g_launch.filter), "%s", launch->filter);
    g_launch.threads = launch->threads;
}

/* Strip ANSI escapes when counting visible width. */
static size_t vislen(const char *s) {
    size_t n = 0;
    while (*s) {
        if (*s == '\x1b') {
            if (s[1] == '[') { s += 2; while (*s && *s != 'm') s++; if (*s) s++; }
            else s += 2;
            continue;
        }
        n++;
        s++;
    }
    return n;
}

static void pad(size_t count) {
    char sp[64]; memset(sp, ' ', 64);
    while (count) { size_t k = count > 64 ? 64 : count; fwrite(sp, 1, k, stdout); count -= k; }
}

/* Row of vertical bars filling the interior width. */
static void blank_row(void) {
    fputs(B_V, stdout);
    pad((size_t)(g_width - 2));
    fputs(B_V, stdout);
    fputs(ESC "K", stdout);
    fputc('\n', stdout);
}

/* A content row already including leading "  " content (no escape counting). */
static void content_row(const char *text, int emph) {
    fputs(B_V, stdout);
    fputs("  ", stdout);
    if (emph) fputs("\x1b[1m", stdout);
    fputs(text, stdout);
    if (emph) fputs("\x1b[0m", stdout);
    size_t vlen = vislen(text);
    size_t used = 2 + vlen;
    if (used < (size_t)(g_width - 2)) pad((size_t)(g_width - 2) - used);
    fputs(B_V, stdout);
    fputs(ESC "K", stdout);
    fputc('\n', stdout);
}

/* Full horizontal rule with a panel title bracketed. */
static void rule(const char *title) {
    fputs(B_LT, stdout);
    size_t t = title ? strlen(title) : 0;
    int inner = g_width - 2;
    int free = inner - (int)t - 2;
    int left = free / 2;
    int right = free - left;
    for (int i = 0; i < left; i++) fputs(B_H, stdout);
    if (t) { fputs(" " B_PS " ", stdout); fputs(title, stdout); fputs(" " B_PS " ", stdout); }
    else   fputs(" ", stdout);
    for (int i = 0; i < right; i++) fputs(B_H, stdout);
    fputs(B_RT, stdout);
    fputs(ESC "K", stdout);
    fputc('\n', stdout);
}

static void render_header_line(void) {
    char cell[512];
    snprintf(cell, sizeof(cell), "Archive.org Downloader  v%s   [LIVE]", g_launch.version[0] ? g_launch.version : "dev");
    long elapsed = (long)(time(NULL) - g_start);
    char el[64];
    snprintf(el, sizeof(el), "elapsed %02ld:%02ld:%02ld", elapsed / 3600, (elapsed % 3600) / 60, elapsed % 60);
    /* left text, right elapsed */
    fputs(B_V, stdout);
    fputs("  ", stdout);
    fputs(cell, stdout);
    size_t l = 2 + vislen(cell);
    size_t r = vislen(el) + 2;
    size_t mid = (size_t)(g_width - 2) - l - r;
    if (mid > (size_t)(g_width - 2)) mid = 0;
    pad(mid);
    fputs(el, stdout);
    fputs("  ", stdout);
    fputs(B_V, stdout);
    fputs(ESC "K", stdout);
    fputc('\n', stdout);
}

static void render_launch_content(void) {
    char thr[32]; snprintf(thr, sizeof(thr), "%d", g_launch.threads > 0 ? g_launch.threads : 1);
    const char *k[][2] = {
        { "Source", g_launch.source_url },
        { "User", g_launch.user[0] ? g_launch.user : "(guest)" },
        { "Archive", g_launch.identifier },
        { "Threads", thr },
        { "Destination", g_launch.destination },
        { "Filter", g_launch.filter[0] ? g_launch.filter : "(all)" },
    };
    int inner = g_width - 2;
    int halfc = (inner - 1) / 2;   /* each cell width (excl borders) */
    int per_col = LAUNCH_IN / 2;    /* rows per column (3) */
    for (int row = 0; row < per_col; row++) {
        fputs(B_V, stdout);
        for (int c = 0; c < 2; c++) {
            int idx = c * per_col + row;
            char cell[600];
            snprintf(cell, sizeof(cell), "  %-11s : %s", k[idx][0], k[idx][1] ? k[idx][1] : "");
            size_t vlen = vislen(cell);
            int max = halfc - 1;
            if ((int)vlen > max) vlen = (size_t)max;
            fputs("  ", stdout);
            fputs(cell, stdout);
            if (vislen(cell) < (size_t)max) pad((size_t)max - vislen(cell));
            fputs(B_V, stdout);
        }
        fputs(ESC "K", stdout);
        fputc('\n', stdout);
    }
}

static void render_downloads_content(int rows) {
    int act = 0;
    for (int i = 0; i < DASH_MAX_SLOTS; i++) if (g_slots[i].active) act++;
    if (act == 0) {
        for (int i = 0; i < rows; i++) blank_row();
        return;
    }
    int shown = 0;
    for (int i = 0; i < DASH_MAX_SLOTS && shown < rows; i++) {
        if (!g_slots[i].active) continue;
        char cell[512];
        char pct[16] = "";
        if (g_slots[i].size > 0) {
            double cur = (double)g_slots[i].resume + (double)g_slots[i].done;
            double p = cur / (double)g_slots[i].size * 100.0;
            if (p > 100.0) p = 100.0;
            snprintf(pct, sizeof(pct), "%6.1f%%", p);
        }
        char spd[24] = "";
        if (g_slots[i].have_speed && g_slots[i].speed >= 0) {
            double kb = g_slots[i].speed / 1024.0;
            if (kb >= 1024.0) snprintf(spd, sizeof(spd), "%.1f MB/s", kb / 1024.0);
            else              snprintf(spd, sizeof(spd), "%.1f KB/s", kb);
        }
        char eta[16] = "--:--:--";
        if (g_slots[i].eta >= 0 && g_slots[i].eta == g_slots[i].eta) {
            long e = (long)g_slots[i].eta;
            snprintf(eta, sizeof(eta), "%02ld:%02ld:%02ld", e / 3600, (e % 3600) / 60, e % 60);
        }
        snprintf(cell, sizeof(cell), "\x1b[1m[%d/%d]\x1b[0m %-30s %s  %10s  ETA %s",
                 g_slots[i].index, g_slots[i].total, g_slots[i].file, pct, spd, eta);
        content_row(cell, 0);
        shown++;
    }
    for (int i = shown; i < rows; i++) blank_row();
}

static void render_log_content(int cap) {
    int rows = cap;
    for (int i = 0; i < rows; i++) {
        if (i < g_log_count) {
            int idx = (g_log_next - g_log_count + i + 512) % 512;
            content_row(g_log[idx], 0);
        } else {
            blank_row();
        }
    }
}

static void render_footer_content(void) {
    long elapsed = (long)(time(NULL) - g_start);
    char d_sz[32]; fmt_size(d_sz, sizeof(d_sz), g_agg.bytes_done);
    char cell[512];
    snprintf(cell, sizeof(cell),
             "Done %ld   Skipped %ld   Filtered %ld   Restricted %ld   Failed %ld   "
             "Bytes %s   Time %02ld:%02ld:%02ld",
             g_agg.downloaded, g_agg.skipped, g_agg.filtered, g_agg.restricted,
             g_agg.failed, d_sz, elapsed / 3600, (elapsed % 3600) / 60, elapsed % 60);
    fputs(B_V, stdout);
    fputs("  ", stdout);
    fputs(cell, stdout);
    size_t vlen = vislen(cell);
    size_t used = 2 + vlen;
    if (used < (size_t)(g_width - 2)) pad((size_t)(g_width - 2) - used);
    fputs(B_V, stdout);
    fputs(ESC "K", stdout);
    fputc('\n', stdout);
}

/* Compute panel row budgets so the whole dashboard fits g_height rows. */
static Layout layout(void) {
    /* rows: top(1) + header(1) + launch rule(1) + LAUNCH_IN + dl rule(1)
       + dl + log rule(1) + lg + footer rule(1) + footer(1) + bottom(1) */
    int fixed = 1 + 1 + 1 + LAUNCH_IN + 1 + 1 + 1 + 1 + 1;
    int avail = g_height - fixed;
    Layout lo = {0, 0};
    if (avail < DL_MIN + LG_MIN) { lo.dl = DL_MIN; lo.lg = LG_MIN; return lo; }
    int act = 0;
    for (int i = 0; i < DASH_MAX_SLOTS; i++) if (g_slots[i].active) act++;
    int want_dl = act > DL_MIN ? act : DL_MIN;
    if (want_dl > avail - LG_MIN) want_dl = avail - LG_MIN;
    lo.dl = want_dl;
    lo.lg = avail - want_dl;
    return lo;
}

static void render_locked(void) {
    if (!g_active) return;
    g_last_render = time(NULL);
    Layout lo = layout();

    /* top border */
    fputs(B_TL, stdout);
    for (int i = 0; i < g_width - 2; i++) fputs(B_H, stdout);
    fputs(B_TR, stdout);
    fputs(ESC "K", stdout);
    fputc('\n', stdout);
    render_header_line();
    rule("LAUNCH");
    render_launch_content();
    rule("ACTIVE DOWNLOADS");
    render_downloads_content(lo.dl);
    rule("LATEST OPERATIONS");
    render_log_content(lo.lg);
    rule("FOOTER");
    render_footer_content();
    fputs(B_BL, stdout);
    for (int i = 0; i < g_width - 2; i++) fputs(B_H, stdout);
    fputs(B_BR, stdout);
    fputs(ESC "K", stdout);
    fputc('\n', stdout);
    fflush(stdout);
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
    g_log_next = (g_log_next + 1) % 512;
    if (g_log_count < 512) g_log_count++;
    render_locked();
    thrd_mutex_unlock(&g_mutex);
}

void dash_tick(void) {
    if (!g_active) return;
    time_t now = time(NULL);
    thrd_mutex_lock(&g_mutex);
    if (now - g_last_render >= 1) {
        g_last_render = now;
        render_locked();
    }
    thrd_mutex_unlock(&g_mutex);
}

void dash_set_agg(const DashCounters *agg) {
    if (!g_active || !agg) return;
    thrd_mutex_lock(&g_mutex);
    g_agg = *agg;
    thrd_mutex_unlock(&g_mutex);
}

void dash_shutdown(void) {
    if (!g_active) return;
    thrd_mutex_lock(&g_mutex);
    /* Final repaint showing current state, then restore cursor to a clean
       line and drop the box. */
    g_last_render = -1;
    render_locked();
    fputs(ESC "J", stdout);   /* clear from cursor to end of screen */
    fflush(stdout);
    g_active = 0;
    thrd_mutex_unlock(&g_mutex);
    thrd_mutex_destroy(&g_mutex);
}