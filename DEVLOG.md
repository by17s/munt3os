# Devlog

## 2026-03-25
### Filesystem:
- Fix memory leaks in VFS and ext2
- Added ext2 write support, including handling of direct, single indirect, and double indirect blocks.
### Capabilities:
- Implemented a basic capability system for thread permissions, allowing threads to have specific capabilities that grant them certain privileges.
- Updated thread management to check capabilities before allowing certain operations, such as creating threads or accessing certain resources.
### System Calls:
- Added create, remove, mkdir, rmdir, sysctl and other file-related system calls.
### Programs:
- Added arguments support XD
### /bin:
- Added `ls`, `cmp`, `wc`, `cat`, `echo`, `stat`
### Hardware Support:
- Added (Return) ahci 1.0 support for SATA devices (Please disable this driver if use real hardware, as it is still in early stages and may cause instability).
### tty:
- Added support for .moscfg configuration files, allowing users to customize terminal settings.
- Added yeilding support to the terminal, allowing it to yield control to other threads when waiting for input or output operations to complete.

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