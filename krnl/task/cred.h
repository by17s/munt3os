#pragma once

#include <stddef.h>

typedef struct cred {
    uid_t uid;
    gid_t gid;

    uid_t euid;
    gid_t egid;
} cred_t;