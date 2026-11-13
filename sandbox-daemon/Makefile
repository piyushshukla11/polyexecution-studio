CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -O2
EVIL_CFLAGS ?= -Wall -Wextra -Werror -O0

SRCS = sandbox.c
OBJS = $(SRCS:.c=.o)
TARGET = sandbox-runner

EVIL_NAMES = loop sleeper exit7 segv exit127 stdout_stderr stderr_only output_flood close_stream_sleep descendant_pipe memory_bomb file_size_limit json_escaping_test
EVIL_BIN_DIR = tests/evil/bin
EVIL_BINS = $(addprefix $(EVIL_BIN_DIR)/, $(EVIL_NAMES))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c sandbox.h
	$(CC) $(CFLAGS) -c $< -o $@

evil: $(EVIL_BINS)

$(EVIL_BIN_DIR)/%: tests/evil/%.c | $(EVIL_BIN_DIR)
	$(CC) $(EVIL_CFLAGS) -o $@ $<

$(EVIL_BIN_DIR):
	mkdir -p $(EVIL_BIN_DIR)

clean:
	rm -f $(OBJS) $(TARGET)
	rm -rf $(EVIL_BIN_DIR)

.PHONY: all clean evil
