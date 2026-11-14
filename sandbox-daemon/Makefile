CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -O2
EVIL_CFLAGS ?= -Wall -Wextra -Werror -O0
LDLIBS ?= -lseccomp

SRCS = sandbox.c phase2_cgroup.c phase2_userns.c phase2_rootfs.c phase2_privilege.c
OBJS = $(SRCS:.c=.o)
TARGET = sandbox-runner

EVIL_NAMES = loop sleeper exit0 exit7 segv exit127 stdout_stderr stderr_only output_flood close_stream_sleep descendant_pipe memory_bomb file_size_limit json_escaping_test
EVIL_BIN_DIR = tests/evil/bin
EVIL_BINS = $(addprefix $(EVIL_BIN_DIR)/, $(EVIL_NAMES))

PHASE2_EVIL_NAMES = fork_bomb read_etc_passwd net_connect
PHASE2_EVIL_BINS = $(addprefix $(EVIL_BIN_DIR)/, $(PHASE2_EVIL_NAMES))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c sandbox.h
	$(CC) $(CFLAGS) -c $< -o $@

sandbox.o: phase2_cgroup.h phase2_userns.h phase2_rootfs.h phase2_privilege.h
phase2_cgroup.o: phase2_cgroup.h
phase2_userns.o: phase2_userns.h
phase2_rootfs.o: phase2_rootfs.h
phase2_privilege.o: phase2_privilege.h

evil: $(EVIL_BINS) $(PHASE2_EVIL_BINS)

phase2-evil: $(PHASE2_EVIL_BINS)

$(EVIL_BIN_DIR)/%: tests/evil/%.c | $(EVIL_BIN_DIR)
	$(CC) $(EVIL_CFLAGS) -o $@ $<

$(EVIL_BIN_DIR):
	mkdir -p $(EVIL_BIN_DIR)

clean:
	rm -f $(OBJS) $(TARGET)
	rm -rf $(EVIL_BIN_DIR)

test: all evil
	python3 tests/test_runner.py

test-setup-protocol: all
	mkdir -p $(EVIL_BIN_DIR)
	$(CC) $(CFLAGS) -o $(EVIL_BIN_DIR)/test_setup_handshake tests/test_setup_handshake.c phase2_cgroup.c phase2_userns.c phase2_rootfs.c phase2_privilege.c $(LDLIBS)
	$(EVIL_BIN_DIR)/test_setup_handshake
	$(CC) $(CFLAGS) -DPES_TEST_FORCE_CLOSE_RANGE_FAILURE -o $(EVIL_BIN_DIR)/test_setup_handshake_fallback tests/test_setup_handshake.c phase2_cgroup.c phase2_userns.c phase2_rootfs.c phase2_privilege.c $(LDLIBS)
	$(EVIL_BIN_DIR)/test_setup_handshake_fallback

test-phase2-structural: all
	mkdir -p $(EVIL_BIN_DIR)
	$(CC) $(CFLAGS) -o $(EVIL_BIN_DIR)/test_phase2_cgroup tests/test_phase2_cgroup.c phase2_cgroup.c
	$(EVIL_BIN_DIR)/test_phase2_cgroup

probe-userns-mapping: all
	mkdir -p $(EVIL_BIN_DIR)
	$(CC) $(CFLAGS) -o $(EVIL_BIN_DIR)/test_userns_mapping tests/test_userns_mapping.c phase2_userns.c
	$(EVIL_BIN_DIR)/test_userns_mapping

test-phase2-seccomp-safe: all
	mkdir -p $(EVIL_BIN_DIR)
	$(CC) $(CFLAGS) -o $(EVIL_BIN_DIR)/test_seccomp_policy tests/test_seccomp_policy.c $(LDLIBS)
	$(EVIL_BIN_DIR)/test_seccomp_policy

.PHONY: all clean evil test test-setup-protocol test-phase2-structural probe-userns-mapping test-phase2-seccomp-safe phase2-evil
