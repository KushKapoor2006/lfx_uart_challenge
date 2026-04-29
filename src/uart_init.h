#ifndef UART_INIT_H
#define UART_INIT_H

#include <termios.h>
#include <stdbool.h>

typedef struct {
    int fd;
    struct termios original_termios;
    bool original_termios_valid;
} uart_device_t;

void uart_device_init(uart_device_t *device);
int uart_open_device(uart_device_t *device, const char *path);
int uart_restore_and_close(uart_device_t *device);

#endif