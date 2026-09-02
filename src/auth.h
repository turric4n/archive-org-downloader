#ifndef AUTH_H
#define AUTH_H

/*
 * Authentication support for archive.org restricted content.
 *
 * To download files from access-restricted collections a user must be logged
 * in.  Archive.org provides a JSON login endpoint:
 *
 *     POST https://archive.org/services/xauthn/?op=login
 *     email=<email>&password=<password>
 *
 * On success the response carries the account's S3 keys plus two session
 * cookies, `logged-in-user` and `logged-in-sig`, which the downloader must
 * send as a Cookie header so the restricted CDN servers authorise the transfer.
 *
 * The cookies (and S3 keys) are persisted to an INI-style config file so they
 * can be reused across runs.  Cookies expire after a few days, at which point
 * the user must log in again.
 */

/*
 * Perform a login against the archive.org xauthn endpoint.
 * Returns 0 on success (credentials stored in the module state).
 * Unless validate is non-zero, no network round-trip is made to check
 * the returned state (validation happens via auth_whoami).
 */
int auth_login(const char *email, const char *password);

/* Validate that the currently stored cookies still give a valid session by
 * querying the archive.org whoami endpoint. Returns 0 if the session is valid,
 * else non-zero (the stored credentials are expired/invalid). */
int auth_whoami(void);

/* Return a Cookie header value string like
 *   "logged-in-user=...; logged-in-sig=..."
 * or NULL if not authenticated.  The pointer is owned by the module and is
 * valid until auth_close() is called. */
const char *auth_cookie_header(void);

/* Return the logged-in account email, or NULL if not authenticated. */
const char *auth_email(void);

/* Flag whether the module is currently authenticated. */
int auth_is_authenticated(void);

/* Persist current credentials to the given config path. Returns 0 on success. */
int auth_save(const char *path);

/* Load credentials from the given config path. Returns 0 on success. */
int auth_load(const char *path);

/* Remove the credentials file at the given path (if any) and clear state. */
void auth_logout(const char *path);

/* Free module state. */
void auth_close(void);

#endif /* AUTH_H */