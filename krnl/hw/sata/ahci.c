#include "ahci.h"
#include "../pcie.h"
#include "../../log.h"
#include "../../mem/kheap.h"
#include "../../mem/pmm.h"
#include "../../cstdlib.h"
#include "../../fs/devfs.h"
#include "../../fs/vfs.h"

LOG_MODULE("ahci");

extern void* phys_to_virt(uint64_t phys_addr);

static uint32_t pcie_read_bar(pcie_device_info_t* dev, int bar_index) {
    uint32_t* bars = (uint32_t*)(dev->ecam_base + 0x10);
    return bars[bar_index];
}


#define AHCI_WAIT_CYCLES 1000000

static bool ahci_stop_cmd(volatile hba_port_t *port) {
    
    port->cmd &= ~HBA_PORT_CMD_ST;
    
    port->cmd &= ~HBA_PORT_CMD_FRE;

    
    for (int i = 0; i < AHCI_WAIT_CYCLES; i++) {
        if ((port->cmd & HBA_PORT_CMD_FR) == 0 && (port->cmd & HBA_PORT_CMD_CR) == 0)
            return true;
    }
    return false;
}

static void ahci_start_cmd(volatile hba_port_t *port) {
    
    while (port->cmd & HBA_PORT_CMD_CR);
    
    
    port->cmd |= HBA_PORT_CMD_FRE;
    port->cmd |= HBA_PORT_CMD_ST;
}

static void ahci_port_rebase(volatile hba_port_t *port) {
    if (!ahci_stop_cmd(port)) {
        LOG_WARN("AHCI: Could not stop command engine for port");
    }

    
    
    
    
    void* mem = pmm_alloc(4); 
    if (!mem) {
        LOG_WARN("AHCI: Out of memory for port rebase");
        return;
    }
    
    uint64_t mem_phys = (uint64_t)mem;

    
    port->clb = (uint32_t)(mem_phys & 0xFFFFFFFF);
    port->clbu = (uint32_t)(mem_phys >> 32);

    
    port->fb = (uint32_t)((mem_phys + 1024) & 0xFFFFFFFF);
    port->fbu = (uint32_t)((mem_phys + 1024) >> 32);

    
    hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)phys_to_virt(mem_phys);
    for (int i=0; i<32; i++) {
        cmdheader[i].prdtl = 8; 
        
        uint64_t ctba = mem_phys + 4096 + (i * 256);
        cmdheader[i].ctba = (uint32_t)(ctba & 0xFFFFFFFF);
        cmdheader[i].ctbau = (uint32_t)(ctba >> 32);
    }

    ahci_start_cmd(port);
}


static int ahci_find_cmdslot(volatile hba_port_t *port) {
    
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if ((slots & 1) == 0)
            return i;
        slots >>= 1;
    }
    return -1;
}

static uint64_t ahci_identify(volatile hba_port_t *port) {
    port->is = (uint32_t)-1; 
    int slot = ahci_find_cmdslot(port);
    if (slot == -1) {
        LOG_WARN("AHCI: No free command slots");
        return 0;
    }

    uint64_t clb_addr = port->clb | ((uint64_t)port->clbu << 32);
    hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)phys_to_virt(clb_addr);

    cmdheader += slot;
    cmdheader->cfl = sizeof(fis_reg_h2d_t)/sizeof(uint32_t); 
    cmdheader->w = 0; 
    cmdheader->prdtl = 1; 

    uint64_t ctba_addr = cmdheader->ctba | ((uint64_t)cmdheader->ctbau << 32);
    hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t*)phys_to_virt(ctba_addr);
    memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t) + (cmdheader->prdtl - 1)*sizeof(hba_prdt_entry_t));

    
    void* buffer_phys = pmm_alloc(1); 
    if (!buffer_phys) return 0;
    
    
    cmdtbl->prdt_entry[0].dba = (uint32_t)(uint64_t)buffer_phys;
    cmdtbl->prdt_entry[0].dbau = (uint32_t)((uint64_t)buffer_phys >> 32);
    cmdtbl->prdt_entry[0].dbc = 511; 
    cmdtbl->prdt_entry[0].i = 1;

    
    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t*)(&cmdtbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; 
    cmdfis->command = ATA_CMD_IDENTIFY;

    
    int spin = 0;
    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) { spin++; } 

    port->ci = 1 << slot;

    
    while (1) {
        if ((port->ci & (1 << slot)) == 0) 
            break;
        if (port->is & (1 << 30)) { 
            LOG_WARN("AHCI: Identify command failed (Task file error)");
            pmm_free(buffer_phys, 1);
            return 0;
        }
    }
    
    if (port->tfd & 0x01) { 
        LOG_WARN("AHCI: Identify command failed (Error bit set)");
        pmm_free(buffer_phys, 1);
        return 0;
    }

    uint16_t* identify_data = (uint16_t*)phys_to_virt((uint64_t)buffer_phys);
    
    
    uint64_t sectors = *((uint64_t*)&identify_data[100]); 
    
    
    char model[41];
    for (int i=0; i<20; i++) {
        model[i*2] = identify_data[27+i] >> 8;
        model[i*2+1] = identify_data[27+i] & 0xFF;
    }
    model[40] = 0;

    LOG_INFO("AHCI: IDENTIFY success. Model: %s, Sectors: %llu, Size: %llu MB", model, sectors, (sectors * 512) / (1024 * 1024));

    
    pmm_free(buffer_phys, 1);
    
    return sectors;
}

