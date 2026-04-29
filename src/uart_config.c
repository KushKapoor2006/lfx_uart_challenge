#include "uart_config.h"
#include "uart_error.h"

#include <errno.h>
#include <unistd.h>

int uart_baud_rate_to_speed(unsigned int baud, speed_t *out)
{
    switch (baud) {
        case 9600: *out = B9600; break;
        case 115200: *out = B115200; break;
        default:
            errno = EINVAL;
            return -1;
    }
    return 0;
}

const char *uart_parity_to_string(uart_parity_t p)
{
    return p == UART_PARITY_NONE ? "none" :
           p == UART_PARITY_EVEN ? "even" : "odd";
}

int uart_configure(int fd, speed_t baud, int data_bits,
                   uart_parity_t parity, int stop_bits)
{
    struct termios t;

    if (tcgetattr(fd, &t) == -1) return -1;

    t.c_cflag |= CLOCAL | CREAD;
    t.c_cflag &= ~CSIZE;

    t.c_cflag |= (data_bits == 7) ? CS7 : CS8;

    if (parity == UART_PARITY_NONE)
        t.c_cflag &= ~PARENB;
    else {
        t.c_cflag |= PARENB;
        if (parity == UART_PARITY_ODD)
            t.c_cflag |= PARODD;
    }

    if (stop_bits == 2)
        t.c_cflag |= CSTOPB;
    else
        t.c_cflag &= ~CSTOPB;

    cfsetispeed(&t, baud);
    cfsetospeed(&t, baud);

    return tcsetattr(fd, TCSANOW, &t);
}