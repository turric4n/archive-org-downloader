#!/usr/bin/env bash
#
# End-to-end integration test against a real archive.org collection.
#
# Runs the built binary against a live archive.org item and asserts a real file
# downloads with the expected content. Retries transient network errors /
# archive.org rate-limits with backoff; fails hard if the expected file never
# arrives with plausible content.
#
# The URL-encoding regression (libcurl "URL using bad/illegal format" rc=3 on
# file names containing spaces/parens/brackets) is deterministically guarded by
# tests/unit_test.c (no network). This script proves the binary's full download
# pipeline works against real archive.org.
#
# Usage: integration_test.sh <binary> <dest-dir> [identifier] [glob]
set -u

BIN="${1:?usage: integration_test.sh <binary> <dest-dir>}"
DEST="${2:?usage: integration_test.sh <binary> <dest-dir>}"
ID="${3:-goodytwoshoes00newyiala}"
GLOB="${4:-*goodytwoshoes00newyiala_djvu.txt}"

# Expected size (bytes) of the file under GLOB. Shared-object/metadata items
# can drift; we allow SIZE_TOLERANCE bytes of drift.
EXPECTED_SIZE=15118
SIZE_TOLERANCE=512

attempts=5
ok=0
mkdir -p "${DEST}"
for attempt in $(seq 1 "$attempts"); do
    echo "== integration: attempt $attempt/$attempts =="
    "${BIN}" --type "${GLOB}" --no-color \
        "https://archive.org/download/${ID}" "${DEST}" \
        > /tmp/archive-downloader-intest.log 2>&1
    rc=$?
    echo "binary exit code: $rc"
    cat /tmp/archive-downloader-intest.log
    if [ "$rc" -eq 0 ]; then
        ok=1
        break
    fi
    echo "== transient failure (attempt $attempt), backing off =="
    sleep "$((attempt * 3))"
done

if [ "$ok" -ne 1 ]; then
    echo "INTEGRATION TEST: binary did not download after $attempts attempts"
    # Distinguish a tool regression from archive.org blocking/rate-limiting
    # GitHub-CI datacenter IPs (a well-known external condition that would make
    # this a flaky gate). Probe archive.org directly: if it is reachable and
    # serves the item, a failure here is our bug -> hard-fail; if archive.org
    # is unreachable/blocked, report as environmental and do not fail the gate.
    probe_url="https://archive.org/metadata/${ID}"
    probe_status=$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 "${probe_url}" 2>/dev/null)
    echo "archive.org probe status: ${probe_status:-unreachable}"
    if [ "${probe_status:-000}" = "200" ]; then
        echo "INTEGRATION TEST FAILED: archive.org reachable but download failed (tool regression)"
        exit 1
    fi
    echo "INTEGRATION TEST SKIPPED (environmental): archive.org unreachable/blocked from this runner (probe=${probe_status:-000})"
    exit 0
fi

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

diff=$(( found_size - EXPECTED_SIZE ))
if [ "$diff" -lt 0 ]; then diff=$(( -diff )); fi
if [ "$diff" -gt "$SIZE_TOLERANCE" ]; then
    echo "INTEGRATION TEST FAILED: '$found_name' size $found_size != expected $EXPECTED_SIZE (off by $diff)"
    exit 1
fi

echo "INTEGRATION TEST PASSED: '$found_name' downloaded ($found_size bytes)"
exit 0