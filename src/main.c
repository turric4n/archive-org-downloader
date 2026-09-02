#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "color.h"
#include "util.h"
#include "http.h"
#include "auth.h"
#include "archive.h"
#include "downloader.h"

/* Version of this build. Override at compile time with -DVERSION="..." (e.g. a
 * release tag or commit). Defaults to "dev" for local builds. */
#ifndef VERSION
#define VERSION "dev"
#endif

static void usage(const char *prog) {
    printf("Archive.org Downloader with Auto-Resume\n\n");
    printf("Usage: %s [options] <source_url> [destination_folder]\n", prog);
    printf("       If destination_folder is omitted it defaults to\n");
    printf("       './<archive-id>' (e.g. './total-dos-collection-b').\n");
    printf("       %s --login [email] [password]   (standalone: authorise only)\n", prog);
    printf("       %s --logout                     (remove saved credentials)\n\n", prog);
    printf("Options:\n");
    printf("  -v, --verbose          Show detailed logging\n");
    printf("  -vv, --trace           Show trace-level logging (HTTP internals)\n");
    printf("  -V, --version          Print the version string and exit\n");
    printf("  --login                Log in to archive.org (unlocks restricted collections)\n");
    printf("  --logout               Remove the saved credentials file\n");
    printf("  --config <path>        Use a custom credentials file (default ~/.ia)\n");
    printf("  --email <address>      Supply login email non-interactively\n");
    printf("  --password <pass>      Supply login password non-interactively\n");
    printf("  --type <glob>          Only download files matching a type/glob\n");
    printf("                         (e.g. \"*.zip\", \"*.{jpg,png}\", \"*/doc/*\")\n");
    printf("  --threads <n>          Download at most n files concurrently (default 1)\n");
    printf("  --color / --no-color   Force enable / disable ANSI colours\n");
    printf("\nEnvironment:\n");
    printf("  IA_EMAIL / IA_PASSWORD Can provide credentials for --login without\n");
    printf("                         exposing them on the command line or in shell history.\n");
    printf("  NO_COLOR               Disable ANSI colours (https://no-color.org/)\n");
    printf("\nCredential priority: --email/--password args, then IA_EMAIL/IA_PASSWORD,\n");
    printf("then interactive prompt.\n");
    printf("\nExamples:\n");
    printf("  %s https://archive.org/download/total-dos-collection-b ./downloads\n", prog);
    printf("  %s --login https://archive.org/download/total-dos-collection-b ./downloads\n", prog);
    printf("  %s --login                       # prompt for credentials only\n", prog);
    printf("  %s --logout                      # forget stored credentials\n", prog);
    printf("  %s -v https://archive.org/download/softwarelibrary_msdos_games ./games\n", prog);
    printf("  %s https://archive.org/download/total-dos-collection-b   # -> ./total-dos-collection-b\n", prog);
    printf("\nFeatures:\n");
    printf("  - Automatically resumes interrupted downloads\n");
    printf("  - Skips already completed files\n");
    printf("  - Shows progress with speed and ETA\n");
    printf("  - Skips restricted/private files (unless logged in)\n");
}

static int build_config_path(char *out, size_t out_sz) {
    char *home = user_home_dir();
    if (!home) return -1;
    snprintf(out, out_sz, "%s%c.ia", home, PATHSEP);
    free(home);
    return 0;
}

/* Prompt on the terminal for email + password. Returns 0 on success. */
static int prompt_credentials(char *email, size_t email_sz,
                              char *password, size_t pass_sz) {
    printf("Archive.org login\n");
    printf("Email address: ");
    fflush(stdout);
    if (!fgets(email, email_sz, stdin)) return -1;
    email[strcspn(email, "\r\n")] = 0;
    printf("Password: ");
    fflush(stdout);
    if (read_password(password, pass_sz) != 0) return -1;
    printf("\n");
    return 0;
}

/* Resolve credentials from (in order): --email/--password args, env vars,
 * then interactive prompt. Returns 0 with email/password filled. */
static int resolve_credentials(const char *arg_email, const char *arg_pass,
                               char *email, size_t email_sz,
                               char *password, size_t pass_sz) {
    const char *e = arg_email ? arg_email : getenv("IA_EMAIL");
    const char *p = arg_pass ? arg_pass : getenv("IA_PASSWORD");

    if (arg_email && arg_pass) {
        LOG_W("Warning: --email/--password expose credentials on the command line.");
    } else if (!arg_email && !arg_pass && e && p) {
        LOG_I("Using IA_EMAIL / IA_PASSWORD from environment.");
    } else if (arg_email || arg_pass) {
        LOG_E("--email and --password must be supplied together.");
        return -1;
    }

    if (e && p) {
        snprintf(email, email_sz, "%s", e);
        snprintf(password, pass_sz, "%s", p);
        return 0;
    }

    if (e && !p) {
        LOG_E("IA_EMAIL is set but IA_PASSWORD is not.");
        return -1;
    }

    LOG_I("No credentials supplied - prompting interactively.");
    return prompt_credentials(email, email_sz, password, pass_sz);
}