typedef struct {
    volatile hba_port_t* port;
    int port_no;
    uint64_t sectors;
    uint32_t sector_size;
} ahci_drive_t;

static bool ahci_read_sectors(volatile hba_port_t *port, uint64_t start_lba, uint32_t count, void *phys_buf) {
    port->is = (uint32_t)-1;
    int slot = ahci_find_cmdslot(port);
    if (slot == -1) return false;

    uint64_t clb_addr = port->clb | ((uint64_t)port->clbu << 32);
    hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)phys_to_virt(clb_addr);
    cmdheader += slot;
    cmdheader->cfl = sizeof(fis_reg_h2d_t)/sizeof(uint32_t); 
    cmdheader->w = 0; 

    uint32_t prdt_count = ((count * 512) - 1) / 0x400000 + 1; 
    cmdheader->prdtl = prdt_count; 

    uint64_t ctba_addr = cmdheader->ctba | ((uint64_t)cmdheader->ctbau << 32);
    hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t*)phys_to_virt(ctba_addr);
    memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t) + (cmdheader->prdtl - 1)*sizeof(hba_prdt_entry_t));

    uint64_t phys_addr = (uint64_t)phys_buf;
    uint32_t remain_bytes = count * 512;
    for (uint32_t i = 0; i < prdt_count; i++) {
        cmdtbl->prdt_entry[i].dba = (uint32_t)phys_addr;
        cmdtbl->prdt_entry[i].dbau = (uint32_t)(phys_addr >> 32);
        uint32_t chunk = remain_bytes > 0x400000 ? 0x400000 : remain_bytes;
        cmdtbl->prdt_entry[i].dbc = chunk - 1; 
        cmdtbl->prdt_entry[i].i = (i == prdt_count - 1) ? 1 : 0; 
        phys_addr += chunk;
        remain_bytes -= chunk;
    }

    
    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t*)(&cmdtbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;  
    cmdfis->command = ATA_CMD_READ_DMA_EXT;

    cmdfis->lba0 = (uint8_t)(start_lba & 0xFF);
    cmdfis->lba1 = (uint8_t)((start_lba >> 8) & 0xFF);
    cmdfis->lba2 = (uint8_t)((start_lba >> 16) & 0xFF);
    cmdfis->device = 1 << 6; 
    
    cmdfis->lba3 = (uint8_t)((start_lba >> 24) & 0xFF);
    cmdfis->lba4 = (uint8_t)((start_lba >> 32) & 0xFF);
    cmdfis->lba5 = (uint8_t)((start_lba >> 40) & 0xFF);
    
    cmdfis->countl = count & 0xFF;
    cmdfis->counth = (count >> 8) & 0xFF;

    int spin = 0;
    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) { spin++; } 
    
    port->ci = 1 << slot;

    while (1) {
        if ((port->ci & (1 << slot)) == 0) 
            break;
        if (port->is & (1 << 30)) { 
            return false;
        }
    }

    if (port->tfd & 0x01) { 
        return false;
    }

    return true;
}

static uint32_t ahci_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    ahci_drive_t* drive = (ahci_drive_t*)node->device;
    if (!drive) return 0;
    
    if (offset >= drive->sectors * drive->sector_size) return 0;
    if (offset + size > drive->sectors * drive->sector_size) {
        size = drive->sectors * drive->sector_size - offset;
    }

    uint64_t start_lba = offset / drive->sector_size;
    uint32_t start_offset = offset % drive->sector_size;
    uint64_t end_lba = (offset + size - 1) / drive->sector_size;
    uint32_t count = end_lba - start_lba + 1;

    
    uint32_t pages = (count * drive->sector_size + 4095) / 4096;
    void* phys_buf = pmm_alloc(pages);
    if (!phys_buf) return 0;

    if (!ahci_read_sectors(drive->port, start_lba, count, phys_buf)) {
        pmm_free(phys_buf, pages);
        return 0;
    }

    void* virt_buf = phys_to_virt((uint64_t)phys_buf);
    memcpy(buffer, (uint8_t*)virt_buf + start_offset, size);

    pmm_free(phys_buf, pages);
    return size;
}

