#ifndef PHASE2_PRIVILEGE_H
#define PHASE2_PRIVILEGE_H

#include <sys/types.h>

int phase2_child_create_namespaces(void);
int phase2_set_parent_death_signal(pid_t expected_parent);
int phase2_privilege_read_cap_last_cap(int *last_cap);
int phase2_target_drop_capabilities_and_identity(void);
int phase2_target_enable_nnp(void);
int phase2_target_install_seccomp(void);
int phase2_supervisor_drop_privileges(int last_cap);

#endif
