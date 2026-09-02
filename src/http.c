#define _CRT_SECURE_NO_WARNINGS
#include "http.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
/* ==================== WinHTTP implementation (Windows) ==================== */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

/* Cookie header ("a=1; b=2") attached to every request when authenticated. */
static char g_cookie_header[8300] = {0};

void http_set_cookie_header(const char *cookie_header) {
    if (cookie_header) {
        snprintf(g_cookie_header, sizeof(g_cookie_header), "%s", cookie_header);
        LOG_D("Cookie header set (%zu bytes).", strlen(g_cookie_header));
    } else {
        g_cookie_header[0] = 0;
        LOG_D("Cookie header cleared.");
    }
}

/* Add the Cookie header (if any) to an open request handle. */
static void http_add_cookie(HINTERNET hRequest) {
    if (!g_cookie_header[0]) return;
    char full[8310];
    snprintf(full, sizeof(full), "Cookie: %s", g_cookie_header);
    wchar_t w[8310];
    MultiByteToWideChar(CP_UTF8, 0, full, -1, w, 8310);
    if (!WinHttpAddRequestHeaders(hRequest, w, (DWORD)wcslen(w),
                                  WINHTTP_ADDREQ_FLAG_ADD)) {
        LOG_W("WinHttpAddRequestHeaders for Cookie failed (err=%lu).",
              GetLastError());
    }
}

void http_init(void) {
    LOG_D("Platform: Windows - WinHTTP needs no global init.");
}

void http_cleanup(void) {
    /* WinHTTP global cleanup not required. */
}

