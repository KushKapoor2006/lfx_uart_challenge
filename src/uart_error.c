#include "uart_error.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define UART_LOG_BODY_MAX 1024
#define UART_LOG_LINE_MAX 2048

static void uart_emit_line(FILE *stream, const char *line, size_t len)
{
    if (!stream || !line || len == 0) return;
    fwrite(line, 1, len, stream);
}

static void uart_emit_formatted_line(FILE *stream, const char *prefix, const char *fmt, va_list ap)
{
    char body[UART_LOG_BODY_MAX];
    char line[UART_LOG_LINE_MAX];

    int body_len = vsnprintf(body, sizeof(body), fmt, ap);
    if (body_len < 0) return;

    int line_len = snprintf(line, sizeof(line), "%s%s\n", prefix, body);
    if (line_len < 0) return;

    if ((size_t)line_len >= sizeof(line)) {
        line[sizeof(line)-2] = '\n';
        line[sizeof(line)-1] = '\0';
        line_len = sizeof(line)-1;
    }

    uart_emit_line(stream, line, (size_t)line_len);
}

void uart_log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    uart_emit_formatted_line(stdout, "[INFO] ", fmt, ap);
    va_end(ap);
}

void uart_log_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    uart_emit_formatted_line(stderr, "[WARN] ", fmt, ap);
    va_end(ap);
}

void uart_log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    uart_emit_formatted_line(stderr, "[ERROR] ", fmt, ap);
    va_end(ap);
}

#ifdef UART_DEBUG
static struct timespec g_start;
static int g_init = 0;

static const char *elapsed(char *buf, size_t sz)
{
    struct timespec now;
    if (!g_init) {
        clock_gettime(CLOCK_MONOTONIC, &g_start);
        g_init = 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &now);

    long long ms = (now.tv_sec - g_start.tv_sec) * 1000LL +
                   (now.tv_nsec - g_start.tv_nsec) / 1000000LL;

    snprintf(buf, sz, "%lld", ms < 0 ? 0 : ms);
    return buf;
}

void uart_log_debug_impl(const char *file, int line, const char *fmt, ...)
{
    char prefix[256];
    char t[32];
    snprintf(prefix, sizeof(prefix), "[DEBUG %sms %s:%d] ", elapsed(t,sizeof(t)), file, line);

    va_list ap;
    va_start(ap, fmt);
    uart_emit_formatted_line(stderr, prefix, fmt, ap);
    va_end(ap);
}

void uart_log_errno_at(const char *ctx, int err, const char *file, int line)
{
    char prefix[256], t[32], linebuf[UART_LOG_LINE_MAX];

    snprintf(prefix, sizeof(prefix), "[ERROR %sms %s:%d] ",
             elapsed(t,sizeof(t)), file, line);

    int n = snprintf(linebuf, sizeof(linebuf), "%s%s: %s\n",
                     prefix, ctx, strerror(err));

    uart_emit_line(stderr, linebuf, (size_t)n);
}
#else
void uart_log_errno(const char *ctx, int err)
{
    fprintf(stderr, "[ERROR] %s: %s\n", ctx, strerror(err));
}
#endif