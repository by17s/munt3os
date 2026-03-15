## Structure

The project is organized into several directories, each serving a specific purpose:
- `api`: Contains the public API definitions for the operating system, including system calls and user-space interfaces.
- `dev`: FS drivers for various devices, such as storage, input, and display (fb).
- `fs`: Contains the file system implementations.
- `mem`: Memory management code, including physical memory management (pmm), virtual memory management (vmm), heap allocator, slab allocator, and buddy allocator.
- `task`: Contains the process management code, including process creation, scheduling, and inter-process
- `hw`: Contains hardware-specific code, such as drivers and architecture-specific implementations.
- `net`: Contains network-related code, such as network drivers and protocols.
- `util`: Contains utility functions and common code used throughout the kernel (Including pseudo-shell).