int http_post_form(const char *url_w, const char *form_data, Buffer *out) {
    LOG_T("http_post_form() ENTER url='%s'", url_w);
    wchar_t wurl[4096];
    MultiByteToWideChar(CP_UTF8, 0, url_w, -1, wurl, 4096);

    URL_COMPONENTS uc = {0};
    wchar_t scheme[16] = {0}, host[2048] = {0}, path[4096] = {0};
    uc.dwStructSize = sizeof(uc);
    uc.lpszScheme = scheme;   uc.dwSchemeLength = 16;
    uc.lpszHostName = host;   uc.dwHostNameLength = 2048;
    uc.lpszUrlPath = path;    uc.dwUrlPathLength = 4096;
    if (!WinHttpCrackUrl(wurl, (DWORD)wcslen(wurl), 0, &uc)) {
        LOG_E("WinHttpCrackUrl failed for URL '%s'", url_w);
        return -1;
    }

    HINTERNET hSession = WinHttpOpen(L"ArchiveDownloader/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     NULL, NULL, 0);
    if (!hSession) {
        LOG_E("WinHttpOpen failed (GetLastError=%lu).", GetLastError());
        return -1;
    }
    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect) {
        LOG_E("WinHttpConnect failed (GetLastError=%lu).", GetLastError());
        WinHttpCloseHandle(hSession);
        return -1;
    }
    DWORD flags = WINHTTP_FLAG_REFRESH;
    if (uc.nScheme == INTERNET_SCHEME_HTTPS) flags |= WINHTTP_FLAG_SECURE;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path, NULL,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        LOG_E("WinHttpOpenRequest failed (GetLastError=%lu).", GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    static const wchar_t ctype[] = L"Content-Type: application/x-www-form-urlencoded";
    WinHttpAddRequestHeaders(hRequest, ctype, (DWORD)wcslen(ctype),
                             WINHTTP_ADDREQ_FLAG_ADD);

    size_t body_len = strlen(form_data);
    LOG_T("Sending POST form to '%ls' (%zu bytes).", path, body_len);
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            (LPVOID)form_data, (DWORD)body_len,
                            (DWORD)body_len, 0)) {
        LOG_E("WinHttpSendRequest failed (GetLastError=%lu).", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        LOG_E("WinHttpReceiveResponse failed (GetLastError=%lu).", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    DWORD code = 0, code_sz = sizeof(code);
    if (!WinHttpQueryHeaders(hRequest,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &code, &code_sz, WINHTTP_NO_HEADER_INDEX)) {
        LOG_W("Could not query status code (GetLastError=%lu).", GetLastError());
    }
    LOG_I("HTTP response (POST): %lu", (unsigned long)code);

    DWORD br = 0;
    char chunk[65536];
    for (;;) {
        if (!WinHttpReadData(hRequest, chunk, sizeof(chunk), &br)) {
            LOG_W("WinHttpReadData failed (GetLastError=%lu).", GetLastError());
            break;
        }
        if (br == 0) break;
        if (out) buf_append(out, chunk, br);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return (int)code;
}

int http_get(const char *url_w, Buffer *out, const char *range_hdr,
             HttpWriteCallback write_cb, void *user) {
    LOG_T("http_get() ENTER url='%s' write_cb=%s user=%s", url_w,
          write_cb ? "yes" : "no", user ? "yes" : "no");

    wchar_t wurl[4096];
    MultiByteToWideChar(CP_UTF8, 0, url_w, -1, wurl, 4096);

    LOG_D("Cracking URL '%s' into components.", url_w);
    URL_COMPONENTS uc = {0};
    wchar_t scheme[16] = {0}, host[2048] = {0}, path[4096] = {0};
    uc.dwStructSize = sizeof(uc);
    uc.lpszScheme = scheme;   uc.dwSchemeLength = 16;
    uc.lpszHostName = host;   uc.dwHostNameLength = 2048;
    uc.lpszUrlPath = path;    uc.dwUrlPathLength = 4096;
    if (!WinHttpCrackUrl(wurl, (DWORD)wcslen(wurl), 0, &uc)) {
        LOG_E("WinHttpCrackUrl failed for URL '%s'", url_w);
        return -1;
    }
    LOG_D("Parsed components: host='%ls' port=%lu scheme=%d path='%ls'",
          host, uc.nPort, uc.nScheme, path);

    HINTERNET hSession = WinHttpOpen(L"ArchiveDownloader/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     NULL, NULL, 0);
    if (!hSession) {
        LOG_E("WinHttpOpen failed (GetLastError=%lu).", GetLastError());
        return -1;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect) {
        LOG_E("WinHttpConnect failed (GetLastError=%lu).", GetLastError());
        WinHttpCloseHandle(hSession);
        return -1;
    }

    DWORD flags = WINHTTP_FLAG_REFRESH;
    if (uc.nScheme == INTERNET_SCHEME_HTTPS) flags |= WINHTTP_FLAG_SECURE;
    LOG_D("Opening request with flags 0x%lX, scheme=%s.", flags,
          uc.nScheme == INTERNET_SCHEME_HTTPS ? "HTTPS" : "HTTP");

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        LOG_E("WinHttpOpenRequest failed (GetLastError=%lu).", GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    http_add_cookie(hRequest);

    if (range_hdr) {
        /* range_hdr arrives as "bytes=N-"; WinHTTP needs a full "Range: ..." header */
        char full_header[128];
        snprintf(full_header, sizeof(full_header), "Range: %s", range_hdr);
        LOG_D("Adding Range header: '%s'", full_header);
        wchar_t wrange[128];
        MultiByteToWideChar(CP_UTF8, 0, full_header, -1, wrange, 128);
        if (!WinHttpAddRequestHeaders(hRequest, wrange,
                                      (DWORD)wcslen(wrange),
                                      WINHTTP_ADDREQ_FLAG_ADD)) {
            LOG_W("WinHttpAddRequestHeaders for Range failed (err=%lu).",
                  GetLastError());
        }
    }

    LOG_T("Sending request: GET '%ls'.", path);
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        LOG_E("WinHttpSendRequest failed (GetLastError=%lu).", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    LOG_T("Waiting for response.");
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        LOG_E("WinHttpReceiveResponse failed (GetLastError=%lu).", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    DWORD code = 0, code_sz = sizeof(code);
    if (!WinHttpQueryHeaders(hRequest,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &code, &code_sz, WINHTTP_NO_HEADER_INDEX)) {
        LOG_W("Could not query status code (GetLastError=%lu).", GetLastError());
    }
    LOG_I("HTTP response: %lu %s", (unsigned long)code,
          (code == 200) ? "OK" :
          (code == 206) ? "Partial Content" :
          (code == 416) ? "Range Not Satisfiable" : "");

    /* Server ignored our Range and sent full 200 body: overwrite from byte 0 */
    if (write_cb && user && code == 200 && range_hdr) {
        DownloadContext *dc = (DownloadContext *)user;
        LOG_W("Server ignored Range request (returned 200). Reopening file '%s' in overwrite mode.",
              dc->file_name ? dc->file_name : "(memory)");
        if (dc->file_name) {
            dc->fp = freopen(dc->file_name, "wb", dc->fp);
            if (!dc->fp) LOG_E("freopen('%s') failed.", dc->file_name);
        } else {
            fseek(dc->fp, 0, SEEK_SET);
        }
        LOG_D("Resetting resume_from=0, downloaded=0.");
        dc->resume_from = 0;
        dc->downloaded = 0;
    }

    LOG_T("Reading response body.");
    DWORD br = 0;
    char chunk[65536];
    long total_read = 0;
    for (;;) {
        if (!WinHttpReadData(hRequest, chunk, sizeof(chunk), &br)) {
            LOG_W("WinHttpReadData failed (GetLastError=%lu).", GetLastError());
            break;
        }
        if (br == 0) break;
        total_read += (long)br;
        if (write_cb) write_cb(chunk, br, user);
        else if (out)   buf_append(out, chunk, br);
    }
    LOG_D("Read %ld bytes total from response body.", total_read);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    LOG_T("WinHTTP handles closed. Returning status %u.", (unsigned)code);
    return (int)code;
}

#else
/* ==================== libcurl implementation (POSIX) ==================== */
#include <curl/curl.h>

/* g_curl_status is written by the header callback and read by the file write
   callback during a transfer; it must be per-thread so concurrent downloads
   (--threads) do not race on it. */
static _Thread_local int g_curl_status = 0;
static char g_cookie_header[8300] = {0};

void http_set_cookie_header(const char *cookie_header) {
    if (cookie_header) {
        snprintf(g_cookie_header, sizeof(g_cookie_header), "%s", cookie_header);
        LOG_D("Cookie header set (%zu bytes).", strlen(g_cookie_header));
    } else {
        g_cookie_header[0] = 0;
        LOG_D("Cookie header cleared.");
    }
}

struct CURLMemory { char *data; size_t size; };

/* Bundles the caller's write callback (e.g. progress_display, which prints
   per-chunk speed/ETA) with its opaque user pointer so the libcurl file
   write callback can delegate to it, matching the WinHTTP backend behaviour. */
typedef struct {
    HttpWriteCallback cb;
    void *user;
} CurlWriteShim;

static size_t curl_header_cb(char *buffer, size_t size, size_t nitems, void *user) {
    size_t b = size * nitems;
    (void)user;
    LOG_T("curl_header_cb: header line (%zu bytes)", b);
    if (b >= 12 && strncmp(buffer, "HTTP/", 5) == 0) {
        g_curl_status = atoi(buffer + 9);
    }
    return b;
}

static size_t curl_write_cb(void *ptr, size_t sz, size_t n, void *ud) {
    struct CURLMemory *m = (struct CURLMemory *)ud;
    size_t b = sz * n;
    char *new = realloc(m->data, m->size + b + 1);
    if (!new) return 0;
    m->data = new;
    memcpy(m->data + m->size, ptr, b);
    m->size += b;
    m->data[m->size] = 0;
    return b;
}

static size_t curl_file_cb(void *ptr, size_t sz, size_t n, void *ud) {
    CurlWriteShim *s = (CurlWriteShim *)ud;
    DownloadContext *ctx = (DownloadContext *)s->user;
    /* Server ignored our Range and sent full 200 body: overwrite from byte 0 */
    if (ctx->resume_from > 0 && g_curl_status == 200) {
        LOG_W("Server ignored Range request (returned 200). Reopening '%s' in overwrite mode.",
              ctx->file_name ? ctx->file_name : "(none)");
        if (ctx->file_name) ctx->fp = freopen(ctx->file_name, "wb", ctx->fp);
        ctx->resume_from = 0;
        ctx->downloaded = 0;
    }
    size_t b = sz * n;
    if (s->cb) s->cb(ptr, b, s->user);
    return b;
}

void http_init(void) {
    LOG_D("Platform: POSIX - initializing libcurl globally.");
    curl_global_init(CURL_GLOBAL_ALL);
}

void http_cleanup(void) {
    curl_global_cleanup();
}

int http_post_form(const char *url, const char *form_data, Buffer *out) {
    LOG_T("http_post_form() ENTER url='%s'", url);
    CURL *c = curl_easy_init();
    if (!c) {
        LOG_E("curl_easy_init failed.");
        return -1;
    }
    struct CURLMemory mem = {0};
    g_curl_status = 0;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, form_data);
    curl_easy_setopt(c, CURLOPT_COPYPOSTFIELDS, form_data);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &mem);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    if (g_cookie_header[0])
        curl_easy_setopt(c, CURLOPT_COOKIE, g_cookie_header);

    LOG_T("Performing POST form to URL: %s", url);
    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) {
        LOG_E("curl_easy_perform failed: %s (rc=%d)",
              curl_easy_strerror(rc), (int)rc);
        curl_easy_cleanup(c);
        free(mem.data);
        return -1;
    }
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    LOG_I("HTTP response (POST): %ld", code);
    curl_easy_cleanup(c);

    if (out) {
        out->data = mem.data;
        out->size = mem.size;
        out->capacity = mem.size;
    } else {
        free(mem.data);
    }
    return (int)code;
}

int http_get(const char *url, Buffer *out, const char *range,
             HttpWriteCallback write_cb, void *user) {
    LOG_T("http_get() ENTER url='%s' write_cb=%s user=%s", url,
          write_cb ? "yes" : "no", user ? "yes" : "no");
    CURL *c = curl_easy_init();
    if (!c) {
        LOG_E("curl_easy_init failed.");
        return -1;
    }
    struct CURLMemory mem = {0};
    CurlWriteShim shim = { write_cb, user };
    g_curl_status = 0;
    LOG_D("Setting libcurl options for URL: %s", url);
    if (range) LOG_D("Requesting byte range: %s", range);

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, curl_header_cb);
    if (g_cookie_header[0])
        curl_easy_setopt(c, CURLOPT_COOKIE, g_cookie_header);
    if (range) {
        /* CURLOPT_RANGE expects "N-" without the "bytes=" unit prefix */
        const char *cur = range;
        if (strncmp(cur, "bytes=", 6) == 0) cur += 6;
        LOG_D("Setting libcurl range: '%s'", cur);
        curl_easy_setopt(c, CURLOPT_RANGE, cur);
    }
    if (write_cb) {
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_file_cb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &shim);
    } else {
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &mem);
    }

    LOG_T("Performing HTTP request...");
    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) {
        LOG_E("curl_easy_perform failed: %s (rc=%d)",
              curl_easy_strerror(rc), (int)rc);
        curl_easy_cleanup(c);
        free(mem.data);
        return -1;
    }

    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    LOG_I("HTTP response: %ld %s", code,
          (code == 200) ? "OK" :
          (code == 206) ? "Partial Content" :
          (code == 416) ? "Range Not Satisfiable" : "");
    curl_easy_cleanup(c);

    if (!write_cb && out) {
        out->data = mem.data;
        out->size = mem.size;
        out->capacity = mem.size;
    } else {
        free(mem.data);
    }
    LOG_T("http_get() EXIT with code %d.", (int)code);
    return (int)code;
}

#endif /* _WIN32 */