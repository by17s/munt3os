#pragma once

#include <stdint.h>
#include "../fs/vfs.h"
#include "../task/sched.h"
#include "../mem/vmm.h"

#define ELF_MAGIC 0x464C457F 

#define ELF_CLASS64 2
#define ELF_DATA2LSB 1
#define ELF_MACH_X86_64 0x3E

#define ELF_PT_LOAD 1

typedef struct {
    uint32_t magic;
    uint8_t  elf_class;
    uint8_t  data_encoding;
    uint8_t  version;
    uint8_t  os_abi;
    uint8_t  abi_version;
    uint8_t  pad[7];
    uint16_t type;
    uint16_t machine;
    uint32_t elf_version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed)) elf64_phdr_t;



uint64_t elf_load(vfs_node_t* node, vmm_context_t* ctx);
