#include "uart_io.h"

#include "uart_error.h"
#include "uart_time.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

/*
 * UART_RX_READ_MAX  — maximum bytes consumed from the device in one read().
 * UART_RX_PREFIX_MAX — maximum chars in the "[RX NNN bytes] " prefix.
 *                      With UART_RX_READ_MAX == 256, the prefix is at most
 *                      "[RX 256 bytes] " == 15 chars; 32 provides headroom.
 * UART_RX_LINE_MAX  — total stack-allocated output buffer.
 *
 * stdout is assumed to be set to _IONBF by the caller before any I/O begins.
 * No internal fflush is performed.
 */
#define UART_RX_READ_MAX    256
#define UART_RX_PREFIX_MAX   32
#define UART_RX_LINE_MAX    (UART_RX_PREFIX_MAX + UART_RX_READ_MAX + 2)

_Static_assert(UART_RX_READ_MAX > 0,
               "UART_RX_READ_MAX must be positive.");
_Static_assert(UART_RX_PREFIX_MAX > 16,
               "UART_RX_PREFIX_MAX must be large enough for '[RX NNN bytes] '.");

static int timespec_now(struct timespec *ts)
{
    if (clock_gettime(CLOCK_MONOTONIC, ts) == -1) {
        int saved_errno = errno;
        uart_log_errno("clock_gettime", saved_errno);
        return -1;
    }

    return 0;
}

static int timespec_remaining_to_timeval(const struct timespec *deadline,
                                         struct timeval *tv)
{
    struct timespec now;
    time_t sec;
    long nsec;

    if (deadline == NULL || tv == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (timespec_now(&now) != 0) {
        return -1;
    }

    if ((now.tv_sec > deadline->tv_sec) ||
        (now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec)) {
        tv->tv_sec = 0;
        tv->tv_usec = 0;
        return 0;
    }

    sec  = deadline->tv_sec  - now.tv_sec;
    nsec = deadline->tv_nsec - now.tv_nsec;

    if (nsec < 0) {
        --sec;
        nsec += 1000000000L;
    }

    tv->tv_sec  = sec;
    tv->tv_usec = nsec / 1000L;
    return 0;
}

/*
 * Returns:
 *   1  -> fd is ready for the requested I/O direction
 *   0  -> deadline already elapsed, or select() timed out
 *  -1  -> error or interrupted
 *
 * Both "deadline already passed on entry" and "select() timed out" return 0.
 * Callers that need to distinguish the two cases must check the deadline
 * themselves before calling.
 */
static int wait_for_fd_ready(int fd,
                             bool want_read,
                             const struct timespec *deadline,
                             volatile sig_atomic_t *interrupted)
{
    for (;;) {
        fd_set fds;
        struct timeval tv;
        int ret;
        int saved_errno;

        if (interrupted != NULL && *interrupted) {
            errno = EINTR;
            return -1;
        }

        if (timespec_remaining_to_timeval(deadline, &tv) != 0) {
            return -1;
        }

        if (tv.tv_sec == 0 && tv.tv_usec == 0) {
            return 0;
        }

        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        if (want_read) {
            ret = select(fd + 1, &fds, NULL, NULL, &tv);
        } else {
            ret = select(fd + 1, NULL, &fds, NULL, &tv);
        }

        if (ret < 0) {
            saved_errno = errno;
            if (saved_errno == EINTR) {
                if (interrupted != NULL && *interrupted) {
                    errno = EINTR;
                    return -1;
                }
                continue;
            }

            uart_log_errno("select", saved_errno);
            return -1;
        }

        return ret;
    }
}

static void uart_print_rx_chunk(const unsigned char *buffer, size_t length)
{
    /*
     * Bytes are printed exactly as received. Terminal control sequences in
     * the incoming stream will execute if stdout is a terminal. Redirect
     * output to a file or add a hex-dump mode for untrusted devices.
     *
     * The buffer is stack-allocated to avoid heap churn in the read loop.
     * stdout must be set to _IONBF by main() before this function is called;
     * no explicit fflush is performed here.
     *
     * The buffer is not null-terminated; fwrite uses the explicit byte count.
     */
    char line[UART_RX_LINE_MAX];
    int prefix_len;
    size_t total_len;

    prefix_len = snprintf(line, sizeof(line), "[RX %zu bytes] ", length);
    if (prefix_len < 0) {
        return;
    }

    if ((size_t)prefix_len >= sizeof(line)) {
        return;
    }

    total_len = (size_t)prefix_len;

    if (total_len + length + 1 > sizeof(line)) {
        uart_log_warn("RX chunk truncated for display (%zu bytes).", length);
        if (total_len + 1 >= sizeof(line)) {
            return;
        }
        length = sizeof(line) - total_len - 1;
    }

    memcpy(line + total_len, buffer, length);
    total_len += length;

    if (length == 0 || buffer[length - 1] != '\n') {
        line[total_len++] = '\n';
    }

    (void)fwrite(line, 1, total_len, stdout);
}

static int uart_write_all(int fd,
                          const unsigned char *buffer,
                          size_t length,
                          int timeout_seconds,
                          volatile sig_atomic_t *interrupted)
{
    size_t total_written = 0;
    struct timespec start;
    struct timespec deadline;

    if (fd < 0 || (length > 0 && buffer == NULL) || timeout_seconds < 0) {
        errno = EINVAL;
        return -1;
    }

    if (timespec_now(&start) != 0) {
        return -1;
    }

    if (uart_timespec_add_seconds(&deadline, &start, timeout_seconds) != 0) {
        return -1;
    }

    while (total_written < length) {
        ssize_t n = write(fd, buffer + total_written, length - total_written);

        if (n > 0) {
            total_written += (size_t)n;
            continue;
        }

        if (n < 0 && errno == EINTR) {
            if (interrupted != NULL && *interrupted) {
                errno = EINTR;
                return -1;
            }
            continue;
        }

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            int ready = wait_for_fd_ready(fd, false, &deadline, interrupted);
            if (ready <= 0) {
                if (ready == 0) {
                    uart_log_warn("Timed out waiting for the device to become writable.");
                    errno = ETIMEDOUT;
                }
                return -1;
            }
            continue;
        }

        if (n < 0) {
            int saved_errno = errno;
            if (saved_errno == EPIPE) {
                uart_log_error("Write failed: remote end closed or device disconnected.");
                return -1;
            }
            uart_log_errno("write", saved_errno);
            return -1;
        }
    }

