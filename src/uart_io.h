#ifndef UART_IO_H
#define UART_IO_H

#include <stddef.h>
#include <signal.h>

int uart_send_message(int fd,
                      const void *data,
                      size_t len,
                      int timeout,
                      volatile sig_atomic_t *interrupted);

int uart_receive_with_timeout(int fd,
                              int timeout,
                              volatile sig_atomic_t *interrupted);

#endif