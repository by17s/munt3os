#include "cmdline.h"
#include "log.h"
#include <cstdlib.h>
#include <tty.h>


#include "mem/kslab.h"
#include "hw/acpi.h"
#include "fs/vfs.h"
#include "hw/pcie.h"
#include "hw/usb/usb.h"
#include "mem/pmm.h"
#include "mem/kheap.h"
#include "task/sched.h"
#include "task/exec.h"

#define CMDLINE_BUFSIZE 1024
#define CMDLINE_MAXARGS 64

#define C_RESET   "\033[0m"
#define C_LOGO    "\033[1;36m"  
#define C_TITLE   "\033[1;35m"  
#define C_LABEL   "\033[1;34m"  
#define C_TEXT    "\033[0m" 

#define PROMPT "munt3os>  "

static int cmd_ls(tty_t *tty, vfs_node_t* target) {
    if (!target) {
        tty->puts(tty, "No such directory\n");
        return -1;
    }

    if (target->type != VFS_DIRECTORY) {
        tty->puts(tty, "Not a directory\n");
        return -1;
    }

    uint32_t i = 0;
    struct dirent* dent;
    while ((dent = vfs_readdir(target, i++)) != NULL) {
        tty->printf(tty, "%s  ", dent->name);
    }
    tty->putchar(tty, '\n');
    return 0;
}


