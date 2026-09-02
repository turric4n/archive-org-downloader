#define _CRT_SECURE_NO_WARNINGS
#include "downloader.h"
#include "http.h"
#include "log.h"
#include "util.h"
#include "color.h"
#include "dashboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

static void status_line(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (dash_active()) {
        char buf[512];
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        dash_log(buf);
    } else {
        vprintf(fmt, ap);
        va_end(ap);
        fflush(stdout);
    }
}

static void progress_display(const void *data, size_t len, void *user) {
    DownloadContext *dc = (DownloadContext *)user;
    LOG_T("file_writer: received %zu bytes for file #%d of %d.",
          len, dc->index, dc->total_files);
    fwrite(data, 1, len, dc->fp);
    dc->downloaded += (long)len;

    double elapsed = difftime(time(NULL), dc->start_time);
    if (elapsed < 0.1) elapsed = 0.1;
    double total = dc->total_size;
    double cur = dc->resume_from + dc->downloaded;
    double pct = total > 0 ? cur / total * 100.0 : 0;
    double speed = dc->downloaded / elapsed;
    double eta = total > 0 ? (total - cur) / speed : 0;

    if (dash_active()) {
        dash_set_worker(dc->slot, dc->downloaded, dc->total_size,
                        dc->resume_from, speed, eta);
        dash_tick();
        return;
    }

    int h = eta > 0 ? (int)(eta / 3600) : 0;
    int m = eta > 0 ? (int)((eta - h * 3600) / 60) : 0;
    int s = eta > 0 ? (int)(eta - h * 3600 - m * 60) : 0;

    printf("\r  [%d/%d] %ld / %ld bytes (%.1f%%) - %.1f KB/s - ETA %02d:%02d:%02d  ",
           dc->index, dc->total_files,
           dc->resume_from + dc->downloaded, dc->total_size,
           pct, speed / 1024.0, h, m, s);
    fflush(stdout);
}

/* Returns the local path for a file name, joined under `dest`. */
static void build_local_path(const char *dest, const char *name,
                             char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s%c%s", dest, PATHSEP, name);
#ifdef _WIN32
    for (char *q = out; *q; q++) if (*q == '/') *q = '\\';
#endif
}

/* Returns the URL of a file within the archive. The file name is
   percent-encoded (spaces, parens, brackets, etc.) so the resulting URL is
   valid for both libcurl and WinHTTP. */
static void build_download_url(const char *identifier, const char *name,
                               char *out, size_t out_sz) {
    char enc[2048];
    if (url_encode_path(name, enc, sizeof(enc)) < 0) {
        LOG_E("File name too long to encode into URL: '%s'", name);
        if (out_sz) out[0] = 0;
        return;
    }
    snprintf(out, out_sz, "https://archive.org/download/%s/%s",
             identifier, enc);
}

/* Shared state handed to every worker thread. `next` is consumed under the
   mutex so each file is handled by exactly one worker. */
typedef struct {
    const char *identifier;
    const char *dest;
    const char *const *names;
    const double *sizes;
    const int *restricted;
    const int *filtered;
    size_t count;
    size_t next;
    DownloadStats *stats;
    thrd_mutex mutex;
} DownloadShared;

static void download_one(void *arg);

static void count_one(DownloadShared *sh, int kind) {
    thrd_mutex_lock(&sh->mutex);
    DownloadStats *s = sh->stats;
    switch (kind) {
        case 1:  s->downloaded++; break;
        case 2:  s->skipped++;    break;
        case 3:  s->failed++;     break;
        case 4:  s->restricted++; break;
        case 5:  s->filtered++;   break;
    }
    thrd_mutex_unlock(&sh->mutex);
}

typedef struct {
    DownloadShared *sh;
    int slot;
} DownloadWorker;

int download_all(const char *identifier, const char *dest,
                 const char *const *names, const double *sizes,
                 const int *restricted, const int *filtered, size_t count,
                 int threads, DownloadStats *stats) {
    stats->downloaded = 0;
    stats->skipped = 0;
    stats->failed = 0;
    stats->restricted = 0;
    stats->filtered = 0;

    LOG_T("Creating destination directory '%s'.", dest);
    make_dir(dest);

    int n = threads > 0 ? threads : 1;
    if ((size_t)n > count) n = (int)count;
    if (n < 1) n = 1;

    DownloadShared sh = {0};
    sh.identifier = identifier;
    sh.dest = dest;
    sh.names = names;
    sh.sizes = sizes;
    sh.restricted = restricted;
    sh.filtered = filtered;
    sh.count = count;
    sh.next = 0;
    sh.stats = stats;
    thrd_mutex_init(&sh.mutex);

    thrd_t *workers = (thrd_t *)calloc((size_t)n, sizeof(thrd_t));
    DownloadWorker *wargs = (DownloadWorker *)calloc((size_t)n, sizeof(DownloadWorker));
    if (!workers || !wargs) {
        free(workers);
        free(wargs);
        thrd_mutex_destroy(&sh.mutex);
        return 1;
    }

    int spawned = 0;
    for (int i = 0; i < n; i++) {
        wargs[i].sh = &sh;
        wargs[i].slot = i;
        if (thrd_create(&workers[i], download_one, &wargs[i]) == 0) spawned++;
    }

    for (int i = 0; i < spawned; i++) thrd_join(workers[i]);
    free(workers);
    free(wargs);
    thrd_mutex_destroy(&sh.mutex);

    return stats->failed > 0 ? 1 : 0;
}

static size_t next_index(DownloadShared *sh) {
    thrd_mutex_lock(&sh->mutex);
    size_t i = sh->next++;
    int ok = i < sh->count;
    thrd_mutex_unlock(&sh->mutex);
    return ok ? i : (size_t)-1;
}

