CC      := gcc
TARGET  := uart_prog
PREFIX  ?= /usr/local

BUILD_MODE ?= release
BUILD_DIR  := build/$(BUILD_MODE)
OBJ_DIR    := $(BUILD_DIR)/obj

BASE_CPPFLAGS := -Iinclude -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
BASE_CFLAGS   := -Wall -Wextra -Wpedantic -std=c11 -O2
DEBUG_CFLAGS  := -Wall -Wextra -Wpedantic -std=c11 -O0 -g -fsanitize=address,undefined
DEBUG_LDFLAGS := -fsanitize=address,undefined

CPPFLAGS ?= $(BASE_CPPFLAGS)
CFLAGS   ?= $(BASE_CFLAGS)
LDFLAGS  ?=
LDLIBS   ?=
DEPFLAGS := -MMD -MP

SRC_DIR := src

SRCS := \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/uart_init.c \
	$(SRC_DIR)/uart_config.c \
	$(SRC_DIR)/uart_io.c \
	$(SRC_DIR)/uart_error.c \
	$(SRC_DIR)/uart_time.c

OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all debug test install uninstall install-strip clean

all: $(TARGET)

debug:
	$(MAKE) BUILD_MODE=debug \
		CPPFLAGS="$(BASE_CPPFLAGS) -DUART_DEBUG" \
		CFLAGS="$(DEBUG_CFLAGS)" \
		LDFLAGS="$(DEBUG_LDFLAGS)" \
		LDLIBS="$(LDLIBS)" \
		all

$(OBJ_DIR):
	@mkdir -p $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(DEPS)

test: all
	./tests/loopback_test.sh

install: all
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m 0755 $(TARGET) "$(DESTDIR)$(PREFIX)/bin/$(TARGET)"

install-strip: all
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -s -m 0755 $(TARGET) "$(DESTDIR)$(PREFIX)/bin/$(TARGET)"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/$(TARGET)"

clean:
	rm -rf build $(TARGET)