#define _CRT_SECURE_NO_WARNINGS
#include "log.h"
#include "color.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

static LogLevel g_log_level = LOG_INFO;

void log_set_level(LogLevel level) {
    g_log_level = level;
}

LogLevel log_get_level(void) {
    return g_log_level;
}

static const char *log_level_name(LogLevel level) {
    switch (level) {
        case LOG_ERROR: return "ERROR";
        case LOG_WARN:  return "WARN ";
        case LOG_INFO:  return "INFO ";
        case LOG_DEBUG: return "DEBUG";
        case LOG_TRACE: return "TRACE";
        default:        return "?????";
    }
}

/* Colour for each log level's tag. NULL/"" disables colour for that level. */
static const char *log_level_color(LogLevel level) {
    switch (level) {
        case LOG_ERROR: return "red";
        case LOG_WARN:  return "yellow";
        case LOG_INFO:  return "green";
        case LOG_DEBUG: return "cyan";
        case LOG_TRACE: return "magenta";
        default:        return NULL;
    }
}

void log_msg(LogLevel level, const char *fmt, ...) {
    if (level > g_log_level) return;

    char tbuf[64];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm) strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tm);
    else snprintf(tbuf, sizeof(tbuf), "??:??:??");

    const char *col = log_level_color(level);
    const char *s = col ? color_start(col) : "";
    const char *r = *s ? color_reset() : "";
    printf("[%s] [%s%s%s] ", tbuf, s, log_level_name(level), r);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}