static int execute_cmd(tty_t *tty, int argc, char **argv) {
    if (argc == 0) return 0;

    if (strcmp(argv[0], "echo") == 0) {
        for (int i = 1; i < argc; ++i) {
            if (i > 1) tty->putchar(tty, ' ');
            tty->puts(tty, argv[i]);
        }
        tty->putchar(tty, '\n');
        return 0;
    }

    if (strcmp(argv[0], "help") == 0) {
        tty->puts(tty, "Available commands: \n");
        tty->puts(tty, "  echo [args...] - Print arguments to the console\n");
        tty->puts(tty, "  help           - Show this help message\n");
        tty->puts(tty, "  cat [file]     - Display contents of a file\n");
        tty->puts(tty, "  cd [dir]       - Change the current directory\n");
        tty->puts(tty, "  ls             - List files in the current directory\n");
        tty->puts(tty, "  file [file]    - Show information about a file\n");
        tty->puts(tty, "  --- Hardware Information ---\n");
        tty->puts(tty, "  lsusb          - List USB controllers and connected devices\n");
        tty->puts(tty, "  lspci          - List PCIe devices\n");
        tty->puts(tty, "  --- Memory Information ---\n");
        tty->puts(tty, "  meminfo        - Show memory usage statistics\n");
        tty->puts(tty, "  slabinfo       - Show slab allocator information\n");
        tty->puts(tty, "  kheapinfo      - Show kernel heap usage information\n");
        tty->puts(tty, "  --- Execution ---\n");
        tty->puts(tty, "  ./<path>       - Execute a program from the filesystem\n");
        tty->puts(tty, "  execz <path>   - Execute a program from the filesystem (shadow)\n");
        tty->puts(tty, "  ps             - Show running processes\n");
        tty->puts(tty, "  --- System Control (TODO) ---\n");
        tty->puts(tty, "  shutdown        - Shut down the system\n");
        tty->puts(tty, "  reboot          - Reboot the system\n");
        tty->puts(tty, "  systemctl       - Control the init system and service manager\n");
        
        return 0;
    }

    if (strcmp(argv[0], "lsusb") == 0) {
        if (usb_controller_count == 0) {
            tty->puts(tty, "No USB controllers found.\n");
        } else {
            vfs_node_t* usb_root = vfs_get_root();
            if (usb_root) usb_root = vfs_finddir(usb_root, "dev");
            if (usb_root) usb_root = vfs_finddir(usb_root, "usb");
            if (!usb_root) {
                tty->puts(tty, "USB subsystem not found.\n");
                return 0;
            }
            tty->puts(tty, "USB Controllers:\n");
            cmd_ls(tty, usb_root); 

            vfs_node_t* usb_main_root = usb_root;

            char* controller_types[] = {"ohci", "ehci", "xhci"};
            tty->puts(tty, "Connected USB Devices:\n");
            for (size_t i = 0; i < 3; i++) {
                usb_root = vfs_finddir(usb_main_root, controller_types[i]);
                if (usb_root) {
                    uint32_t i = 0;
                    struct dirent* dent;
                    vfs_node_t *usb_dev;
                    while ((dent = vfs_readdir(usb_root, i++)) != NULL) {
                        tty->printf(tty, "%s  ", dent->name);
                        usb_dev = vfs_finddir(usb_root, dent->name);
                        if (usb_dev && usb_dev->device) {
                            struct usb_dev* dev = (struct usb_dev*)usb_dev->device;
                            if (dev->desc_fetched) {
                                tty->printf(tty, "VID:0x%04x PID:0x%04x | Class:%d Sub:%d Protocol:%d | MaxPacket0:%d USB:%x.%02x\n", 
                                    dev->desc.idVendor, dev->desc.idProduct, dev->desc.bDeviceClass, dev->desc.bDeviceSubClass, 
                                    dev->desc.bDeviceProtocol, dev->desc.bMaxPacketSize0, 
                                    (dev->desc.bcdUSB >> 8) & 0xFF, dev->desc.bcdUSB & 0xFF);
                            } else {
                                tty->puts(tty, "Descriptor not fetched\n");
                            }
                        } else {
                            tty->puts(tty, "Invalid device node\n");
                        }
                    }
                }
            }
        }
        return 0;
    }

    if (strcmp(argv[0], "lspci") == 0) {
        tty->puts(tty, "PCIe Devices:\n");
        tty->puts(tty, "   B:D.F  | Vendor | Device | Class ID | Class Name\n");
        tty->puts(tty, "----------|--------|--------|----------|-----------------\n");
        
        for (int i = 0; i < pcie_device_count; i++) {
            pcie_device_info_t* dev = &pcie_devices[i];
            const char* class_name = pcie_get_class_name(dev->header.class_code);
            
            tty->printf(tty, "  %02x:%02x.%d | 0x%04x | 0x%04x |   0x%02x   | %s\n",
                dev->bus, dev->dev, dev->func,
                dev->header.vendor_id, dev->header.device_id,
                dev->header.class_code, class_name);
        }
        tty->printf(tty, "Total devices found: %d\n", pcie_device_count);
        return 0;
    }

    if (strcmp(argv[0], "ls") == 0) {
        vfs_node_t* target = tty->cwd;
        if (!target) {
            target = vfs_get_root();
            tty->cwd = target; 
        }

        if (argc > 1) {
            char* abs_path = vfs_resolve_path(tty->pwd, argv[1]);
            if (!abs_path) return 0;
            
            target = vfs_get_root();
            if (strcmp(abs_path, "/") != 0) {
                char* temp_path = (char*)khmalloc(strlen(abs_path) + 1);
                strcpy(temp_path, abs_path);

                char* p = temp_path;
                if (*p == '/') p++;
                
                while (*p) {
                    char* slash = NULL;
                    for (int i = 0; p[i]; i++) {
                        if (p[i] == '/') {
                            slash = &p[i];
                            break;
                        }
                    }
                    if (slash) *slash = '\0';
                    
                    target = vfs_finddir(target, p);
                    if (!target) {
                        break;
                    }
                    
                    if (slash) {
                        p = slash + 1;
                    } else {
                        break;
                    }
                }
                khfree(temp_path);
            }
            if (!target) {
                tty->printf(tty, "ls: cannot access '%s': No such file or directory\n", argv[1]);
                khfree(abs_path);
                return 0;
            }
            khfree(abs_path);
        }

        if (!target) {
            tty->puts(tty, "VFS not initialized or no working directory\n");
            return 0;
        }

        if (target->type != VFS_DIRECTORY) {
            tty->printf(tty, "%s\n", target->name);
            return 0;
        }

        uint32_t i = 0;
        struct dirent* dent;
        vfs_node_t *child;
        while ((dent = vfs_readdir(target, i++)) != NULL) {
            child = vfs_finddir(target, dent->name);
            tty->printf(tty, "%s%s  ", dent->name, (child && child->type == VFS_DIRECTORY) ? "/" : "");
        }
        tty->puts(tty, "\n");
        return 0;
    }

    if (strcmp(argv[0], "cd") == 0) {
        if (argc < 2) {
            if (tty->cd(tty, "/") != 0) {
                tty->puts(tty, "cd: failed to change directory to /\n");
            }
            return 0;
        }
        
        if (tty->cd(tty, argv[1]) != 0) {
            tty->printf(tty, "cd: %s: Not a directory\n", argv[1]);
        }
        return 0;
    }

    if (strcmp(argv[0], "cat") == 0) {
        if (argc < 2) {
            tty->puts(tty, "cat: missing operand\n");
            return 0;
        }
        
        char* abs_path = vfs_resolve_path(tty->pwd, argv[1]);
        if (!abs_path) return 0;

        vfs_node_t* root = vfs_get_root();
        vfs_node_t* target = root;
        
        if (strcmp(abs_path, "/") != 0) {
            char* temp_path = (char*)khmalloc(strlen(abs_path) + 1);
            strcpy(temp_path, abs_path);

            char* p = temp_path;
            if (*p == '/') p++;
            
            while (*p) {
                char* slash = NULL;
                for (int j = 0; p[j]; j++) {
                    if (p[j] == '/') {
                        slash = &p[j];
                        break;
                    }
                }
                if (slash) *slash = '\0';
                
                target = vfs_finddir(target, p);
                if (!target) {
                    break;
                }
                
                if (slash) {
                    p = slash + 1;
                } else {
                    break;
                }
            }
            khfree(temp_path);
        }

        if (!target) {
            tty->printf(tty, "cat: %s: No such file or directory\n", argv[1]);
        } else if (target->type == VFS_DIRECTORY) {
            tty->printf(tty, "cat: %s: Is a directory\n", argv[1]);
        } else {
            uint8_t* buf = (uint8_t*)khmalloc(target->size + 1);
            if (buf) {
                uint32_t bytes_read = vfs_read(target, 0, target->size, buf);
                buf[bytes_read] = '\0';
                tty->puts(tty, (char*)buf);
                if (bytes_read > 0 && buf[bytes_read - 1] != '\n') {
                    tty->puts(tty, "\n");
                }
                khfree(buf);
            }
        }
        khfree(abs_path);
        return 0;
    }

    if (strcmp(argv[0], "file") == 0) {
        if (argc < 2) {
            tty->puts(tty, "file: missing operand\n");
            return 0;
        }

        char* abs_path = vfs_resolve_path(tty->pwd, argv[1]);

        if (!abs_path) 
            return 0;

        vfs_node_t *file = kopen(abs_path);
        if (!file) {
            tty->printf(tty, "file: %s: No such file or directory\n", argv[1]);
        } else {
            const char* type_str = "Unknown";
            switch (file->type) {
                case VFS_FILE: type_str = "File"; break;
                case VFS_DIRECTORY: type_str = "Directory"; break;
                case VFS_CHAR_DEVICE: type_str = "Character Device"; break;
                case VFS_BLOCK_DEVICE: type_str = "Block Device"; break;
                case VFS_PIPE: type_str = "Pipe"; break;
                case VFS_SYMLINK: type_str = "Symbolic Link"; break;
                case VFS_MOUNTPOINT: type_str = "Mount Point"; break;
                default: break;
            }
            tty->printf(tty, "Type: %s\n", type_str);
            char* perms = (file->mask & 0x4) ? "r" : "-";
            tty->printf(tty, "Permissions: %s\n", perms);
            
            tty->printf(tty, "Size: %u bytes\n", file->size);
            if (file->type == VFS_CHAR_DEVICE || file->type == VFS_BLOCK_DEVICE) {
                tty->printf(tty, "Device ID: %u\n", file->inode);
            }
            kclose(file);
        }

        khfree(abs_path);
        return 0;
    }

    if(strcmp(argv[0], "mkdir") == 0) {
        if(argc < 2) {
            tty->puts(tty, "mkdir: missing operand\n");
            return 0;
        }
        vfs_node_t* target = tty->cwd;
        if(vfs_mkdir(target, argv[1], 0x777) != 0) {
            tty->printf(tty, "mkdir: failed to create directory '%s'\n", argv[1]);
        }
        return 0;
    }

    if(strcmp(argv[0], "create") == 0) {
        if(argc < 2) {
            tty->puts(tty, "create: missing operand\n");
            return 0;
        }
        vfs_node_t* target = tty->cwd;
        int new_file = vfs_create(target, argv[1], 0x666);
        if(new_file) {
            tty->printf(tty, "create: failed to create file '%s'\n", argv[1]);
        }
        return 0;

    }

    if (strcmp(argv[0], "meminfo") == 0) {
        return 0;
    }

    if (strcmp(argv[0], "slabinfo") == 0) {
        kmem_cache_dump_info();
        return 0;
    }

    if (strcmp(argv[0], "kheapinfo") == 0) {
        kheap_stat_t khstat = kheap_get_stat();
        tty->printf(tty, "Kernel Heap Usage:\n");
        tty->printf(tty, "  Total Size: %llu bytes\n", khstat.total_size);
        tty->printf(tty, "  Used Size: %llu bytes\n", khstat.used_size);
        tty->printf(tty, "  Free Size: %llu bytes\n", khstat.free_size);
        tty->printf(tty, "  Block Count: %llu\n", khstat.block_count);
        return 0;
    }

    if (strcmp(argv[0], "cpus") == 0) {
        acpi_cpu_dump();
        return 0;
    }

    if (strcmp(argv[0], "exit") == 0) {
        return 1; 
    }

    if (strcmp(argv[0], "clear") == 0) {
        if (tty->clear) {
            tty->clear(tty);
        }
        return 0; 
    }

    if (strcmp(argv[0], "fork") == 0) {
        uint64_t child_tid = sched_fork();
        if (child_tid == 0) {
            tty->printf(tty, "I am the child thread! Tid is 0 here.\n");
            
            
            while(1) { asm volatile("hlt"); } 
        } else if (child_tid != (uint64_t)-1) {
            tty->printf(tty, "I am the parent! Created child thread TID: %llu\n", child_tid);
        } else {
            tty->printf(tty, "Fork failed!\n");
        }
        return 0;
    }

    if (strcmp(argv[0], "cpus") == 0) {
        acpi_cpu_dump();
        return 0;
    }

    if (strcmp(argv[0], "ps") == 0) {
        sched_print_threads();
        return 0;
    }

    //Execute a program in a child process without waiting for it (shadow execution)
    if (strcmp(argv[0], "execz") == 0) {
        if (argc < 2) {
            tty->printf(tty, "Usage: execz <path>\n");
            return 0;
        }

        uint64_t child_tid = sched_fork();
        if (child_tid == 0) {
            
            int ret = sys_execve(argv[1], (const char**)&argv[1]);
            tty->printf(tty, "Exec failed with code %d\n", ret);
            sched_exit(ret); 
        } else if (child_tid != (uint64_t)-1) {
            tty->printf(tty, "Spawned child process %llu for %s\n", child_tid, argv[1]);
        } else {
            tty->printf(tty, "Fork failed before exec!\n");
        }
        return 0;
    }

    if (memcmp(argv[0], "./", 2) == 0) {
        uint64_t child_tid = sched_fork();
        if (child_tid == 0) {
            const char* exec_argv[argc + 1];
            exec_argv[0] = &argv[0][2];
            for (int i = 1; i < argc; i++) exec_argv[i] = argv[i];
            exec_argv[argc] = NULL;
            int ret = sys_execve(exec_argv[0], exec_argv);
            tty->printf(tty, "File %s execution failed: permission denied (code %d)\n", exec_argv[0], ret);
            sched_exit(ret); 
        } else if (child_tid != (uint64_t)-1) {
            int status = 0;
            sched_waitpid(child_tid, &status);
            //tty->printf(tty, "Process %llu exited with status %d\n", child_tid, status);
        } else {
            tty->printf(tty, "Fork failed before exec!\n");
        }
        return 0;
    }

    tty->printf(tty, "Unknown command: %s\n", argv[0]);
    return 0;
}


