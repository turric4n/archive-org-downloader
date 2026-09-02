#ifndef ARCHIVE_H
#define ARCHIVE_H

#include <stddef.h>

/* A single file entry in an archive.org collection manifest. */
typedef struct {
    char *name;       /* Full item path (may contain '/') */
    char *private;    /* NULL, or "true" if restricted */
    double size;      /* Bytes; 0 if not present in metadata */
} ArchiveFile;

/* The parsed file listing for one archive identifier. */
typedef struct {
    char *identifier;
    ArchiveFile *files;
    size_t count;
} ArchiveListing;

/*
 * Fetch the file manifest for an archive.org identifier and parse it.
 *
 * @param identifier  e.g. "total-dos-collection-b"
 * @param listing     Out-param; populated on success. Caller must call
 *                    archive_listing_free().
 * @return 0 on success, non-zero on failure.
 */
int archive_fetch(const char *identifier, ArchiveListing *listing);

/* Free all memory owned by a listing previously filled by archive_fetch(). */
void archive_listing_free(ArchiveListing *listing);

#endif /* ARCHIVE_H */