#include "uart_init.h"
#include "uart_error.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <termios.h>

void uart_device_init(uart_device_t *d)
{
    if (!d) return;
    d->fd = -1;
    d->original_termios_valid = false;
    memset(&d->original_termios, 0, sizeof(d->original_termios));
}

int uart_open_device(uart_device_t *d, const char *path)
{
    if (!d || !path) {
        errno = EINVAL;
        return -1;
    }

    uart_device_init(d);

    d->fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (d->fd < 0) {
        int err = errno;

        if (err == ENOENT) {
            uart_log_error("Device path does not exist: %s", path);
        } else if (err == EACCES) {
            uart_log_error("Permission denied accessing device: %s", path);
        } else if (err == EBUSY) {
            uart_log_error("Device is busy or already in use: %s", path);
        } else {
            uart_log_errno("open", err);
        }

        return -1;
    }

    if (!isatty(d->fd)) {
        int err = ENOTTY;

        /* IMPORTANT: message must contain "not a TTY" and include the path */
        uart_log_error("Device is not a TTY: %s", path);

        close(d->fd);
        d->fd = -1;
        errno = err;
        return -1;
    }

    if (tcgetattr(d->fd, &d->original_termios) == -1) {
        int err = errno;
        uart_log_errno("tcgetattr", err);
        close(d->fd);
        d->fd = -1;
        return -1;
    }

    d->original_termios_valid = true;
    return 0;
}

int uart_restore_and_close(uart_device_t *d)
{
    if (!d || d->fd < 0) return 0;

    tcdrain(d->fd);
    tcflush(d->fd, TCIFLUSH);

    if (d->original_termios_valid) {
        tcsetattr(d->fd, TCSANOW, &d->original_termios);
    }

    close(d->fd);
    d->fd = -1;
    return 0;
}