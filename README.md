# UART Challenge Submission

## Overview

This project is a Linux user-space C program that opens and configures a serial device using the `termios` API, sends a test message, waits for incoming data with timeout-based non-blocking I/O, prints received bytes to the console, and handles common error cases cleanly.

The code was written for a UART / serial-port coding challenge and is designed to work both with real hardware devices such as `/dev/ttyS0` or `/dev/ttyUSB0`, and with a laptop-only virtual setup using `socat`.

## What the program does

The program performs the following steps:

1. Opens the serial device path given by the user.
2. Configures UART parameters:

   * baud rate
   * data bits
   * parity
   * stop bits
3. Sends a test message through the UART interface.
4. Waits for incoming data using timeout-based I/O.
5. Prints any received bytes to the console.
6. Handles errors such as:

   * invalid device paths
   * permission denied
   * the path not being a TTY
   * read/write failures
   * timeout conditions
7. Restores the original terminal settings before exiting.

## Project structure

This is the structure used in my submission:

```text
project-root/
├── src/
│   ├── main.c
│   ├── uart_error.c
│   ├── uart_time.c
│   ├── uart_init.c
│   ├── uart_config.c
│   ├── uart_io.c
│   ├── uart_error.h
│   ├── uart_time.h
│   ├── uart_init.h
│   ├── uart_config.h
│   └── uart_io.h
├── tests/
│   └── loopback_test.sh
├── Makefile
├── full_run.log
├── rx_capture.log
├── tx_capture.log
└── build/
    └── release/
        └── obj/
```

Notes:

* All `.c` and `.h` files are inside `src/`.
* There is no separate `include/` folder.
* The `tests/` folder contains the loopback test script.
* The `build/release/obj/` directory contains the object files produced by the build system.
* The log files in the project root are proof artifacts from testing.

## Files in the root directory

### `Makefile`

Builds the program, creates the debug build, and runs the loopback test.

### `full_run.log`

Contains the full output of a successful test run.

### `rx_capture.log`

Contains the data received during the loopback test.

### `tx_capture.log`

Contains the data transmitted during the loopback test.

### `uart_prog`

The compiled executable produced by the build.

## Requirements

To build and test the project, you need:

* Linux
* `gcc` or another C compiler compatible with the build flags
* `make`
* `socat`
* `python3`

Install the required tools on Debian/Ubuntu-based systems with:

```bash
sudo apt update
sudo apt install build-essential socat python3
```

## Build instructions

From the project root directory:

```bash
make
```

This builds the release version and creates the executable:

```text
./uart_prog
```

For a debug build with sanitizers and debug logging enabled:

```bash
make debug
```

## Run instructions

### 1. Run on a real serial device

You can run the program with a real UART device path such as `/dev/ttyS0` or `/dev/ttyUSB0`:

```bash
./uart_prog --device /dev/ttyUSB0 --baud 115200
```

If the device needs a different configuration, you can also specify:

```bash
./uart_prog \
  --device /dev/ttyUSB0 \
  --baud 9600 \
  --timeout 5 \
  --message "Hello UART\\n" \
  --data-bits 8 \
  --parity none \
  --stop-bits 1
```

### 2. Run on a laptop without UART hardware

A virtual serial pair can be created using `socat`. This is the easiest way to verify the program without external hardware.

Create the virtual pair:

```bash
socat -d -d PTY,link=/tmp/ttyV0,raw,echo=0 PTY,link=/tmp/ttyV1,raw,echo=0
```

Then run the program on one side:

```bash
./uart_prog --device /tmp/ttyV0 --baud 115200
```

And send data from the other side in a second terminal:

```bash
echo "Hello from peer" > /tmp/ttyV1
```

The program should print the received bytes to the console.

## Testing

The repository includes an automated loopback test script:

```bash
./tests/loopback_test.sh
```

Or, if your Makefile is set up for it:

```bash
make test
```

The test script:

* creates a virtual serial pair using `socat`
* runs the UART program
* sends a test message through the peer side
* verifies that transmitted data and received data are both handled correctly
* checks basic negative cases such as an invalid device path and a non-TTY path

## What the logs show

The log files in the project root are proof of a successful run:

* `full_run.log` shows the full test execution output
* `tx_capture.log` shows the transmitted UART payload
* `rx_capture.log` shows the received UART payload

These files are useful as evidence that the code worked end-to-end during testing.

## Implementation summary

The code follows a modular design:

### `main.c`

Handles argument parsing, signal handling, orchestration, and cleanup.

### `uart_init.c`

Opens the device, validates that it is a TTY, stores the original terminal settings, and restores them on exit.

### `uart_config.c`

Configures UART parameters using `termios`.

### `uart_io.c`

Performs transmit and receive operations using non-blocking I/O and `select()`-based timeout handling.

### `uart_error.c`

Provides structured logging and human-readable error messages.

### `uart_time.c`

Provides shared time arithmetic used by the timeout logic.

## UART configuration supported

The program supports the common UART settings used in the challenge:

* baud rates such as `9600` and `115200`
* data bits: `5`, `6`, `7`, `8`
* parity: `none`, `even`, `odd`
* stop bits: `1`, `2`

## Error handling behavior

The program handles common failures gracefully:

* **Invalid path**: prints a clear message if the device does not exist
* **Permission issues**: prints a message when the user cannot open the device
* **Not a TTY**: rejects paths that are not serial/TTY devices
* **Read/write failures**: reports I/O errors clearly
* **Timeouts**: exits cleanly if no data arrives within the configured timeout
* **Signals**: handles interrupts and restores the device settings before exit

## Notes on the implementation

* The program is written in standard C11 style.
* The receive path uses `select()` so it does not busy-wait.
* Received bytes are printed directly to the console.
* The original terminal configuration is restored after the program finishes.
* Debug builds include extra logging and sanitizers.

## Example test output

A successful test run should end with a line like:

```text
[INFO] Loopback test completed successfully.
```

## Submission note

This project was prepared as a submission for a Linux Foundation mentorship challenge focused on Linux system programming, hardware interfacing, and robust error handling.
