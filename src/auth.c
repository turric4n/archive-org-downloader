#define _CRT_SECURE_NO_WARNINGS
#include "auth.h"
#include "http.h"
#include "log.h"
#include "util.h"
#include "parson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOGIN_URL "https://archive.org/services/xauthn/?op=login"
#define WHOAMI_URL "https://archive.org/account/login"
#define CONFIG_SECTION "cookies"
#define S3_SECTION "s3"

/* ---- module state ---- */
static char g_user[4096] = {0};
static char g_sig[4096] = {0};
static char g_screenname[4096] = {0};
static char g_s3_access[1024] = {0};
static char g_s3_secret[1024] = {0};
static char g_cookie[8300] = {0};
static int  g_authenticated = 0;

static void rebuild_cookie(void) {
    if (g_user[0] || g_sig[0]) {
        snprintf(g_cookie, sizeof(g_cookie), "logged-in-user=%s; logged-in-sig=%s",
                 g_user, g_sig);
    } else {
        g_cookie[0] = 0;
    }
    if (g_user[0] && g_sig[0]) {
        LOG_D("Authentication state: logged in as '%s'.", g_user);
    } else {
        LOG_D("Authentication state: not fully authenticated.");
    }
}

const char *auth_cookie_header(void) {
    return g_cookie[0] ? g_cookie : NULL;
}

const char *auth_email(void) {
    return g_user[0] ? g_user : NULL;
}

int auth_is_authenticated(void) {
    return g_authenticated;
}

/* Percent-encode a string for safe inclusion in application/x-www-form-urlencoded. */
static void url_encode(const char *in, char *out, size_t out_sz) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            if (o + 1 < out_sz) out[o++] = (char)c;
        } else {
            if (o + 3 < out_sz) {
                out[o++] = '%';
                out[o++] = hex[c >> 4];
                out[o++] = hex[c & 0xF];
            }
        }
    }
    out[o] = 0;
}

/*
 * The xauthn login response returns cookie "values" that are in the form of a
 * full Set-Cookie string, e.g.:
 *
 *   user%40example.com; domain=.archive.org; path=/; expires=...
 *
 * Only the leading "value" portion (before the first ';') is a cookie value we
 * must echo back in the Cookie header. This helper copies that value (keeping
 * its URL-encoding intact, as the server expects) into `out`. RFC 1738/3986
 * unreserved characters are left as-is. Returns the stripped result.
 */
static const char *strip_cookie_value(const char *raw) {
    static char buf[8192];
    size_t n = 0;
    for (; raw && raw[n] && raw[n] != ';'; n++) {
        if (n + 1 >= sizeof(buf)) break;
        buf[n] = raw[n];
    }
    buf[n] = 0;
    /* Trim trailing whitespace/CR that may precede the ';'. */
    while (n > 0 && (buf[n-1] == ' ' || buf[n-1] == '\t' || buf[n-1] == '\r'))
        buf[--n] = 0;
    return buf;
}

/* Copy a possibly full Set-Cookie string's leading value into a fixed buffer. */
static void copy_cookie_value(char *dst, size_t dst_sz, const char *raw) {
    const char *v = strip_cookie_value(raw);
    snprintf(dst, dst_sz, "%s", v);
}

int auth_login(const char *email, const char *password) {
    if (!email || !password) {
        LOG_E("Login requires both email and password.");
        return -1;
    }

    char enc_email[4096], enc_pass[4096];
    url_encode(email, enc_email, sizeof(enc_email));
    url_encode(password, enc_pass, sizeof(enc_pass));

    char form[9000];
    snprintf(form, sizeof(form), "email=%s&password=%s", enc_email, enc_pass);
    LOG_D("Posting login form to xauthn (email=%s).", email);

    Buffer resp = {0};
    int code = http_post_form(LOGIN_URL, form, &resp);
    if (code != 200 || !resp.data) {
        LOG_E("Login request failed (HTTP %d).", code);
        buf_free(&resp);
        return -1;
    }

    LOG_D("Parsing login response (%zu bytes).", resp.size);
    JSON_Value *root = json_parse_string(resp.data);
    buf_free(&resp);
    if (!root) {
        LOG_E("Login response was not valid JSON.");
        return -1;
    }

    JSON_Object *root_obj = json_value_get_object(root);
    JSON_Value *success = root_obj
        ? json_object_get_value(root_obj, "success") : NULL;
    int ok = success &&
             (json_value_get_type(success) == JSONBoolean) &&
             json_value_get_boolean(success);
    if (!ok) {
        const char *reason = root_obj
            ? json_object_dotget_string(root_obj, "values.reason") : NULL;
        if (!reason) reason = root_obj ? json_object_get_string(root_obj, "error") : NULL;
        LOG_E("Login rejected by archive.org: %s",
              reason ? reason : "unknown error");
        json_value_free(root);
        return -1;
    }

    const char *user = json_object_dotget_string(root_obj, "values.cookies.logged-in-user");
    const char *sig  = json_object_dotget_string(root_obj, "values.cookies.logged-in-sig");
    const char *sn   = json_object_dotget_string(root_obj, "values.screenname");
    const char *acc  = json_object_dotget_string(root_obj, "values.s3.access");
    const char *sec  = json_object_dotget_string(root_obj, "values.s3.secret");

    if (!user || !sig) {
        LOG_E("Login response was missing session cookies.");
        json_value_free(root);
        return -1;
    }

    /* Strip per-cookie attributes and store only the value portion so that the
       reconstructed Cookie header is valid on this and every later session. */
    copy_cookie_value(g_user, sizeof(g_user), user);
    copy_cookie_value(g_sig, sizeof(g_sig), sig);
    snprintf(g_screenname, sizeof(g_screenname), "%s", sn ? sn : "");
    snprintf(g_s3_access, sizeof(g_s3_access), "%s", acc ? acc : "");
    snprintf(g_s3_secret, sizeof(g_s3_secret), "%s", sec ? sec : "");
    if (!g_user[0] || !g_sig[0]) {
        LOG_E("Cannot persist: login response contained empty session cookies.");
        json_value_free(root);
        return -1;
    }
    g_authenticated = 1;
    rebuild_cookie();

    LOG_I("Login successful. Screenname='%s'.", sn ? sn : email);
    json_value_free(root);
    return 0;
}

