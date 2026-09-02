#!/usr/bin/env bash
#
# End-to-end integration test against a real archive.org collection.
#
# Verifies that the built binary correctly downloads a file whose name contains
# characters that must be URL-encoded (spaces, parentheses, brackets). This is
# the exact regression that caused "curl_easy_perform failed: URL using
# bad/illegal format or missing URL (rc=3)" on libcurl.
#
# Usage: integration_test.sh <binary> <dest-dir> [identifier] [glob]
#
# Retries transient network failures, but fails hard if the expected file never
# arrives (i.e. the URL-encoding is wrong).
set -u

BIN="${1:?usage: integration_test.sh <binary> <dest-dir>}"
DEST="${2:?usage: integration_test.sh <binary> <dest-dir>}"
ID="${3:-TOSEC_2020_Roundup}"
GLOB="${4:-*ACT Apricot PC-Xi - Demos*}"

# Expected archive.org size (bytes) of the file under GLOB, when known.
# We tolerate small drift; the crucial assertion is that the file actually
# downloads (non-zero) with URL-encoded special characters in its name.
EXPECTED_SIZE=127409
SIZE_TOLERANCE=256

attempts=3
ok=0
mkdir -p "${DEST}"
for attempt in $(seq 1 "$attempts"); do
    echo "== integration: attempt $attempt/$attempts =="
    "${BIN}" --type "${GLOB}" --no-color \
        "https://archive.org/download/${ID}" "${DEST}" \
        > /tmp/intest.log 2>&1
    rc=$?
    echo "binary exit code: $rc"
    cat /tmp/intest.log
    if [ "$rc" -eq 0 ]; then
        ok=1
        break
    fi
    echo "== transient failure (attempt $attempt), retrying =="
    sleep 3
done

if [ "$ok" -ne 1 ]; then
    echo "INTEGRATION TEST FAILED: binary never succeeded"
    exit 1
fi

# Locate the downloaded file and assert it exists with the expected size.
found=0
found_size=0
found_name=""
while IFS= read -r -d '' f; do
    b=$(basename "$f")
    case "$b" in $GLOB) found=1; found_size=$(stat -c %s "$f" 2>/dev/null || echo 0); found_name="$b";; esac
done < <(find "${DEST}" -type f -print0)

if [ "$found" -ne 1 ]; then
    echo "INTEGRATION TEST FAILED: expected file matching '$GLOB' was not downloaded"
    exit 1
fi

if [ "$found_size" -le 0 ]; then
    echo "INTEGRATION TEST FAILED: '$found_name' downloaded but is 0 bytes"
    exit 1
fi

if [ -n "$EXPECTED_SIZE" ]; then
    diff=$(( found_size - EXPECTED_SIZE ))
    if [ "$diff" -lt 0 ]; then diff=$(( -diff )); fi
    if [ "$diff" -gt "$SIZE_TOLERANCE" ]; then
        echo "INTEGRATION TEST FAILED: '$found_name' size $found_size != expected $EXPECTED_SIZE (off by $diff)"
        exit 1
    fi
fi

echo "INTEGRATION TEST PASSED: '$found_name' downloaded ($found_size bytes)"
exit 0