    return 0;
}

int uart_send_message(int fd,
                      const void *data,
                      size_t length,
                      int timeout_seconds,
                      volatile sig_atomic_t *interrupted)
{
    if (fd < 0 || data == NULL || timeout_seconds < 0) {
        errno = EINVAL;
        uart_log_error("uart_send_message called with invalid arguments.");
        return -1;
    }

    if (length == 0) {
        uart_log_warn("Transmit payload is empty; nothing was sent.");
        return 0;
    }

    if (uart_write_all(fd, (const unsigned char *)data, length, timeout_seconds, interrupted) != 0) {
        return -1;
    }

    uart_log_info("Transmitted %zu bytes.", length);
    return 0;
}

int uart_receive_with_timeout(int fd,
                              int timeout_seconds,
                              volatile sig_atomic_t *interrupted)
{
    struct timespec start;
    struct timespec deadline;
    int received_any_data = 0;

    if (fd < 0 || timeout_seconds < 0) {
        errno = EINVAL;
        uart_log_error("uart_receive_with_timeout called with invalid arguments.");
        return -1;
    }

    if (timespec_now(&start) != 0) {
        return -1;
    }

    if (uart_timespec_add_seconds(&deadline, &start, timeout_seconds) != 0) {
        return -1;
    }

    /*
     * TEST_SENTINEL: the exact string "Waiting up to" below is matched by
     * tests/loopback_test.sh to detect when this function has been entered.
     * Do not change the wording without updating the test script.
     */
    uart_log_info("Waiting up to %d second(s) for incoming data...", timeout_seconds);

    while (1) {
        int ready;

        if (interrupted != NULL && *interrupted) {
            errno = EINTR;
            uart_log_warn("Interrupted by signal; stopping receive loop.");
            return -1;
        }

        ready = wait_for_fd_ready(fd, true, &deadline, interrupted);
        if (ready < 0) {
            return -1;
        }

        if (ready == 0) {
            break;
        }

        /*
         * Drain all bytes currently available. There is no end-of-message
         * detection; the function exits only on timeout, signal, or peer close.
         */
        for (;;) {
            unsigned char buffer[UART_RX_READ_MAX];
            ssize_t n;

            if (interrupted != NULL && *interrupted) {
                errno = EINTR;
                uart_log_warn("Interrupted by signal while reading.");
                return -1;
            }

            n = read(fd, buffer, sizeof(buffer));

            if (n > 0) {
                uart_print_rx_chunk(buffer, (size_t)n);
                received_any_data = 1;
                continue;
            }

            if (n == 0) {
                /*
                 * On PTYs, n == 0 means the peer closed the slave side.
                 * On real hardware with VMIN=0, VTIME=0, and O_NONBLOCK,
                 * n == 0 should not occur because select() gates the read;
                 * treat it defensively as graceful end-of-stream.
                 */
                uart_log_info("Remote side closed the serial link.");
                return 0;
            }

            if (errno == EINTR) {
                if (interrupted != NULL && *interrupted) {
                    errno = EINTR;
                    uart_log_warn("Interrupted by signal while reading.");
                    return -1;
                }
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            {
                int saved_errno = errno;
                uart_log_errno("read", saved_errno);
                return -1;
            }
        }
    }

    if (!received_any_data) {
        uart_log_info("No incoming data received before timeout.");
    }

    return 0;
}