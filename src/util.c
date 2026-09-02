#define _CRT_SECURE_NO_WARNINGS
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

void buf_init(Buffer *b) {
    b->data = NULL;
    b->size = 0;
    b->capacity = 0;
}

void buf_free(Buffer *b) {
    free(b->data);
    buf_init(b);
}

void buf_append(Buffer *b, const void *src, size_t n) {
    if (b->size + n > b->capacity) {
        size_t cap = (b->capacity == 0) ? 65536 : b->capacity;
        while (cap < b->size + n) cap *= 2;
        b->data = realloc(b->data, cap);
        b->capacity = cap;
    }
    memcpy(b->data + b->size, src, n);
    b->size += n;
}

long file_size_on_disk(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0) ? (long)st.st_size : -1;
}

int make_dir(const char *path) {
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

int mkdirs(const char *path) {
    char tmp[4096];
    size_t i;
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    for (i = 0; tmp[i]; i++) {
        if (tmp[i] == PATHSEP || tmp[i] == '/') {
            tmp[i] = 0;
            if (tmp[0]) make_dir(tmp);
            tmp[i] = PATHSEP;
        }
    }
    if (tmp[0]) make_dir(tmp);
    return 0;
}

void fmt_size(char *buf, size_t n, long bytes) {
    if (bytes < 1024)                            snprintf(buf, n, "%ld B", bytes);
    else if (bytes < 1048576)                    snprintf(buf, n, "%.1f KB", bytes / 1024.0);
    else if (bytes < 1073741824L)                snprintf(buf, n, "%.1f MB", bytes / 1048576.0);
    else                                         snprintf(buf, n, "%.2f GB", bytes / 1073741824.0);
}

int extract_identifier(const char *url, char *out, size_t out_sz) {
    static const char *markers[] = { "/download/", "/details/", NULL };
    const char *p = NULL;
    for (int i = 0; markers[i]; i++) {
        p = strstr(url, markers[i]);
        if (p) { p += strlen(markers[i]); break; }
    }
    if (!p) return -1;

    const char *end = p;
    while (*end && *end != '/' && *end != '?' && *end != '#') end++;

    size_t len = end - p;
    if (len == 0 || len >= out_sz) return -1;
    memcpy(out, p, len);
    out[len] = 0;
    return 0;
}

char *str_dup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

/* Percent-encode a URL path segment, preserving '/' so sub-directory paths
   inside filenames survive. Bytes outside the RFC 3986 unreserved set are
   encoded as %XX. Returns the encoded length, or -1 if `out` is too small. */
int url_encode_path(const char *in, char *out, size_t out_sz) {
    static const char hex[] = "0123456789ABCDEF";
    if (!out_sz) return -1;
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        unsigned char c = *p;
        int safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') || c == '/' || c == '-' ||
                   c == '_' || c == '.' || c == '~';
        if (safe) {
            if (o + 1 >= out_sz) return -1;
            out[o++] = (char)c;
        } else {
            if (o + 3 >= out_sz) return -1;
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0xF];
        }
    }
    out[o] = 0;
    return (int)o;
}

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

int read_password(char *out, size_t out_sz) {
    if (!out || out_sz == 0) return -1;
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0, newmode = 0;
    BOOL have_mode = (h != INVALID_HANDLE_VALUE) && GetConsoleMode(h, &mode);
    if (have_mode) {
        newmode = mode & ~ENABLE_ECHO_INPUT;
        SetConsoleMode(h, newmode);
    }
    int ok = fgets(out, (int)out_sz, stdin) != NULL;
    if (have_mode) SetConsoleMode(h, mode);
#else
    struct termios tty, oldtty;
    int have_tty = tcgetattr(STDIN_FILENO, &oldtty) == 0;
    if (have_tty) {
        tty = oldtty;
        tty.c_lflag &= ~ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &tty);
    }
    int ok = fgets(out, (int)out_sz, stdin) != NULL;
    if (have_tty) tcsetattr(STDIN_FILENO, TCSANOW, &oldtty);
#endif
    if (!ok) return -1;
    out[strcspn(out, "\r\n")] = 0;
    return 0;
}

/* Recursive glob matcher supporting '*', '?', '[...]' and '{a,b}'.
   A leading alternate inside braces is tried against the text; the rest of the
   pattern is matched after the closing brace. */
static int wmatch(const char *text, const char *pattern);

static int wmatch(const char *t, const char *p) {
    while (*p) {
        if (*p == '*') {
            while (*p == '*') p++;
            if (!*p) return 1;
            for (const char *tt = t; ; tt++) {
                if (wmatch(tt, p)) return 1;
                if (!*tt) break;
            }
            return 0;
        } else if (*p == '?') {
            if (!*t) return 0;
            t++;
            p++;
        } else if (*p == '[') {
            if (!*t) return 0;
            int negate = 0;
            const char *q = p + 1;
            if (*q == '!' || *q == '^') { negate = 1; q++; }
            int matched = 0;
            while (*q && *q != ']') {
                char lo = *q;
                if (q[1] == '-' && q[2] && q[2] != ']') {
                    if (*t >= lo && *t <= q[2]) matched = 1;
                    q += 3;
                } else {
                    if (*t == lo) matched = 1;
                    q++;
                }
            }
            if (negate) matched = !matched;
            if (!matched) return 0;
            t++;
            p = (*q == ']') ? q + 1 : q;
        } else if (*p == '{') {
            const char *close = strchr(p, '}');
            if (close) {
                const char *seg = p + 1;
                while (seg < close) {
                    const char *comma = strchr(seg, ',');
                    if (comma == NULL || comma > close) comma = close;
                    size_t olen = (size_t)(comma - seg);
                    size_t restlen = strlen(close + 1);
                    if (olen + restlen + 1 < 4096) {
                        char combined[4096];
                        memcpy(combined, seg, olen);
                        strcpy(combined + olen, close + 1);
                        if (wmatch(t, combined)) return 1;
                    }
                    if (comma >= close) break;
                    seg = comma + 1;
                }
                return 0;
            }
            /* No closing brace: treat '{' literally. */
            if (*t != '{') return 0;
            t++;
            p++;
        } else {
            if (*t != *p) return 0;
            t++;
            p++;
        }
    }
    return *t == 0;
}

int wildcard_match(const char *text, const char *pattern) {
    if (!pattern || !pattern[0]) return 1; /* empty pattern matches everything */
    return wmatch(text, pattern);
}

char *user_home_dir(void) {
    const char *home = NULL;
#ifdef _WIN32
    home = getenv("USERPROFILE");
    if (!home) home = getenv("HOMEDRIVE");
#else
    home = getenv("HOME");
#endif
    if (!home) home = ".";
    return str_dup(home);
}