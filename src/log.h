#ifndef LOG_H
#define LOG_H

typedef enum {
    LOG_ERROR = 0,
    LOG_WARN  = 1,
    LOG_INFO  = 2,
    LOG_DEBUG = 3,
    LOG_TRACE = 4
} LogLevel;

/* Sets the current global log level threshold. */
void log_set_level(LogLevel level);

/* Returns the current global log level. */
LogLevel log_get_level(void);

/* Core logging function - formats and prints a message with timestamp and level tag. */
void log_msg(LogLevel level, const char *fmt, ...);

#define LOG_E(...) log_msg(LOG_ERROR, __VA_ARGS__)
#define LOG_W(...) log_msg(LOG_WARN,  __VA_ARGS__)
#define LOG_I(...) log_msg(LOG_INFO,  __VA_ARGS__)
#define LOG_D(...) log_msg(LOG_DEBUG, __VA_ARGS__)
#define LOG_T(...) log_msg(LOG_TRACE, __VA_ARGS__)

#endif /* LOG_H */