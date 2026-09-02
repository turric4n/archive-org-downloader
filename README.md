# Archive.org Downloader (C)

A C console application for downloading complete collections from archive.org with automatic resume support.

## Features

- **Auto-resume** - Interrupted downloads automatically resume from where they left off
- **Skip completed files** - Already-downloaded files are detected and skipped
- **Restricted file filtering** - Files marked as private/restricted are skipped
- **Nested directory support** - Files in subdirectories are recreated locally
- **Progress display** - Shows per-file progress with speed and ETA
- **Cross-platform** - Uses WinHTTP on Windows, libcurl on Linux/macOS

## Build

### Windows (native, no external deps)

Option A - CMake (recommended):

```bash
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

Option B - Makefile.win (MinGW):

```bash
mingw32-make -f Makefile.win CC=gcc
```

### Linux/macOS

Requires libcurl dev package:

```bash
sudo apt install libcurl4-openssl-dev   # Debian/Ubuntu
```

Option A - CMake:

```bash
cmake -B build
cmake --build build
```

Option B - Makefile:

```bash
make
```

The POSIX `Makefile` and Windows `Makefile.win` use separate object directories
(`obj/` and `build-win/` respectively), so they don't collide with the CMake
`build/` output.

## Testing

Two automated test suites run in CI on both Windows and Linux:

- **Unit tests** (`tests/unit_test.c`, deterministic, no network): verify URL
  path encoding, archive-identifier extraction, glob matching and size
  formatting. The URL-encoding tests guard against the libcurl
  `URL using bad/illegal format` (rc=3) failure caused by raw spaces, `(`/`)`
  and `[`/`]` in file names.
- **Integration test** (`tests/integration_test.sh`): runs the real binary
  against a live archive.org collection and asserts a file downloads with the
  correct size, retrying transient failures with backoff. This is a **best-effort
  smoke test and is non-blocking**: because archive.org intermittently
  blocks/rate-limits GitHub-CI datacenter IPs, an external download failure is
  usually environmental, so the script always exits `0` and reports
  `PASSED`/`WARN`/`SKIPPED`. The deterministic regression guard (the `rc=3`
  URL-encoding bug) lives in `tests/unit_test.c`, runs unconditionally on both
  platforms, and needs no network.

Run them locally:

```bash
make test                # Linux/macOS: unit + integration
mingw32-make -f Makefile.win CC=gcc test   # Windows (MinGW)
```

CI runs `test-unit` and `test-integration` as part of every push, so a broken
URL build or encoding regression fails the build before any release is cut.

## Project Layout

```
src/
  main.c        CLI entrypoint: argument parsing, orchestration
  log.h/.c      Timestamped, leveled logging (ERROR..TRACE)
  util.h/.c     Buffers, path/mkdir helpers, size formatting, identifier extraction
  color.h/.c    ANSI terminal colour detection + escape helpers
  http.h/.c     HTTP abstraction: WinHTTP (Windows) + libcurl (POSIX) backends
  auth.h/.c     Archive.org login (xauthn), credential persistence, session validation
  archive.h/.c  archive.org metadata JSON fetch/parse
  downloader.h/.c  Per-file download loop, resume detection, progress UI
  parson.h/.c   Vendored JSON parser
CMakeLists.txt
Makefile          GNU Makefile (Linux/macOS, libcurl)
Makefile.win      GNU Makefile (Windows/MinGW, WinHTTP)
```

## Usage

```
archive_downloader [-v | -vv] [--login] [--logout] [--config <path>] <source_url> [destination_folder]
```

`destination_folder` is optional. When omitted, files are downloaded into
`./<archive-id>` derived from the source URL — e.g.
`archive_downloader https://archive.org/download/total-dos-collection-b`
creates and uses `./total-dos-collection-b`.

### Options

| Flag | Description |
|------|-------------|
| `-v`, `--verbose` | Enable detailed logging (INFO + DEBUG + ERROR) |
| `-vv`, `--trace`  | Enable maximum trace logging (adds TRACE level) |
| `-V`, `--version` | Print the build version string and exit |
| `--color`         | Force-enable ANSI colours (e.g. when output is piped/redirected) |
| `--no-color`      | Force-disable ANSI colours |
| `--login`         | Log in to archive.org (unlocks restricted collections) |
| `--logout`        | Remove the saved credentials file and clear the session |
| `--config <path>` | Use a custom credentials file (default `~/.ia`) |
| `--email <addr>`  | Supply the login email non-interactively |
| `--password <pass>` | Supply the login password non-interactively |
| `--type <glob>`   | Only download files matching a type/glob pattern |

### Terminal colours

When stdout is an interactive terminal, log levels and download status tags are
colourised by default:

| Element | Colour |
|---------|--------|
| `[ERROR]` / `[ERR]` | red |
| `[WARN]`  / `[SKIP]` | yellow |
| `[INFO]`  / `[DONE]` / `[OK]` | green |
| `[DEBUG]` / `[GET]` | cyan |
| `[TRACE]` | magenta |

