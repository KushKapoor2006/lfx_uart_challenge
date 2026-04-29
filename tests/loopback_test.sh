#!/usr/bin/env bash
# Requires: bash 4+, socat, python3
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/uart_prog"

if ! command -v socat >/dev/null 2>&1; then
    echo "[ERROR] socat is required for this test." >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "[ERROR] python3 is required for TX capture in this test." >&2
    exit 1
fi

LINK_A="$(mktemp -u /tmp/ttyV0.XXXX)"
LINK_B="$(mktemp -u /tmp/ttyV1.XXXX)"
LOG_FILE="$(mktemp /tmp/uart_test.XXXXXX.log)"
TX_FILE="$(mktemp /tmp/uart_tx.XXXXXX.bin)"
NEG_LOG="$(mktemp /tmp/uart_neg.XXXXXX.log)"

SOCAT_PID=""
APP_PID=""
CAP_PID=""

cleanup() {
    if [[ -n "${APP_PID}" ]]; then
        kill -0 "${APP_PID}" >/dev/null 2>&1 && kill "${APP_PID}" >/dev/null 2>&1 || true
    fi

    if [[ -n "${CAP_PID}" ]]; then
        kill -0 "${CAP_PID}" >/dev/null 2>&1 && kill "${CAP_PID}" >/dev/null 2>&1 || true
    fi

    if [[ -n "${SOCAT_PID}" ]]; then
        kill -0 "${SOCAT_PID}" >/dev/null 2>&1 && kill "${SOCAT_PID}" >/dev/null 2>&1 || true
    fi

    rm -f "${LINK_A}" "${LINK_B}" "${LOG_FILE}" "${TX_FILE}" "${NEG_LOG}"
}
trap cleanup EXIT

wait_for_path() {
    local path="$1"
    local timeout_tenths="$2"
    local elapsed_tenths=0

    while [[ ! -e "${path}" ]]; do
        sleep 0.1
        elapsed_tenths=$((elapsed_tenths + 1))
        if (( elapsed_tenths >= timeout_tenths )); then
            echo "[ERROR] Timed out waiting for ${path}" >&2
            exit 1
        fi
    done
}

wait_for_log_line() {
    # TEST_SENTINEL: the pattern passed here must match the log output of
    # uart_receive_with_timeout() in src/uart_io.c. See the comment there.
    local pattern="$1"
    local timeout_tenths="$2"
    local elapsed_tenths=0

    while ! grep -qF "${pattern}" "${LOG_FILE}" 2>/dev/null; do
        if ! kill -0 "${APP_PID}" >/dev/null 2>&1; then
            echo "[ERROR] Application exited before reaching the receive loop." >&2
            cat "${LOG_FILE}" >&2 || true
            exit 1
        fi

        sleep 0.1
        elapsed_tenths=$((elapsed_tenths + 1))
        if (( elapsed_tenths >= timeout_tenths )); then
            echo "[ERROR] Timed out waiting for log pattern: ${pattern}" >&2
            cat "${LOG_FILE}" >&2 || true
            exit 1
        fi
    done
}

# ---------------------------------------------------------------------------
echo "[INFO] Creating virtual serial pair:"
echo "       ${LINK_A} <-> ${LINK_B}"

socat PTY,link="${LINK_A}",raw,echo=0 PTY,link="${LINK_B}",raw,echo=0 >/dev/null 2>&1 &
SOCAT_PID=$!

wait_for_path "${LINK_A}" 50
wait_for_path "${LINK_B}" 50