static vfs_operations_t ahci_vfs_ops = {
    .read = ahci_vfs_read,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .mkdir = NULL
};

static uint32_t __ahci_drive_id = 0;

static void ahci_register_drive(volatile hba_port_t *port, int port_no, uint64_t sectors) {
    ahci_drive_t* drive = (ahci_drive_t*)khmalloc(sizeof(ahci_drive_t));
    drive->port = port;
    drive->port_no = port_no;
    drive->sectors = sectors;
    drive->sector_size = 512;

    vfs_node_t* node = vfs_alloc_node();
    node->type = VFS_BLOCK_DEVICE;
    node->ops = &ahci_vfs_ops;
    node->device = drive;
    node->size = sectors * 512;

    char dev_path[32];
    snprintf(dev_path, sizeof(dev_path), "sd%c", 'a' + __ahci_drive_id++);
    devfs_register_device(dev_path, node);
    LOG_INFO("AHCI: Registered drive /dev/%s (%llu MB)", dev_path, (sectors * 512) / (1024 * 1024));
}

void ahci_init(pcie_device_info_t* dev) {
    LOG_INFO("AHCI: Found controller at %d:%d.%d", dev->bus, dev->dev, dev->func);

    
    
    uint32_t abar = pcie_read_bar(dev, 5);
    if ((abar & 1) != 0) {
        LOG_WARN("AHCI: BAR5 does not appear to be MMIO");
    }

    uint64_t phys = (uint64_t)(abar & 0xFFFFFFF0);
    void* mmio = phys_to_virt(phys);
    if (!mmio) {
        LOG_WARN("AHCI: Cannot map BAR5 physical 0x%llx", phys);
        return;
    }

    volatile hba_mem_t* hba_mem = (volatile hba_mem_t*)mmio;

    uint32_t cap = hba_mem->cap;
    uint32_t ghc = hba_mem->ghc;
    uint32_t is  = hba_mem->is;
    uint32_t pi  = hba_mem->pi;

    
    if (!(ghc & 0x80000000)) {
        hba_mem->ghc |= 0x80000000;
        LOG_INFO("AHCI: Enabled AHCI mode (AE bit)");
    }

    int ports = 0;
    for (int i = 0; i < 32; i++) {
        if (pi & (1u << i)) ports++;
    }

    LOG_INFO("AHCI: CAP=0x%08x GHC=0x%08x IS=0x%08x PI=0x%08x -> Ports implemented: %d", cap, hba_mem->ghc, is, pi, ports);

    
    vfs_node_t* dev_dir = vfs_alloc_node();
    if (dev_dir) {
        dev_dir->type = VFS_CHAR_DEVICE;
        dev_dir->ops = NULL; 
        dev_dir->size = 0;
        dev_dir->device = NULL;
        devfs_register_device("ctrl/ahci", dev_dir);
        LOG_INFO("AHCI: Registered devfs directory /dev/ctrl/ahci");
    }

    
    for (int i = 0; i < 32; i++) {
        if (!(pi & (1u << i))) continue;

        volatile hba_port_t* port = &hba_mem->ports[i];
        
        uint32_t ssts = port->ssts;
        uint32_t sig  = port->sig;
        
        uint8_t det = ssts & 0x0F; 
        uint8_t ipm = (ssts >> 8) & 0x0F; 
        
        if (det == 3 && ipm == 1) { 
            const char* type = "Unknown";
            if (sig == SATA_SIG_ATA) type = "SATA Disk";
            else if (sig == SATA_SIG_ATAPI) type = "ATAPI (CD-ROM)";
            else if (sig == SATA_SIG_SEMB) type = "SEMB";
            else if (sig == SATA_SIG_PM) type = "Port Multiplier";

            LOG_INFO("AHCI Port %d: %s connected (SSTS=0x%08x, SIG=0x%08x)", i + 1, type, ssts, sig);
            
            if (sig == SATA_SIG_ATA) {
                ahci_port_rebase(port);
                LOG_INFO("AHCI Port %d: Rebased and command engine started", i + 1);
                
                uint64_t sectors = ahci_identify(port);
                if (sectors > 0) {
                    ahci_register_drive(port, i, sectors);
                }
            }

        }
    }

    LOG_INFO("AHCI: Initialization complete.");
}
