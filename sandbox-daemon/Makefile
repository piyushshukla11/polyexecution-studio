CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -O2
SRCS = sandbox.c
OBJS = $(SRCS:.c=.o)
TARGET = sandbox-runner

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c sandbox.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
