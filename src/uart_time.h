#ifndef UART_TIME_H
#define UART_TIME_H

#include <time.h>

int uart_timespec_add_seconds(struct timespec *out,
                              const struct timespec *in,
                              int seconds);

#endif