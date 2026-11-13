#ifndef SANDBOX_H
#define SANDBOX_H

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

/*
 * Run the target binary at binary_path.
 * Returns 0 on success, or non-zero on failure.
 */
int run_sandbox(const char *binary_path);

#endif /* SANDBOX_H */
