#ifndef HTTP_H
#define HTTP_H

#include <stdio.h>
#include <time.h>

#include "util.h"

/*
 * Download-context struct used when transferring a file to disk.
 * `resume_from` > 0 means a requested byte-offset for a Range request.
 * If the server ignores the Range and returns 200 (full body), the
 * implementation reopens the file at offset 0 (overwrite).
 */
typedef struct {
    FILE *fp;
    const char *file_name;
    long resume_from;
    long total_size;
    long downloaded;
    time_t start_time;
    int index;
    int total_files;
} DownloadContext;

/*
 * Write callback invoked with body bytes as they are received.
 * Used both internally (file_writer) and passed from the downloader.
 */
typedef void (*HttpWriteCallback)(const void *data, size_t len, void *user);

/*
 * Perform an HTTP GET request.
 *
 * @param url        Fully-qualified URL.
 * @param out        If write_cb is NULL, body is accumulated here (raw buffer).
 * @param range_hdr  Optional "bytes=start-" range string; NULL for full request.
 * @param write_cb   Optional callback receiving body chunks; if NULL data goes
 *                   into `out`.
 * @param user       Opaque pointer passed to write_cb (a DownloadContext*).
 *
 * @return HTTP status code (200, 206, 416, ...) or -1 on transfer failure.
 */
int http_get(const char *url, Buffer *out, const char *range_hdr,
             HttpWriteCallback write_cb, void *user);

/* Perform an HTTP POST with application/x-www-form-urlencoded form data.
 * Response body is accumulated into `out`. Used for the login endpoint.
 * @return HTTP status code (e.g. 200 or 401) or -1 on transfer failure.
 */
int http_post_form(const char *url, const char *form_data, Buffer *out);

/* Set a Cookie header value (e.g. "a=1; b=2") to attach to every subsequent
 * request. Pass NULL to clear. The string is copied internally. */
void http_set_cookie_header(const char *cookie_header);

/* Initialize any global HTTP subsystem once before use. */
void http_init(void);

/* Tear down global HTTP subsystem. */
void http_cleanup(void);

#endif /* HTTP_H */