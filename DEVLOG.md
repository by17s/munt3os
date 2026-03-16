# Devlog

## 2026-03-17
### Multitasking and Credentials:
- Added `cred_t` struct to represent thread credentials, including UID, GID, EUID, and EGID.    
- Updated `thread_t` struct to include a `cred` field of type `cred_t`.
- Implemented `setuid`, `setgid`, `seteuid`, and `setegid` functions to update the corresponding fields in the current thread's credentials.
- Updated `sched_create_thread` to initialize the `cred` field of new threads with default values (UID=0, GID=0, EUID=0, EGID=0).
- Added new system calls for getting and setting user and group IDs, and updated `sysnums.h` accordingly.
### Configuration:
- Added system configuration files (.moscfg) support.
- Updated tty to support .moscfg files.
### Cryptography and Hashing:
- Implemented SHA-256 hashing algorithm in `utils/sha256.c`.
### Utilities:
- Added `hashmap` and `list` data structures in `utils/hashmap.c` and `utils/list.c`.