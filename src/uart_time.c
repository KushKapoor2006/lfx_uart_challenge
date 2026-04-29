#include "uart_time.h"

#include <errno.h>
#include <limits.h>

_Static_assert(CHAR_BIT == 8, "Only 8-bit bytes are supported.");

/*
 * Returns the maximum representable value of time_t, derived portably
 * from sizeof(time_t) rather than from a fixed constant.
 *
 * Using a fixed constant such as LLONG_MAX would overflow a 32-bit time_t
 * (where time_t is int or long) and produce implementation-defined behaviour.
 */
static time_t uart_time_t_max_value(void)
{
    if ((time_t)-1 > 0) {
        /* Unsigned time_t: all bits set is the maximum. */
        return (time_t)-1;
    }

    /*
     * Signed time_t: the maximum is 2^(width-1) - 1.
     * Computed via unsigned long long to avoid signed overflow; the shift
     * amount is always < 64 because sizeof(time_t) <= 8 on all POSIX targets
     * and CHAR_BIT == 8 is asserted above.
     */
    return (time_t)(((unsigned long long)1u << (sizeof(time_t) * (size_t)CHAR_BIT - 1u)) - 1ull);
}

int uart_timespec_add_seconds(struct timespec *out,
                              const struct timespec *in,
                              int seconds)
{
    time_t tmax;

    if (out == NULL || in == NULL || seconds < 0) {
        errno = EINVAL;
        return -1;
    }

    tmax = uart_time_t_max_value();
    if (seconds > 0 && in->tv_sec > (time_t)(tmax - (time_t)seconds)) {
        errno = EOVERFLOW;
        return -1;
    }

    out->tv_sec = in->tv_sec + seconds;
    out->tv_nsec = in->tv_nsec;
    return 0;
}