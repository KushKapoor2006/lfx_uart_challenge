#include "uart_config.h"
#include "uart_error.h"
#include "uart_init.h"
#include "uart_io.h"

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DEFAULT_BAUD_RATE      115200U
#define DEFAULT_TIMEOUT_SEC    5
#define DEFAULT_MESSAGE        "Hello from the UART challenge!\n"
#define DEFAULT_DATA_BITS      8
#define DEFAULT_STOP_BITS      1

static volatile sig_atomic_t g_interrupted = 0;

static void on_signal(int signo)
{
    (void)signo;
    g_interrupted = 1;
}

static int install_signal_handlers(void)
{
    struct sigaction sa;
    struct sigaction ign;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        int saved_errno = errno;
        uart_log_errno("sigaction(SIGINT)", saved_errno);
        return -1;
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        int saved_errno = errno;
        uart_log_errno("sigaction(SIGTERM)", saved_errno);
        return -1;
    }

    if (sigaction(SIGHUP, &sa, NULL) == -1) {
        int saved_errno = errno;
        uart_log_errno("sigaction(SIGHUP)", saved_errno);
        return -1;
    }

    memset(&ign, 0, sizeof(ign));
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    ign.sa_flags = 0;

    if (sigaction(SIGPIPE, &ign, NULL) == -1) {
        int saved_errno = errno;
        uart_log_errno("sigaction(SIGPIPE)", saved_errno);
        return -1;
    }

    return 0;
}

static void print_usage(const char *prog_name)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s --device <path> [--baud <rate>] [--timeout <sec>]\n"
            "     [--message <text>] [--data-bits <5|6|7|8>] [--parity <none|even|odd>] [--stop-bits <1|2>]\n\n"
            "Examples:\n"
            "  %s --device /dev/ttyUSB0\n"
            "  %s --device /tmp/ttyV0 --baud 115200 --timeout 5 --message \"Hello UART\\\\n\"\n\n"
            "Notes:\n"
            "  - Backslash escapes in --message are supported: \\n, \\r, \\t, \\\\ and \\xHH.\n"
            "  - A positional device path is also accepted as the first non-option argument.\n",
            prog_name, prog_name, prog_name);
}

static int parse_unsigned_int(const char *text,
                              unsigned int min_value,
                              unsigned int max_value,
                              unsigned int *out_value)
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL || out_value == NULL) {
        return -1;
    }

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }

    if (value < min_value || value > max_value) {
        return -1;
    }

    *out_value = (unsigned int)value;
    return 0;
}

static int parse_int(const char *text,
                     int min_value,
                     int max_value,
                     int *out_value)
{
    char *end = NULL;
    long value;

    if (text == NULL || out_value == NULL) {
        return -1;
    }

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }

    if (value < min_value || value > max_value) {
        return -1;
    }

    *out_value = (int)value;
    return 0;
}