int auth_save(const char *path) {
    if (!g_authenticated) {
        LOG_E("Nothing to save - not authenticated.");
        return -1;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        LOG_E("Cannot write config file: %s", path);
        return -1;
    }
    fprintf(f, "[s3]\n");
    fprintf(f, "access = %s\n", g_s3_access);
    fprintf(f, "secret = %s\n", g_s3_secret);
    fprintf(f, "\n[cookies]\n");
    fprintf(f, "logged-in-user = %s\n", g_user);
    fprintf(f, "logged-in-sig = %s\n", g_sig);
    fprintf(f, "\n[general]\n");
    fprintf(f, "screenname = %s\n", g_screenname);
    fclose(f);
    LOG_I("Credentials saved to: %s", path);
    return 0;
}

int auth_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        LOG_I("No credentials file found at: %s", path);
        return -1;
    }
    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        /* strip trailing newline */
        line[strcspn(line, "\r\n")] = 0;
        char key[512], val[1024];
        if (sscanf(line, "%511[^=] = %1023[^\r\n]", key, val) == 2) {
            /* trim spaces around key */
            char *k = key;
            while (*k == ' ' || *k == '\t') k++;
            char *ke = k + strlen(k);
            while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t')) *--ke = 0;
            if (strcmp(k, "logged-in-user") == 0)
                copy_cookie_value(g_user, sizeof(g_user), val);
            else if (strcmp(k, "logged-in-sig") == 0)
                copy_cookie_value(g_sig, sizeof(g_sig), val);
            else if (strcmp(k, "screenname") == 0)
                snprintf(g_screenname, sizeof(g_screenname), "%s", val);
            else if (strcmp(k, "access") == 0)
                snprintf(g_s3_access, sizeof(g_s3_access), "%s", val);
            else if (strcmp(k, "secret") == 0)
                snprintf(g_s3_secret, sizeof(g_s3_secret), "%s", val);
        }
    }
    fclose(f);

    if (g_user[0] && g_sig[0]) {
        g_authenticated = 1;
        rebuild_cookie();
        LOG_I("Loaded credentials for '%s' from: %s", g_user, path);
        return 0;
    }
    LOG_W("Credentials file incomplete (missing cookies).");
    return -1;
}

int auth_whoami(void) {
    if (!g_authenticated || !g_cookie[0]) {
        LOG_W("auth_whoami called without an authenticated session.");
        return -1;
    }
    /* Point a probe request at the authenticated-account landing page. If the
       session cookies are still accepted, this returns HTTP 200. */
    int code = http_get(WHOAMI_URL, NULL, NULL, NULL, NULL);
    if (code == 200) {
        LOG_I("Session cookies still valid.");
        return 0;
    }
    LOG_W("Session cookies rejected (HTTP %d) - the stored login has likely expired.",
          code);
    return -1;
}

void auth_logout(const char *path) {
    if (path) {
        if (remove(path) == 0) {
            LOG_I("Removed credentials file: %s", path);
        } else {
            LOG_W("Could not remove credentials file %s (may not exist).", path);
        }
    }
    g_user[0] = g_sig[0] = g_screenname[0] = 0;
    g_s3_access[0] = g_s3_secret[0] = g_cookie[0] = 0;
    g_authenticated = 0;
    http_set_cookie_header(NULL);
    LOG_I("Logged out - credentials cleared.");
}

void auth_close(void) {
    http_set_cookie_header(NULL);
    g_user[0] = g_sig[0] = g_screenname[0] = 0;
    g_s3_access[0] = g_s3_secret[0] = g_cookie[0] = 0;
    g_authenticated = 0;
    LOG_D("Auth state cleared.");
}