#ifndef UART_ERROR_H
#define UART_ERROR_H

#include <stdarg.h>

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

void uart_log_errno_at(const char *context, int saved_errno, const char *file, int line);
#define uart_log_errno(context, saved_errno) \
    uart_log_errno_at((context), (saved_errno), __FILE__, __LINE__)
#else
#define uart_log_debug(...) ((void)0)
void uart_log_errno(const char *context, int saved_errno);
#endif

#endif