EXPECTED_TX=$'TX from program\n'
EXPECTED_TX_LEN=${#EXPECTED_TX}

# ---------------------------------------------------------------------------
# Start the UART program first so it opens LINK_A (PTY master via socat).
# We then wait for the readiness sentinel before starting the capture and
# sending peer data, eliminating the write-before-open race.
# ---------------------------------------------------------------------------
echo "[INFO] Starting UART program..."
"${BIN}" --device "${LINK_A}" \
         --baud 115200 \
         --timeout 5 \
         --message "TX from program\n" \
         --data-bits 8 \
         --parity none \
         --stop-bits 1 \
         >"${LOG_FILE}" 2>&1 &
APP_PID=$!

# TEST_SENTINEL: "Waiting up to" matches uart_io.c uart_receive_with_timeout.
# Do not change without updating the corresponding comment in uart_io.c.
wait_for_log_line "Waiting up to" 50

echo "[INFO] Capturing transmitted data from peer side..."
python3 - "${LINK_B}" "${TX_FILE}" "${EXPECTED_TX_LEN}" <<'PY' &
import errno
import os
import select
import sys
import time

path   = sys.argv[1]
out    = sys.argv[2]
target = int(sys.argv[3])

deadline = time.monotonic() + 10.0
fd  = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
buf = bytearray()

try:
    while len(buf) < target:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        ready, _, _ = select.select([fd], [], [], remaining)
        if not ready:
            continue
        try:
            chunk = os.read(fd, target - len(buf))
        except OSError as exc:
            if exc.errno in (errno.EAGAIN, errno.EWOULDBLOCK, errno.EIO):
                time.sleep(0.05)
                continue
            raise
        if not chunk:
            time.sleep(0.05)
            continue
        buf.extend(chunk)

    with open(out, "wb") as f:
        f.write(buf)
finally:
    os.close(fd)

sys.exit(0 if len(buf) >= target else 1)
PY
CAP_PID=$!

echo "[INFO] Sending test data from the peer side..."
printf 'Hello from the peer side\n' > "${LINK_B}"

set +e
wait "${APP_PID}"
APP_EXIT=$?
set -e

if [[ "${APP_EXIT}" -ne 0 ]]; then
    echo "[ERROR] Program exited with code ${APP_EXIT}" >&2
    cat "${LOG_FILE}" >&2 || true
    exit 1
fi

set +e
wait "${CAP_PID}"
CAP_EXIT=$?
set -e

if [[ "${CAP_EXIT}" -ne 0 ]]; then
    echo "[ERROR] TX capture process failed (exit ${CAP_EXIT})." >&2
    cat "${TX_FILE}" >&2 || true
    exit 1
fi

echo "[INFO] Program output:"
cat "${LOG_FILE}"

echo "[INFO] Captured transmitted data:"
cat "${TX_FILE}"

if ! grep -qF "Hello from the peer side" "${LOG_FILE}"; then
    echo "[ERROR] Expected received data was not found in program output." >&2
    exit 1
fi

if ! grep -qF "TX from program" "${TX_FILE}"; then
    echo "[ERROR] Expected transmitted data was not captured from the peer side." >&2
    exit 1
fi

if ! grep -qF "Transmitted" "${LOG_FILE}"; then
    echo "[ERROR] Expected transmit log entry was not found in output." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
echo "[INFO] Running negative test: invalid device path..."
set +e
"${BIN}" --device /no/such/serial/device --baud 115200 >"${NEG_LOG}" 2>&1
NEG_EXIT=$?
set -e

if [[ "${NEG_EXIT}" -eq 0 ]]; then
    echo "[ERROR] Invalid-device negative test unexpectedly succeeded." >&2
    exit 1
fi

if ! grep -qF "does not exist" "${NEG_LOG}"; then
    echo "[ERROR] Negative test did not emit the expected device-not-found message." >&2
    cat "${NEG_LOG}" >&2 || true
    exit 1
fi

# ---------------------------------------------------------------------------
: > "${NEG_LOG}"

echo "[INFO] Running negative test: non-TTY device path..."
set +e
"${BIN}" --device /dev/null --baud 115200 >"${NEG_LOG}" 2>&1
NEG_EXIT=$?
set -e

if [[ "${NEG_EXIT}" -eq 0 ]]; then
    echo "[ERROR] Non-TTY negative test unexpectedly succeeded." >&2
    exit 1
fi

if ! grep -qF "not a TTY" "${NEG_LOG}"; then
    echo "[ERROR] Non-TTY test did not emit the expected message." >&2
    cat "${NEG_LOG}" >&2 || true
    exit 1
fi

echo "[INFO] Loopback test completed successfully."