static int hex_value(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static int parse_parity_arg(const char *text, uart_parity_t *out_parity)
{
    if (text == NULL || out_parity == NULL) {
        return -1;
    }

    if (strcasecmp(text, "none") == 0) { *out_parity = UART_PARITY_NONE; return 0; }
    if (strcasecmp(text, "even") == 0) { *out_parity = UART_PARITY_EVEN; return 0; }
    if (strcasecmp(text, "odd")  == 0) { *out_parity = UART_PARITY_ODD;  return 0; }

    return -1;
}

static int decode_message_escapes(const char *input,
                                  unsigned char **out_buf,
                                  size_t *out_len)
{
    size_t in_len;
    unsigned char *buf;
    size_t out_i = 0;
    size_t i;

    if (input == NULL || out_buf == NULL || out_len == NULL) {
        errno = EINVAL;
        return -1;
    }

    in_len = strlen(input);
    buf = (unsigned char *)malloc(in_len + 1);
    if (buf == NULL) {
        /* errno is ENOMEM; caller is responsible for logging. */
        return -1;
    }

    for (i = 0; i < in_len; ++i) {
        unsigned char ch = (unsigned char)input[i];

        if (ch != '\\') {
            buf[out_i++] = ch;
            continue;
        }

        if (i + 1 >= in_len) {
            buf[out_i++] = '\\';
            break;
        }

        ch = (unsigned char)input[++i];
        switch (ch) {
        case 'n':  buf[out_i++] = '\n'; break;
        case 'r':  buf[out_i++] = '\r'; break;
        case 't':  buf[out_i++] = '\t'; break;
        case '\\': buf[out_i++] = '\\'; break;
        case '0':  buf[out_i++] = '\0'; break;
        case 'x': {
            int value  = 0;
            int digits = 0;

            while (i + 1 < in_len && digits < 2) {
                int hv = hex_value((unsigned char)input[i + 1]);
                if (hv < 0) {
                    break;
                }
                ++i;
                value = (value << 4) | hv;
                ++digits;
            }

            if (digits == 0) {
                /*
                 * Report offset of the backslash (i-1 because i was incremented
                 * to point at 'x' after the backslash was consumed).
                 */
                uart_log_error("Invalid \\x escape sequence at offset %zu in --message.",
                               i > 0 ? i - 1 : 0);
                free(buf);
                errno = EINVAL;
                return -1;
            }

            buf[out_i++] = (unsigned char)value;
            break;
        }
        default:
            buf[out_i++] = ch;
            break;
        }
    }

    *out_buf = buf;
    *out_len = out_i;
    return 0;
}

int main(int argc, char **argv)
{
    const char *device_path  = NULL;
    const char *message_arg  = NULL;
    unsigned char *message_storage = NULL;
    const unsigned char *message_buf = NULL;
    size_t message_len = 0;
    bool message_provided = false;

    uart_device_t device;
    speed_t      baud_rate    = (speed_t)0;
    uart_parity_t parity      = UART_PARITY_NONE;
    unsigned int  baud_display = 0;
    bool          baud_rate_set = false;
    int timeout_seconds = DEFAULT_TIMEOUT_SEC;
    int data_bits       = DEFAULT_DATA_BITS;
    int stop_bits       = DEFAULT_STOP_BITS;
    int rc;

    enum {
        OPT_DEVICE = 1000,
        OPT_BAUD,
        OPT_TIMEOUT,
        OPT_MESSAGE,
        OPT_DATA_BITS,
        OPT_PARITY,
        OPT_STOP_BITS
    };

    static const struct option long_options[] = {
        { "device",    required_argument, NULL, OPT_DEVICE    },
        { "baud",      required_argument, NULL, OPT_BAUD      },
        { "timeout",   required_argument, NULL, OPT_TIMEOUT   },
        { "message",   required_argument, NULL, OPT_MESSAGE   },
        { "data-bits", required_argument, NULL, OPT_DATA_BITS },
        { "parity",    required_argument, NULL, OPT_PARITY    },
        { "stop-bits", required_argument, NULL, OPT_STOP_BITS },
        { "help",      no_argument,       NULL, 'h'           },
        { 0,           0,                 0,    0             }
    };

    uart_device_init(&device);

    /*
     * Set both streams unbuffered before any logging so that uart_emit_line
     * (which does not call fflush) produces output immediately.
     */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (install_signal_handlers() != 0) {
        return EXIT_FAILURE;
    }

    while (1) {
        int opt = getopt_long(argc, argv, "d:b:t:m:c:p:s:h", long_options, NULL);

        if (opt == -1) {
            break;
        }

        switch (opt) {
        case OPT_DEVICE:
        case 'd':
            device_path = optarg;
            break;

        case OPT_BAUD:
        case 'b': {
            unsigned int baud_rate_value = 0;
            if (parse_unsigned_int(optarg, 300U, 4000000U, &baud_rate_value) != 0 ||
                uart_baud_rate_to_speed(baud_rate_value, &baud_rate) != 0) {
                uart_log_error("Unsupported baud rate: %s", optarg);
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            baud_display  = baud_rate_value;
            baud_rate_set = true;
            break;
        }

        case OPT_TIMEOUT:
        case 't':
            if (parse_int(optarg, 1, 3600, &timeout_seconds) != 0) {
                uart_log_error("Invalid timeout value: %s", optarg);
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            break;

        case OPT_MESSAGE:
        case 'm':
            message_arg      = optarg;
            message_provided = true;
            break;

        case OPT_DATA_BITS:
        case 'c':
            if (parse_int(optarg, 5, 8, &data_bits) != 0) {
                uart_log_error("Invalid data bits value: %s", optarg);
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            break;

        case OPT_PARITY:
        case 'p':
            if (parse_parity_arg(optarg, &parity) != 0) {
                uart_log_error("Invalid parity value: %s", optarg);
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            break;

        case OPT_STOP_BITS:
        case 's':
            if (parse_int(optarg, 1, 2, &stop_bits) != 0) {
                uart_log_error("Invalid stop bits value: %s", optarg);
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            break;

        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;

        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (device_path == NULL && optind < argc) {
        device_path = argv[optind++];
    }

    if (optind < argc) {
        uart_log_error("Unexpected extra argument: %s", argv[optind]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (device_path == NULL) {
        uart_log_error("No device path supplied.");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (!baud_rate_set) {
        if (uart_baud_rate_to_speed(DEFAULT_BAUD_RATE, &baud_rate) != 0) {
            uart_log_error("Default baud rate is not supported on this platform.");
            return EXIT_FAILURE;
        }
        baud_display = DEFAULT_BAUD_RATE;
    }

    if (message_provided) {
        /*
         * message_arg is borrowed from argv; decode_message_escapes allocates
         * message_storage. message_buf borrows that allocation. Only
         * message_storage is freed — never message_buf directly.
         *
         * message_arg == NULL here would be an internal logic error (getopt
         * always sets optarg for required_argument options).
         */
        if (message_arg == NULL) {
            uart_log_error("Internal error: --message flag set but optarg is NULL.");
            return EXIT_FAILURE;
        }

        if (decode_message_escapes(message_arg, &message_storage, &message_len) != 0) {
            int saved_errno = errno;
            if (saved_errno == ENOMEM) {
                /* ENOMEM is the only case decode_message_escapes does not log. */
                uart_log_errno("decode_message_escapes (malloc)", saved_errno);
            }
            /* All other errors (EINVAL from bad escapes) are already logged. */
            return EXIT_FAILURE;
        }
    } else {
        message_len    = strlen(DEFAULT_MESSAGE);
        message_storage = (unsigned char *)malloc(message_len);
        if (message_storage == NULL) {
            uart_log_errno("malloc (default message)", errno);
            return EXIT_FAILURE;
        }
        memcpy(message_storage, DEFAULT_MESSAGE, message_len);
    }

    message_buf = message_storage;

    uart_log_info("Opening device: %s", device_path);
    uart_log_info("Requested configuration: baud=%u data_bits=%d parity=%s stop_bits=%d timeout=%d",
                  baud_display,
                  data_bits,
                  uart_parity_to_string(parity),
                  stop_bits,
                  timeout_seconds);

    rc = EXIT_FAILURE;

    if (uart_open_device(&device, device_path) != 0) {
        rc = EXIT_FAILURE;
        goto cleanup;
    }

    if (uart_configure(device.fd, baud_rate, data_bits, parity, stop_bits) != 0) {
        rc = EXIT_FAILURE;
        goto cleanup;
    }

    if (uart_send_message(device.fd, message_buf, message_len,
                          timeout_seconds, &g_interrupted) != 0) {
        rc = (g_interrupted != 0) ? 130 : EXIT_FAILURE;
        goto cleanup;
    }

    if (uart_receive_with_timeout(device.fd, timeout_seconds, &g_interrupted) != 0) {
        rc = (g_interrupted != 0) ? 130 : EXIT_FAILURE;
        goto cleanup;
    }

    rc = EXIT_SUCCESS;

cleanup:
    /*
     * uart_restore_and_close checks device->fd < 0 internally, so it is
     * safe to call even when uart_open_device failed and left fd == -1.
     */
    if (uart_restore_and_close(&device) != 0 && rc == EXIT_SUCCESS) {
        rc = EXIT_FAILURE;
    }

    free(message_storage);
    message_storage = NULL;

    if (g_interrupted != 0 && rc == EXIT_FAILURE) {
        rc = 130;
    }

    if (rc == EXIT_SUCCESS) {
        uart_log_info("Done.");
    }

    return rc;
}