#ifndef UART_CONFIG_H
#define UART_CONFIG_H

#include <termios.h>

typedef enum {
    UART_PARITY_NONE,
    UART_PARITY_EVEN,
    UART_PARITY_ODD
} uart_parity_t;

int uart_baud_rate_to_speed(unsigned int baud, speed_t *out);
const char *uart_parity_to_string(uart_parity_t p);

int uart_configure(int fd,
                   speed_t baud,
                   int data_bits,
                   uart_parity_t parity,
                   int stop_bits);

#endif