Colours are automatically disabled when output is redirected/not a TTY, when the
`NO_COLOR` environment variable is set (https://no-color.org/), or with
`--no-color`. Use `--color` to force colour output even when piped. On Windows,
ANSI/VT processing is enabled automatically for Windows 10+ consoles.

### Authentication & restricted collections

archive.org marks many collections (e.g. `total-dos-collection-b`) as
**access-restricted**; downloads return HTTP 401 unless you are logged in with an
account that has been approved for access. To unlock these:

```bash
# Interactive prompt (hides the password as you type):
archive_downloader --login <source_url> <destination_folder>

# Login only, then download later:
archive_downloader --login

# Non-interactive, via environment (avoids exposing the password in shell history):
export IA_EMAIL="you@example.com"
export IA_PASSWORD="your-password"
archive_downloader --login <source_url> <destination_folder>

# Forgetting stored credentials:
archive_downloader --logout
```

The session cookies (`logged-in-user`, `logged-in-sig`) are stored in `~/.ia` and
are automatically loaded on subsequent runs, so you only need to log in again after
they expire (typically a few days). Use `--config <path>` to keep credentials in a
custom location.

archive.org returns each login cookie as a full Set-Cookie string (with
`domain`, `path`, `expires`, ... attributes). The tool automatically strips these
attributes down to the bare cookie values before sending them, so the stored
credentials stay valid across sessions - a login persists until the session
expires server-side.

Credential priority for `--login`: `--email`/`--password` arguments, then
`IA_EMAIL`/`IA_PASSWORD` environment variables, then an interactive prompt.

> **Note:** Passing credentials via `--email`/`--password` exposes them in your
> shell history and process list. Prefer `IA_EMAIL`/`IA_PASSWORD` or the
> interactive prompt.

Once authenticated, restricted/private files are attempted (they may still return
HTTP 401 if the account is not approved). Without authentication such files are
skipped to avoid noise.

### Logging format

Every log line is prefixed with a timestamp and level tag:
```
[20:23:54] [INFO ] Archive.org Downloader starting
[20:23:54] [DEBUG] Metadata URL: https://archive.org/metadata/softwarelibrary_msdos_showcase/files
[20:23:54] [DEBUG] Parsed components: host='archive.org' port=443 scheme=2 path='/metadata/...'
[20:23:55] [INFO ] HTTP response: 200 OK
[20:23:55] [DEBUG] Read 1691 bytes total from response body.
```

### Examples

```bash
# Download the DOS game collection into ./total-dos-collection-b (auto-derived)
archive_downloader https://archive.org/download/total-dos-collection-b

# Download into an explicit folder
archive_downloader https://archive.org/download/total-dos-collection-b ./downloads

# Download restricted collection with an interactive login
archive_downloader --login https://archive.org/download/total-dos-collection-b ./downloads

# Downlod the MS-DOS software library with verbose logging
archive_downloader -v https://archive.org/download/softwarelibrary_msdos_games ./games

# Trace every request and HTTP header
archive_downloader -vv https://archive.org/download/softwarelibrary_msdos_games ./games

# Download only the .zip files in a collection
archive_downloader --type "*.zip" https://archive.org/download/softwarelibrary_msdos_games ./games

# Download only images (jpeg or png), matching anywhere in the filename
archive_downloader --type "*.{jpg,jpeg,png}" https://archive.org/download/somecollection ./out

# Download only files under an "iso/" subdirectory
archive_downloader --type "iso/*" https://archive.org/download/somecollection ./out
```

The `--type` value is a glob supporting `*`, `?`, `[...]` character classes with
ranges (e.g. `*.[0-9][0-9][0-9]`), and `{a,b,c}` alternation. Non-matching files
are skipped and counted in the `Filtered` summary line.

## How It Works

1. Extracts the archive identifier from the URL (e.g. `total-dos-collection-b`)
2. Fetches the file manifest from archive.org's metadata API
3. For each public file, downloads it to the destination folder
4. Files already fully downloaded are skipped
5. Partial files are detected and resumed via HTTP `Range` headers

## Dependencies

- C11 compiler (GCC/Clang/MSVC)
- CMake 3.10+
- libcurl (Linux/macOS only)
- [parson](https://github.com/kgabis/parson) JSON library (vendored in `src/`)
---
Continuous integration and release builds are provided via GitHub Actions (see .github/workflows/release.yml). Releases are produced on every push to master and on version tags.

### Release artifacts

Each release ships the same-named binary per platform, packaged into archives
whose *filename* carries the version and platform. The binary **inside** an
archive is always `archive_downloader` (Linux) or `archive_downloader.exe`
(Windows):

| Platform | Archive asset | Inner binary |
|----------|---------------|--------------|
| Linux    | `archive_downloader-<version>-linux-x86_64.tar.gz` | `archive_downloader` |
| Windows  | `archive_downloader-<version>-windows-x86_64.zip` | `archive_downloader.exe` |
| Both     | `checksums.txt` | SHA256 of every asset |

The `<version>` is a readable, auto-incrementing semantic version. Every
branch build bumps the patch number from the previous version tag (e.g.
`v1.0.0`, then `v1.0.1`, `v1.0.2`, ...), starting at `v1.0.0`. Pushing a
`v1.2.3` tag or any `v*` tag publishes a release at exactly that version. The
version string is compiled into every binary and reported by
`archive_downloader --version`, so you can always identify exactly which build
you are running.

Builds default `VERSION` to `dev` for local compiles. To embed a version
locally: `cmake -DVERSION="v1.0.0"`, `make VERSION="v1.0.0"`, or
`mingw32-make -f Makefile.win VERSION="v1.0.0"`.