static void download_one(void *arg) {
    DownloadWorker *wk = (DownloadWorker *)arg;
    DownloadShared *sh = wk->sh;
    int slot = wk->slot;

    for (;;) {
        size_t i = next_index(sh);
        if (i == (size_t)-1) break;

        const char *name = sh->names[i];
        LOG_T("Processing file index %zu of %zu.", i + 1, sh->count);
        if (!name || !name[0]) {
            LOG_W("File %zu has empty name - skipped.", i + 1);
            continue;
        }

        if (sh->filtered && sh->filtered[i]) {
            LOG_D("File '%s': filtered out by --type pattern - skipping.", name);
            status_line("  %s[SKIP]%s %s (filtered)\n",
                        color_start("yellow"), color_reset(), name);
            count_one(sh, 5);
            continue;
        }

        if (sh->restricted && sh->restricted[i]) {
            LOG_D("File '%s': marked as restricted - skipping.", name);
            status_line("  %s[SKIP]%s %s (restricted)\n",
                        color_start("yellow"), color_reset(), name);
            count_one(sh, 4);
            continue;
        }

        double sz = sh->sizes ? sh->sizes[i] : 0;
        long fsz = (long)sz;

        char url[4096], path[4096];
        build_download_url(sh->identifier, name, url, sizeof(url));
        build_local_path(sh->dest, name, path, sizeof(path));
        LOG_D("Download URL : %s", url);
        LOG_D("Local path   : %s  (expected size %ld)", path, fsz);

        long exist = file_size_on_disk(path);
        if (exist >= 0) LOG_D("File exists on disk with size %ld bytes.", exist);
        else            LOG_D("File does not exist on disk.");

        if (exist >= fsz && fsz > 0) {
            LOG_I("File '%s' already downloaded (%ld bytes). Skipping.", name, exist);
            status_line("  %s[DONE]%s %s (already downloaded)\n",
                        color_start("green"), color_reset(), name);
            count_one(sh, 2);
            continue;
        }

        char szbuf[64]; fmt_size(szbuf, sizeof(szbuf), fsz);
        LOG_I("Starting download of '%s' (%s, %ld bytes).", name, szbuf, fsz);
        status_line("  %s[GET]%s  %s (%s)\n",
                    color_start("cyan"), color_reset(), name, szbuf);

        /* Create parent dirs if the name has sub-paths */
        char *last = strrchr(path, '/');
        char *lastbs = strrchr(path, '\\');
        if (!last || (lastbs && lastbs > last)) last = lastbs;
        if (last) {
            *last = 0;
            LOG_T("Creating directory structure for: %s", path);
            mkdirs(path);
            *last = PATHSEP;
        }

        FILE *fp = NULL;
        long resume = 0;
        if (exist > 0 && exist < fsz) {
            LOG_I("Partial file: %ld / %ld bytes (%.1f%%). Resuming.",
                  exist, fsz, (double)exist / fsz * 100);
            status_line("         Resuming from %ld bytes (%.1f%%)\n",
                        exist, (double)exist / fsz * 100);
            fp = fopen(path, "ab");
            resume = exist;
            if (fp) LOG_D("Opened '%s' in append mode.", path);
        } else {
            LOG_T("Opening '%s' for fresh download.", path);
            fp = fopen(path, "wb");
            if (fp) LOG_D("Opened '%s' for writing.", path);
        }
        if (!fp) {
            LOG_E("Cannot open file for writing: %s", path);
            status_line("  %s[ERR]%s Cannot write: %s\n",
                        color_start("red"), color_reset(), path);
            count_one(sh, 3);
            continue;
        }

        DownloadContext dc = {0};
        dc.fp = fp;
        dc.file_name = path;
        dc.resume_from = resume;
        dc.total_size = fsz;
        dc.start_time = time(NULL);
        dc.index = (int)(i + 1);
        dc.total_files = (int)sh->count;
        dc.slot = slot;

        dash_begin_worker(slot, name, (int)(i + 1), (int)sh->count);

        char range_h[64] = {0};
        if (resume > 0) {
            snprintf(range_h, sizeof(range_h), "bytes=%ld-", resume);
            LOG_D("Range header set: '%s'", range_h);
        } else {
            LOG_T("No resume - no Range header needed.");
        }

        LOG_T("Initiating HTTP download of file '%s'.", name);
        int sc = http_get(url, NULL, range_h[0] ? range_h : NULL,
                          progress_display, &dc);
        fclose(fp);
        dash_end_worker(slot);
        LOG_D("File '%s': transfer complete, status %d.", name, sc);

        if (sc == 200 || sc == 206) {
            long final = file_size_on_disk(path);
            LOG_D("File '%s': final size on disk = %ld.", name, final);
            if (final >= fsz || fsz <= 0) {
                LOG_I("Successfully downloaded '%s' (%ld bytes).", name, final);
                status_line("  %s[OK]%s   %s\n",
                            color_start("green"), color_reset(), name);
                count_one(sh, 1);
            } else {
                LOG_W("File '%s' incomplete: %ld/%ld bytes.", name, final, fsz);
                status_line("  %s[WARN]%s %s (incomplete: %ld/%ld)\n",
                            color_start("yellow"), color_reset(), name, final, fsz);
                count_one(sh, 3);
            }
        } else if (sc == 416) {
            LOG_I("Server: Range not satisfiable - file '%s' complete.", name);
            status_line("  %s[DONE]%s %s (server says Range not satisfiable)\n",
                        color_start("green"), color_reset(), name);
            count_one(sh, 2);
        } else {
            LOG_E("HTTP error %d for file '%s'.", sc, name);
            status_line("  %s[ERR]%s  %s (HTTP %d)\n",
                        color_start("red"), color_reset(), name, sc);
            count_one(sh, 3);
        }
    }
}