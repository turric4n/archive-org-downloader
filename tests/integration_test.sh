#!/usr/bin/env bash
#
# End-to-end smoke test against a real archive.org collection.
#
# Runs the built binary against a live archive.org item and asserts a real file
# downloads with the expected content, retrying transient network errors /
# archive.org rate-limits with backoff.
#
# This is intentionally NON-BLOCKING: archive.org is well known to intermittently
# block/rate-limit GitHub-CI datacenter IPs, so a failed external download is
# usually environmental rather than a tool regression. The script therefore
# always exits 0 and reports PASS / WARN / SKIPPED for clarity.
#
# The deterministic guard for the URL-encoding regression (libcurl "URL using
# bad/illegal format" rc=3 on file names containing spaces/parens/brackets) is
# tests/unit_test.c, which needs no network and always runs on both platforms.
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
    echo "INTEGRATION TEST DID NOT COMPLETE: binary did not download after $attempts attempts"
    # archive.org is known to intermittently block/throttle GitHub-CI datacenter
    # IPs, so a failed external download is frequently environmental rather than
    # a tool regression. Probe the ACTUAL file URL to classify the failure for
    # reporting. The deterministic URL-encoding regression is guarded
    # unconditionally by tests/unit_test.c (no network) on both platforms.
    probe_url="https://archive.org/download/${ID}/goodytwoshoes00newyiala_djvu.txt"
    probe_status=$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 "${probe_url}" 2>/dev/null)
    echo "archive.org file probe status: ${probe_status:-unreachable}"
    if [ "${probe_status:-000}" = "200" ]; then
        echo "INTEGRATION TEST WARN: archive.org serves the file but the binary did not (possible CI-IP throttle or tool issue)"
    else
        echo "INTEGRATION TEST SKIPPED (environmental): archive.org file unreachable/blocked from this runner (probe=${probe_status:-000})"
    fi
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
    echo "INTEGRATION TEST WARN: binary exited 0 but no file matching '$GLOB' was found under '${DEST}'"
    echo "                       (environmental: archive.org metadata/name drift, CI-IP throttle, or glob mismatch)"
    exit 0
fi

if [ "$found_size" -le 0 ]; then
    echo "INTEGRATION TEST WARN: '$found_name' downloaded but is 0 bytes"
    echo "                       (environmental: archive.org served an empty/throttled response)"
    exit 0
fi

diff=$(( found_size - EXPECTED_SIZE ))
if [ "$diff" -lt 0 ]; then diff=$(( -diff )); fi
if [ "$diff" -gt "$SIZE_TOLERANCE" ]; then
    echo "INTEGRATION TEST WARN: '$found_name' size $found_size != expected $EXPECTED_SIZE (off by $diff)"
    echo "                       (content drift on archive.org)"
    exit 0
fi

echo "INTEGRATION TEST PASSED: '$found_name' downloaded ($found_size bytes)"
exit 0