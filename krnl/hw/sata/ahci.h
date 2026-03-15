#pragma once

#include "../pcie.h"
#include <stdint.h>
#include <stdbool.h>

#define SATA_SIG_ATA    0x00000101  
#define SATA_SIG_ATAPI  0xEB140101  
#define SATA_SIG_SEMB   0xC33C0101  
#define SATA_SIG_PM     0x96690101  

#define AHCI_PORT_DET_PRESENT 3
#define AHCI_PORT_IPM_ACTIVE 1

#define HBA_PORT_CMD_ST   0x0001
#define HBA_PORT_CMD_FRE  0x0010
#define HBA_PORT_CMD_FR   0x4000
#define HBA_PORT_CMD_CR   0x8000

#define ATA_CMD_READ_PIO          0x20
#define ATA_CMD_READ_PIO_EXT      0x24
#define ATA_CMD_READ_DMA          0xC8
#define ATA_CMD_READ_DMA_EXT      0x25
#define ATA_CMD_WRITE_PIO         0x30
#define ATA_CMD_WRITE_PIO_EXT     0x34
#define ATA_CMD_WRITE_DMA         0xCA
#define ATA_CMD_WRITE_DMA_EXT     0x35
#define ATA_CMD_CACHE_FLUSH       0xE7
#define ATA_CMD_CACHE_FLUSH_EXT   0xEA
#define ATA_CMD_PACKET            0xA0
#define ATA_CMD_IDENTIFY_PACKET   0xA1
#define ATA_CMD_IDENTIFY          0xEC

typedef enum {
    FIS_TYPE_REG_H2D    = 0x27, 
    FIS_TYPE_REG_D2H    = 0x34, 
    FIS_TYPE_DMA_ACT    = 0x39, 
    FIS_TYPE_DMA_SETUP  = 0x41, 
    FIS_TYPE_DATA       = 0x46, 
    FIS_TYPE_BIST       = 0x58, 
    FIS_TYPE_PIO_SETUP  = 0x5F, 
    FIS_TYPE_DEV_BITS   = 0xA1, 
} fis_type_t;

typedef struct {
    uint8_t  fis_type;  
    uint8_t  pmport:4;  
    uint8_t  rsv0:3;    
    uint8_t  c:1;       
    uint8_t  command;   
    uint8_t  featurel;  
    uint8_t  lba0;      
    uint8_t  lba1;      
    uint8_t  lba2;      
    uint8_t  device;    
    uint8_t  lba3;      
    uint8_t  lba4;      
    uint8_t  lba5;      
    uint8_t  featureh;  
    uint8_t  countl;    
    uint8_t  counth;    
    uint8_t  icc;       
    uint8_t  control;   
    uint8_t  rsv1[4];   
} __attribute__((packed)) fis_reg_h2d_t;

typedef struct {
    uint32_t dba;       
    uint32_t dbau;      
    uint32_t rsv0;      
    uint32_t dbc:22;    
    uint32_t rsv1:9;    
    uint32_t i:1;       
} __attribute__((packed)) hba_prdt_entry_t;

typedef struct {
    uint8_t  cfis[64]; 
    uint8_t  acmd[16]; 
    uint8_t  rsv[48];
    hba_prdt_entry_t prdt_entry[1]; 
} __attribute__((packed)) hba_cmd_tbl_t;

typedef struct {
    uint8_t  cfl:5;     
    uint8_t  a:1;       
    uint8_t  w:1;       
    uint8_t  p:1;       
    uint8_t  r:1;       
    uint8_t  b:1;       
    uint8_t  c:1;       
    uint8_t  rsv0:1;    
    uint8_t  pmp:4;     
    uint16_t prdtl;     
    volatile uint32_t prdbc; 
    uint32_t ctba;      
    uint32_t ctbau;     
    uint32_t rsv1[4];   
} __attribute__((packed)) hba_cmd_header_t;

typedef struct {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t rsv0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t rsv1[11];
    uint32_t vendor[4];
} __attribute__((packed)) hba_port_t;

typedef struct {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t  rsv[0xA0-0x2C];
    uint8_t  vendor[0x100-0xA0];
    hba_port_t ports[32];
} __attribute__((packed)) hba_mem_t;

void ahci_init(pcie_device_info_t* dev);

