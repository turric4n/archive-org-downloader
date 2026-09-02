#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

/* Platform path separator */
#ifdef _WIN32
#define PATHSEP '\\'
#else
#define PATHSEP '/'
#endif

/* Growing in-memory byte buffer */
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} Buffer;

void buf_init(Buffer *b);
void buf_free(Buffer *b);
void buf_append(Buffer *b, const void *src, size_t n);

/* Returns -1 if the file does not exist, otherwise its size in bytes. */
long file_size_on_disk(const char *path);

/* Recursively create intermediate directories for the given path. */
int mkdirs(const char *path);

/* Create a single directory (platform-specific). */
int make_dir(const char *path);

/* Format a byte count as a human-readable string (B/KB/MB/GB). */
void fmt_size(char *buf, size_t n, long bytes);

/* Extract the archive identifier from an archive.org URL.
   Returns 0 on success and writes the identifier into `out`.
   Accepts URLs in the forms:
     https://archive.org/download/<id>
     https://archive.org/details/<id>
     .../download/<id>/
     .../details/<id>/files  */
int extract_identifier(const char *url, char *out, size_t out_sz);

char *str_dup(const char *s);

/* Percent-encode a URL path while preserving '/' separators (RFC 3986).
   Returns the encoded length, or -1 if `out` is too small. */
int url_encode_path(const char *in, char *out, size_t out_sz);

/* Read a line from stdin without echoing the typed characters (for passwords).
   Returns 0 on success; the result is written into `out` (NUL-terminated). */
int read_password(char *out, size_t out_sz);

/* Simple glob matcher: supports '*', '?', '[...]' character classes and
   '{a,b,c}' alternation. Returns non-zero if `text` matches `pattern`. */
int wildcard_match(const char *text, const char *pattern);

/* Get the base directory for storing the tool's auth config file.
   On Windows uses %USERPROFILE%; on POSIX $HOME. Returns a freshly-allocated
   path string the caller must free(). */
char *user_home_dir(void);

#endif /* UTIL_H */