#define _CRT_SECURE_NO_WARNINGS
#include "archive.h"
#include "http.h"
#include "log.h"
#include "util.h"
#include "parson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int archive_fetch(const char *identifier, ArchiveListing *listing) {
    char meta_url[1024];
    snprintf(meta_url, sizeof(meta_url),
             "https://archive.org/metadata/%s/files", identifier);
    LOG_I("Fetching file list from metadata API: %s", meta_url);

    Buffer resp = {0};
    int code = http_get(meta_url, &resp, NULL, NULL, NULL);
    if (code != 200 || !resp.data) {
        LOG_E("Failed to fetch file list (HTTP %d).", code);
        buf_free(&resp);
        return -1;
    }
    LOG_D("Received %zu bytes of metadata JSON.", resp.size);

    JSON_Value *root = json_parse_string(resp.data);
    buf_free(&resp);
    if (!root) {
        LOG_E("Invalid JSON in metadata response.");
        return -1;
    }
    LOG_D("JSON parsed successfully.");

    JSON_Object *root_obj = json_value_get_object(root);
    JSON_Array *arr = json_object_get_array(root_obj, "result");
    if (!arr) {
        LOG_E("No 'result' array found in metadata response.");
        json_value_free(root);
        return -1;
    }

    size_t count = json_array_get_count(arr);
    LOG_I("Found %zu files in archive metadata.", count);

    listing->identifier = str_dup(identifier);
    listing->files = (count > 0) ? calloc(count, sizeof(ArchiveFile)) : NULL;
    listing->count = count;

    for (size_t i = 0; i < count; i++) {
        JSON_Object *f = json_array_get_object(arr, i);
        if (!f) {
            LOG_W("Metadata item %zu is not an object; skipped.", i);
            listing->files[i].name = str_dup("");
            continue;
        }

        const char *name = json_object_get_string(f, "name");
        const char *priv = json_object_get_string(f, "private");
        JSON_Value *szval = json_object_get_value(f, "size");
        double sz = 0;
        if (szval && json_value_get_type(szval) == JSONString)
            sz = atof(json_value_get_string(szval));
        else if (szval && json_value_get_type(szval) == JSONNumber)
            sz = json_value_get_number(szval);

        listing->files[i].name = str_dup(name ? name : "");
        listing->files[i].private = str_dup(priv);
        listing->files[i].size = sz;
    }

    json_value_free(root);
    return 0;
}

void archive_listing_free(ArchiveListing *listing) {
    if (!listing) return;
    for (size_t i = 0; i < listing->count; i++) {
        free(listing->files[i].name);
        free(listing->files[i].private);
    }
    free(listing->files);
    free(listing->identifier);
    listing->files = NULL;
    listing->identifier = NULL;
    listing->count = 0;
}