#ifndef UART_ERROR_H
#define UART_ERROR_H

#include <stdarg.h>

/*
 * Logging is single-threaded and not atomic across multiple calls.
 * Each log call may interleave with another thread if the program is extended.
 */

#if defined(__GNUC__) || defined(__clang__)
#define UART_PRINTF_ATTR(fmt_index, first_arg) __attribute__((format(printf, fmt_index, first_arg)))
#else
#define UART_PRINTF_ATTR(fmt_index, first_arg)
#endif

void uart_log_info(const char *fmt, ...) UART_PRINTF_ATTR(1, 2);
void uart_log_warn(const char *fmt, ...) UART_PRINTF_ATTR(1, 2);
void uart_log_error(const char *fmt, ...) UART_PRINTF_ATTR(1, 2);

#ifdef UART_DEBUG
void uart_log_debug_impl(const char *file, int line, const char *fmt, ...) UART_PRINTF_ATTR(3, 4);
#define uart_log_debug(fmt, ...) uart_log_debug_impl(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define uart_log_debug(...) ((void)0)
#endif

void uart_log_errno(const char *context, int saved_errno);

#endif /* UART_ERROR_H */