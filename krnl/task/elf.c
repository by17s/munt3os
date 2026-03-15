#include "elf.h"
#include "../fs/vfs.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "../mem/kheap.h"
#include "../log.h"
#include <string.h>

LOG_MODULE("elf");

uint64_t elf_load(vfs_node_t* node, vmm_context_t* ctx) {
    if (!node) return 0;

    elf64_ehdr_t ehdr;
    if (vfs_read(node, 0, sizeof(elf64_ehdr_t), (uint8_t*)&ehdr) != sizeof(elf64_ehdr_t)) {
        LOG_ERROR("Failed to read ELF header");
        return 0;
    }

    if (ehdr.magic != ELF_MAGIC || ehdr.elf_class != ELF_CLASS64 || 
        ehdr.data_encoding != ELF_DATA2LSB || ehdr.machine != ELF_MACH_X86_64) {
        LOG_ERROR("Invalid ELF signature or unsupported machine");
        return 0;
    }

    if (ehdr.ehsize < sizeof(elf64_ehdr_t)) {
        LOG_ERROR("Invalid ELF header size");
        return 0;
    }
    
    elf64_phdr_t* phdrs = khmalloc(ehdr.phnum * sizeof(elf64_phdr_t));
    if (!(uint8_t*)phdrs) return 0;
    
    if (vfs_read(node, ehdr.phoff, ehdr.phnum * sizeof(elf64_phdr_t), (uint8_t*)phdrs) != ehdr.phnum * sizeof(elf64_phdr_t)) {
        LOG_ERROR("Failed to read program headers");
        khfree((uint8_t*)phdrs);
        return 0;
    }

    for (int i = 0; i < ehdr.phnum; i++) {
        if (phdrs[i].type == ELF_PT_LOAD) {
            uint64_t memsz = phdrs[i].memsz;
            uint64_t filesz = phdrs[i].filesz;
            uint64_t vaddr = phdrs[i].vaddr;
            uint64_t offset = phdrs[i].offset;

            
            uint64_t aligned_vaddr = vaddr & ~0xFFF;
            uint64_t diff = vaddr - aligned_vaddr;
            uint64_t pages = (memsz + diff + 0xFFF) / 0x1000;
            
            for (uint64_t j = 0; j < pages; j++) {
                uint64_t target_vaddr = aligned_vaddr + j * 0x1000;
                
                if (vmm_virt_to_phys(ctx, target_vaddr) == 0) {
                    uint64_t paddr = (uint64_t)pmm_alloc(1);
                    
                    memset((void*)(paddr + 0xFFFF800000000000), 0, 0x1000);
                    vmm_map(ctx, target_vaddr, paddr, VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
                }
            }
            
            
            
            
            
            uint8_t* temp_buf = khmalloc(filesz);
            if(temp_buf) {
                if (vfs_read(node, offset, filesz, temp_buf) == filesz) {
                    
                    uint64_t old_cr3;
                    asm volatile("mov %%cr3, %0" : "=r"(old_cr3));
                    vmm_switch_context(ctx);
                    memcpy((void*)vaddr, temp_buf, filesz);
                    memset((void*)(vaddr + filesz), 0, memsz - filesz);
                    asm volatile("mov %0, %%cr3" :: "r"(old_cr3) : "memory");
                }
                khfree(temp_buf);
            }
        }
    }

    khfree((uint8_t*)phdrs);
    return ehdr.entry;
}