static int parse_and_execute(tty_t* tty, char *line) {
    char *args[CMDLINE_MAXARGS];
    int argc = 0;
    char *p = line;

    while (*p && argc < CMDLINE_MAXARGS) {
        while (isspace((unsigned char)*p)) ++p;
        if (*p == '\0') break;

        if (*p == '"') {
            ++p;
            args[argc++] = p;
            while (*p && *p != '"') ++p;
            if (*p == '"') {
                *p++ = '\0';
            }
        } else {
            args[argc++] = p;
            while (*p && !isspace((unsigned char)*p)) ++p;
            if (*p) *p++ = '\0';
        }
    }

    return execute_cmd(tty, argc, args);
}


void cmdline_run(void) {
    tty_t *tty;
    tty_get(-1, &tty);

    char buf[CMDLINE_BUFSIZE];
    size_t pos = 0;

    
    tty->printf(tty, C_LABEL"root"C_RESET"@"C_LOGO"munt3os"C_RESET":"C_TITLE"%s"C_RESET"# [", tty->pwd ? tty->pwd : "/");

    while (true) {
        int c = tty->getchar(tty);

        if (c == 0 || c == -1) {
            
            
            asm volatile("hlt");
            continue;
        }

        
        if (c == '\r' || c == '\n') {
            tty->putchar(tty, '\n'); 
            
            buf[pos] = '\0';
            if (pos > 0) {
                if (parse_and_execute(tty, buf) == 1) {
                    break; 
                }
            }
            
            pos = 0;
            tty->printf(tty, C_LABEL"root"C_RESET"@"C_LOGO"munt3os"C_RESET":"C_TITLE"%s"C_RESET"# [", tty->pwd ? tty->pwd : "/"); 
            continue;
        }

        
        if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                pos--;
                
                tty->puts(tty, "\b\b["); 
            }
            continue;
        }

        
        if (pos + 1 < CMDLINE_BUFSIZE) {
            buf[pos++] = (char)c;
            tty->putchar(tty, '\b'); 
            tty->putchar(tty, c); 
            tty->putchar(tty, '['); 
        }
    }
}