static int do_login(const char *cfg_path,
                    const char *arg_email, const char *arg_pass) {
    char email[512], password[512];
    if (resolve_credentials(arg_email, arg_pass,
                            email, sizeof(email),
                            password, sizeof(password)) != 0) {
        return -1;
    }

    if (auth_login(email, password) != 0) {
        LOG_E("Login failed.");
        return -1;
    }

    if (auth_save(cfg_path) != 0) {
        LOG_W("Could not persist credentials to: %s", cfg_path);
    }
    LOG_I("Login stored. You're now able to download restricted collections.");
    return 0;
}

static int do_logout(const char *cfg_path) {
    auth_logout(cfg_path);
    return 0;
}

int main(int argc, char *argv[]) {
    color_init();
    /* Honour --color / --no-color / --version before the opening banner is
     * printed so it uses the requested colour scheme and --version is clean. */
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--color") == 0) color_enable();
        else if (strcmp(argv[a], "--no-color") == 0) color_disable();
        else if (strcmp(argv[a], "-V") == 0 ||
                 strcmp(argv[a], "--version") == 0) {
            printf("archive_downloader %s\n", VERSION);
            return 0;
        }
    }
    LOG_I("==================================================");
    LOG_I("  Archive.org Downloader starting");
    LOG_I("==================================================");

    /* Parse optional flags and positional args */
    const char *source_url = NULL, *dest = NULL;
    const char *cfg_path_arg = NULL;
    const char *arg_email = NULL, *arg_pass = NULL;
    const char *type_pattern = NULL; /* optional --type glob; NULL = all files */
    int threads = 1;
    int have_src = 0, have_dest = 0;
    int want_login = 0, want_logout = 0;
    int need_val = 0; /* next token is a flag's value */
    const char *val_flag = NULL;
    for (int a = 1; a < argc; a++) {
        const char *tok = argv[a];
        if (need_val) {
            if (strcmp(val_flag, "email") == 0) arg_email = tok;
            else if (strcmp(val_flag, "password") == 0) arg_pass = tok;
            else if (strcmp(val_flag, "config") == 0) cfg_path_arg = tok;
            else if (strcmp(val_flag, "type") == 0) type_pattern = tok;
            else if (strcmp(val_flag, "threads") == 0) {
                threads = atoi(tok);
                if (threads < 1) threads = 1;
            }
            need_val = 0;
            val_flag = NULL;
            continue;
        }
        if (strcmp(tok, "-v") == 0 || strcmp(tok, "--verbose") == 0) {
            log_set_level(LOG_DEBUG);
            LOG_I("Verbose logging enabled.");
        } else if (strcmp(tok, "-vv") == 0 ||
                   strcmp(tok, "--trace") == 0) {
            log_set_level(LOG_TRACE);
            LOG_I("Trace logging enabled.");
        } else if (strcmp(tok, "--login") == 0) {
            want_login = 1;
        } else if (strcmp(tok, "--logout") == 0) {
            want_logout = 1;
        } else if (strcmp(tok, "--email") == 0) {
            need_val = 1; val_flag = "email";
        } else if (strcmp(tok, "--password") == 0) {
            need_val = 1; val_flag = "password";
        } else if (strcmp(tok, "--config") == 0) {
            need_val = 1; val_flag = "config";
        } else if (strcmp(tok, "--type") == 0) {
            need_val = 1; val_flag = "type";
        } else if (strcmp(tok, "--threads") == 0) {
            need_val = 1; val_flag = "threads";
        } else if (strcmp(tok, "--color") == 0) {
            color_enable();
        } else if (strcmp(tok, "--no-color") == 0) {
            color_disable();
        } else if (tok[0] == '-' && tok[1] != '\0') {
            LOG_W("Unknown option ignored: %s", tok);
        } else if (!have_src) {
            source_url = tok;
            have_src = 1;
        } else if (!have_dest) {
            dest = tok;
            have_dest = 1;
        } else {
            LOG_W("Ignoring unexpected extra argument: %s", tok);
        }
    }
    if (need_val) {
        LOG_E("Missing value after --%s.", val_flag);
        return 1;
    }

    /* Resolve the credentials file path (custom or default). */
    char default_path[4096];
    if (!cfg_path_arg) {
        if (build_config_path(default_path, sizeof(default_path)) != 0) {
            LOG_E("Could not determine a credentials file location.");
            return 1;
        }
        cfg_path_arg = default_path;
    }

    http_init();

    /* Standalone --logout: clear stored credentials and exit. */
    if (want_logout && !have_src && !have_dest && !want_login) {
        do_logout(cfg_path_arg);
        http_cleanup();
        return 0;
    }

    /* Standalone --login: authorise only (no download). */
    if (want_login && !have_src && !have_dest) {
        int rc = do_login(cfg_path_arg, arg_email, arg_pass) == 0 ? 0 : 1;
        http_cleanup();
        return rc;
    }

    if (!have_src) {
        LOG_E("Not enough arguments (expected a source URL).");
        usage(argv[0]);
        http_cleanup();
        return 1;
    }

    /* Derive the destination folder from the source URL when none is given,
     * e.g. ./total-dos-collection-b for the URL above. */
    char id[512];
    char default_dest[520]; /* "./" + id */
    if (extract_identifier(source_url, id, sizeof(id)) != 0) {
        LOG_E("Could not extract archive identifier from URL: %s", source_url);
        http_cleanup();
        return 1;
    }
    LOG_I("Extracted archive identifier: %s", id);

    if (!have_dest) {
        snprintf(default_dest, sizeof(default_dest), ".%c%s", PATHSEP, id);
        dest = default_dest;
        LOG_I("No destination given - using default: %s", dest);
    }

    LOG_I("Command line arguments parsed:");
    LOG_I("  source_url           = %s", source_url);
    LOG_I("  destination_folder   = %s", dest);
    LOG_I("  credentials file     = %s", cfg_path_arg);
    LOG_I("  download threads     = %d", threads);

    /* Handle --login alongside a download: authenticate and store credentials. */
    if (want_login) {
        if (do_login(cfg_path_arg, arg_email, arg_pass) != 0) {
            http_cleanup();
            return 1;
        }
    }

    /* Load previously saved credentials so restricted content is downloadable. */
    {
        if (auth_load(cfg_path_arg) == 0) {
            const char *ck = auth_cookie_header();
            if (ck) {
                http_set_cookie_header(ck);
                LOG_I("Loaded saved credentials for '%s'.", auth_email() ?
                      auth_email() : "(unknown)");
            }
        }
    }

    ArchiveListing listing;
    if (archive_fetch(id, &listing) != 0) {
        LOG_E("Failed to obtain file listing for archive '%s'.", id);
        http_cleanup();
        return 1;
    }

    /* Build parallel arrays consumed by the downloader.
     * Restricted/private files are attempted when authenticated; otherwise
     * they are skipped up-front. Files not matching --type are filtered. */
    int is_auth = auth_is_authenticated();
    const char **names = NULL;
    double *sizes = NULL;
    int *restricted = NULL;
    int *filtered = NULL;
    if (listing.count > 0) {
        names = calloc(listing.count, sizeof(char *));
        sizes = calloc(listing.count, sizeof(double));
        restricted = calloc(listing.count, sizeof(int));
        filtered = calloc(listing.count, sizeof(int));
    }
    for (size_t i = 0; i < listing.count; i++) {
        names[i] = listing.files[i].name;
        sizes[i] = listing.files[i].size;
        int priv = (listing.files[i].private &&
                    strcmp(listing.files[i].private, "true") == 0) ? 1 : 0;
        /* Only pre-skip restricted files when we aren't authenticated. */
        restricted[i] = (priv && !is_auth) ? 1 : 0;
        /* Honor a --type filter: skip names that don't match the glob. */
        if (type_pattern) {
            filtered[i] = wildcard_match(names[i], type_pattern) ? 0 : 1;
        }
    }
    if (type_pattern) {
        LOG_I("File-type filter: '%s'", type_pattern);
    }

    DownloadStats stats;
    int rc = download_all(id, dest, names, sizes, restricted, filtered,
                          listing.count, threads, &stats);

    LOG_I("==================================================");
    LOG_I("  Download summary");
    LOG_I("==================================================");
    LOG_I("  Total       : %zu files", listing.count);
    LOG_I("  Downloaded  : %d", stats.downloaded);
    LOG_I("  Skipped     : %d (already present)", stats.skipped);
    LOG_I("  Restricted  : %d", stats.restricted);
    LOG_I("  Filtered    : %d (did not match --type)", stats.filtered);
    LOG_I("  Failed      : %d", stats.failed);

    printf("\n========================================\n");
    printf("  Total    : %zu\n", listing.count);
    printf("  Downloaded: %d\n", stats.downloaded);
    printf("  Skipped  : %d\n", stats.skipped);
    printf("  Restricted: %d\n", stats.restricted);
    printf("  Filtered  : %d\n", stats.filtered);
    printf("  Failed   : %d\n", stats.failed);
    printf("========================================\n");

    free(names);
    free(sizes);
    free(restricted);
    free(filtered);
    archive_listing_free(&listing);
    auth_close();
    http_cleanup();

    LOG_I("Archive.org Downloader finished with %d failure(s).", stats.failed);
    return rc;
}