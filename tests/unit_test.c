#define _CRT_SECURE_NO_WARNINGS
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Deterministic unit tests (no network). Link this against src/util.c and run
 * on any platform. Exits 0 on success, 1 on failure.
 */

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do {                                            \
    g_checks++;                                                     \
    if (!(cond)) {                                                  \
        printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        g_failures++;                                               \
    }                                                               \
} while (0)

#define CHECK_STR(actual, expected) do {                            \
    const char *a = (actual);                                       \
    const char *e = (expected);                                     \
    g_checks++;                                                     \
    if (strcmp(a, e) != 0) {                                        \
        printf("FAIL %s:%d  got \"%s\" expected \"%s\"\n",          \
               __FILE__, __LINE__, a, e);                           \
        g_failures++;                                               \
    }                                                               \
} while (0)

static void test_url_encode_path(void) {
    char buf[512];

    /* Spaces are the core regression: curl rc=3 on raw spaces. */
    url_encode_path("hello world.zip", buf, sizeof(buf));
    CHECK_STR(buf, "hello%20world.zip");

    /* Parens and brackets (TOSEC-style names) must be encoded. */
    url_encode_path("B-1 Nuclear Bomber (1983)(Avalon Hill) [Documentation].zip",
                    buf, sizeof(buf));
    CHECK_STR(buf,
        "B-1%20Nuclear%20Bomber%20%281983%29%28Avalon%20Hill%29%20"
        "%5BDocumentation%5D.zip");
    CHECK(strlen(buf) < sizeof(buf));

    /* Sub-directory separators must be preserved. */
    url_encode_path("history/files/covers 2024/logo_header.jpg.~1~",
                    buf, sizeof(buf));
    CHECK_STR(buf,
        "history/files/covers%202024/logo_header.jpg.~1~");

    /* Unreserved characters pass through untouched. */
    url_encode_path("abc-_.~XYZ0123", buf, sizeof(buf));
    CHECK_STR(buf, "abc-_.~XYZ0123");

    /* '+' and '&' and '=' are reserved for query strings, encode them. */
    url_encode_path("a+b & c=d", buf, sizeof(buf));
    CHECK_STR(buf, "a%2Bb%20%26%20c%3Dd");

    /* Non-ASCII bytes get percent-encoded (UTF-8). */
    url_encode_path("M\xc3\xbcnchen.txt", buf, sizeof(buf));
    CHECK_STR(buf, "M%C3%BCnchen.txt");

    /* Truncation safety: tiny buffer must not overflow and returns -1. */
    CHECK(url_encode_path("toolong", buf, 1) == -1);
    CHECK(url_encode_path("toolong", buf, 2) == -1);
    /* Exact-fit buffer succeeds. */
    CHECK(url_encode_path("hi", buf, 3) == 2);
    CHECK_STR(buf, "hi");

    /* Encoded length is returned correctly. */
    CHECK(url_encode_path("a b", buf, sizeof(buf)) == 5);  /* %20 = 3 chars */
}

static void test_extract_identifier(void) {
    char id[128];
    CHECK(extract_identifier("https://archive.org/download/my_item", id, sizeof(id)) == 0);
    CHECK_STR(id, "my_item");

    CHECK(extract_identifier("https://archive.org/details/my_item/files", id, sizeof(id)) == 0);
    CHECK_STR(id, "my_item");

    CHECK(extract_identifier("https://archive.org/details/my_item/", id, sizeof(id)) == 0);
    CHECK_STR(id, "my_item");

    /* Trailing slash / query. */
    CHECK(extract_identifier("https://archive.org/download/foo.bar-baz/", id, sizeof(id)) == 0);
    CHECK_STR(id, "foo.bar-baz");

    /* Not an archive.org URL (no /details/ or /download/ marker). */
    CHECK(extract_identifier("https://example.com/foo/bar", id, sizeof(id)) == -1);
}

static void test_wildcard_match(void) {
    CHECK(wildcard_match("test.zip", "*.zip"));
    CHECK(wildcard_match("a.png", "*.{jpg,png}"));
    CHECK(!wildcard_match("a.gif", "*.{jpg,png}"));
    CHECK(wildcard_match("dir/file.txt", "*/file.txt"));
    CHECK(!wildcard_match("dir/file.txt", "*/file.bin"));
    CHECK(wildcard_match("abc", "a?c"));
    CHECK(!wildcard_match("abcd", "a?c"));
    CHECK(wildcard_match("Demos (TOSEC-v2018-03-08).zip", "*Demos*"));
    CHECK(wildcard_match("readme.txt", "readme.*"));
    CHECK(wildcard_match("", "*"));
    CHECK(!wildcard_match("abc.txt", "a?c"));
}

static void test_fmt_size_buffer(void) {
    char buf[32];
    fmt_size(buf, sizeof(buf), 500);
    CHECK_STR(buf, "500 B");
    fmt_size(buf, sizeof(buf), 2048);
    CHECK_STR(buf, "2.0 KB");
    fmt_size(buf, sizeof(buf), 1572864); /* 1.5 MiB */
    CHECK_STR(buf, "1.5 MB");
}

int main(void) {
    test_url_encode_path();
    test_extract_identifier();
    test_wildcard_match();
    test_fmt_size_buffer();

    printf("\nUnit tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}