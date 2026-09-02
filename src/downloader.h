#ifndef DOWNLOADER_H
#define DOWNLOADER_H

#include <stddef.h>

#include "thrd.h"

/* Aggregate result counters for a batch download. */
typedef struct {
    int downloaded;
    int skipped;
    int failed;
    int restricted;
    int filtered;
} DownloadStats;

/*
 * Download every non-restricted, non-filtered file from an archive listing
 * into `dest`.
 *
 * Auto-resume: if a local file already exists with a size smaller than the
 * server-side size, the download resumes using an HTTP Range request.
 *
 * @param identifier  Archive identifier (used to build download URLs).
 * @param dest        Destination folder (created if it does not exist).
 * @param names       Array of null-terminated file names.
 * @param count       Number of entries in `names`.
 * @param sizes       Parallel array of expected sizes (bytes), 0 = unknown.
 * @param restricted  Parallel array; if non-NULL, entries with value != 0 are
 *                    skipped as restricted.
 * @param filtered    Parallel array; if non-NULL, entries with value != 0 are
 *                    skipped as filtered out (e.g. by a file-type/glob flag).
 * @param threads     Number of parallel download workers (>= 1).
 * @param stats       Out-param collecting per-category totals.
 * @return 0 on success (no failures), non-zero otherwise.
 */
int download_all(const char *identifier, const char *dest,
                 const char *const *names, const double *sizes,
                 const int *restricted, const int *filtered, size_t count,
                 int threads, DownloadStats *stats);

#endif /* DOWNLOADER_H */