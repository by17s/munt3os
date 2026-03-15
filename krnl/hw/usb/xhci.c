#include "xhci.h"
#include "usb.h"
#include "usb_kbd.h"
#include <log.h>
#include "../../mem/pmm.h"
#include "../../mem/kheap.h"
#include "../../cstdlib.h"
#include "../../fs/devfs.h"
#include "../../fs/vfs.h"
#include "../../dev/input.h"

LOG_MODULE("xhci")

extern void* phys_to_virt(uint64_t phys_addr);

typedef struct {
    uint32_t param[4];
} __attribute__((packed)) xhci_trb_t;

typedef struct {
    uint64_t base;
    uint16_t size;
    uint16_t reserved[3];
} __attribute__((packed)) xhci_erst_entry_t;

typedef struct {
    uint32_t param[8]; 
} __attribute__((packed)) xhci_ctx32_t;


typedef struct {
    xhci_ctx32_t slot;
    xhci_ctx32_t ep[31];
} __attribute__((packed)) xhci_device_context_t;


typedef struct {
    xhci_ctx32_t control;  
    xhci_ctx32_t slot;     
    xhci_ctx32_t ep[31];   
} __attribute__((packed)) xhci_input_context_t;


#define XHCI_MAX_SLOTS 8
typedef struct {
    xhci_device_context_t dev_ctx;               
    xhci_trb_t ep0_ring[256];                    
} __attribute__((packed, aligned(64))) xhci_slot_data_t;

typedef struct {
    uint64_t dcbaa[256];                         
    xhci_erst_entry_t erst[1];                   
    uint8_t  padding1[2032];                     
    xhci_trb_t cmd_ring[256];                    
    xhci_trb_t event_ring[256];                  
    
    
    xhci_input_context_t in_ctx;                 
    uint8_t padding2[32];                        
    
    
    xhci_slot_data_t slots[XHCI_MAX_SLOTS];     
    
    uint8_t desc_buf[512];                       
    xhci_trb_t int_in_ring[256];                 
    uint8_t kbd_report[8];                       
    uint8_t cfg_desc_buf[256];                   
    uint8_t led_report[1];                       
} __attribute__((packed, aligned(4096))) xhci_mem_t;


#define XHCI_MAX_CONTROLLERS 4
typedef struct {
    xhci_mem_t*         mem_virt;
    xhci_mem_t*         mem_phys;
    volatile uint32_t*  ir0_regs;
    volatile uint32_t*  db_regs;
    volatile uint32_t*  op_regs;
    int                 ev_ccs;
    int                 ev_deq;
    int                 kbd_slot_id;
    int                 kbd_slot_idx;      
    int                 kbd_ep_dci;        
    int                 int_in_enq;        
    int                 int_in_pcs;        
    int                 ep0_enq;           
    int                 ep0_pcs;           
    uint8_t             kbd_iface_num;     
    uint8_t             kbd_leds;          
    int                 has_kbd;           
    int                 poll_count;        
} xhci_controller_state_t;

static xhci_controller_state_t xhci_ctrls[XHCI_MAX_CONTROLLERS];
static int xhci_ctrl_count = 0;

static void dummy_mdelay(int ms) {
    for (volatile uint32_t i = 0; i < ms * 10000; i++) {
        asm volatile("pause");
    }
}

static uint32_t pcie_read_bar(pcie_device_info_t* dev, int bar_index) {
    uint32_t* bars = (uint32_t*)(dev->ecam_base + 0x10);
    return bars[bar_index];
}

static uint64_t v2p(xhci_mem_t* virt, xhci_mem_t* phys, void* p) {
    return (uint64_t)phys + ((uint8_t*)p - (uint8_t*)virt);
}

static void xhci_enqueue_cmd(xhci_mem_t* mem, volatile uint32_t* db_regs, int* cmd_enq, int* cmd_pcs, xhci_trb_t trb) {
    int i = *cmd_enq;
    mem->cmd_ring[i].param[0] = trb.param[0];
    mem->cmd_ring[i].param[1] = trb.param[1];
    mem->cmd_ring[i].param[2] = trb.param[2];
    
    uint32_t ctrl = trb.param[3] & ~1;
    ctrl |= (*cmd_pcs & 1);
    mem->cmd_ring[i].param[3] = ctrl;
    
    (*cmd_enq)++;
    
    db_regs[0] = 0;
}

static void xhci_advance_event(xhci_mem_t* mem, xhci_mem_t* mem_phys, volatile uint32_t* ir0_regs, int* ev_ccs, int* ev_deq) {
    (*ev_deq)++;
    if (*ev_deq == 256) {
        *ev_deq = 0;
        *ev_ccs ^= 1;
    }
    uint64_t erdp_phys = v2p(mem, mem_phys, &mem->event_ring[*ev_deq]);
    erdp_phys |= 8; 
    ir0_regs[6] = (uint32_t)(erdp_phys & 0xFFFFFFFF);
    ir0_regs[7] = (uint32_t)(erdp_phys >> 32);
}

static xhci_trb_t* xhci_wait_for_event(xhci_mem_t* mem, xhci_mem_t* mem_phys, volatile uint32_t* ir0_regs, int* ev_ccs, int* ev_deq, int expected_type) {
    int spin_total = 0;
    while (spin_total < 5000) {
        xhci_trb_t* ev = &mem->event_ring[*ev_deq];
        if ((ev->param[3] & 1) == *ev_ccs) {
            int trb_type = (ev->param[3] >> 10) & 0x3F;
            if (trb_type == expected_type) {
                return ev;
            } else {
                
                xhci_advance_event(mem, mem_phys, ir0_regs, ev_ccs, ev_deq);
            }
            spin_total = 0;
        } else {
            dummy_mdelay(1);
            spin_total++;
        }
    }
    return NULL;
}



static void xhci_set_kbd_leds(xhci_controller_state_t* ctrl, uint8_t led_bits) {
    xhci_mem_t* mem = ctrl->mem_virt;
    xhci_mem_t* mem_phys = ctrl->mem_phys;
    xhci_trb_t* ep0 = mem->slots[ctrl->kbd_slot_idx].ep0_ring;

    
    mem->led_report[0] = led_bits;

    int idx = ctrl->ep0_enq;
    int pcs = ctrl->ep0_pcs;

    
    
    
    uint32_t setup_lo = 0x02000921;  
    uint32_t setup_hi = (uint32_t)ctrl->kbd_iface_num | (1 << 16); 

    
    ep0[idx].param[0] = setup_lo;
    ep0[idx].param[1] = setup_hi;
    ep0[idx].param[2] = 8;
    ep0[idx].param[3] = (2 << 16) | (2 << 10) | (1 << 6) | (pcs & 1);
    idx++;

    
    uint64_t buf_phys = v2p(mem, mem_phys, mem->led_report);
    ep0[idx].param[0] = (uint32_t)(buf_phys & 0xFFFFFFFF);
    ep0[idx].param[1] = (uint32_t)(buf_phys >> 32);
    ep0[idx].param[2] = 1;
    ep0[idx].param[3] = (0 << 16) | (3 << 10) | (pcs & 1);
    idx++;

    
    ep0[idx].param[0] = 0;
    ep0[idx].param[1] = 0;
    ep0[idx].param[2] = 0;
    ep0[idx].param[3] = (1 << 16) | (4 << 10) | (1 << 5) | (pcs & 1);
    idx++;

    ctrl->ep0_enq = idx;
    
    ctrl->db_regs[ctrl->kbd_slot_id] = 1;
}

void xhci_poll(void) {
    for (int c = 0; c < xhci_ctrl_count; c++) {
        xhci_controller_state_t* ctrl = &xhci_ctrls[c];
        if (!ctrl->has_kbd) continue;

        xhci_mem_t* mem = ctrl->mem_virt;
        xhci_mem_t* mem_phys = ctrl->mem_phys;
        volatile uint32_t* ir0_regs = ctrl->ir0_regs;

        
        if (ctrl->poll_count < 3 || (ctrl->poll_count % 5000) == 0) {
            xhci_trb_t* ev_peek = &mem->event_ring[ctrl->ev_deq];
            uint32_t ev_dw3 = ev_peek->param[3];
            






            
            
            int dci = ctrl->kbd_ep_dci;
            int sidx = ctrl->kbd_slot_idx;
            xhci_device_context_t* dctx = &mem->slots[sidx].dev_ctx;
            uint32_t ep_dw0 = dctx->ep[dci - 1].param[0];
            uint32_t ep_dw1 = dctx->ep[dci - 1].param[1];
            uint32_t ep_dw2 = dctx->ep[dci - 1].param[2];
            uint32_t ep_dw3 = dctx->ep[dci - 1].param[3];
            
            
            
            uint32_t slot_dw0 = dctx->slot.param[0];
            uint32_t slot_dw3 = dctx->slot.param[3];
            
            
            
            
            
            
            
            
        }

        
        
        uint32_t iman = ir0_regs[0];
        if (iman & 1) {
            ir0_regs[0] = iman | 1; 
        }

        
        uint32_t usbsts = ctrl->op_regs[1];
        if (usbsts & (1 << 3)) {
            ctrl->op_regs[1] = (1 << 3); 
        }

        
        int events_processed = 0;
        while (events_processed < 16) { 
            xhci_trb_t* ev = &mem->event_ring[ctrl->ev_deq];
            if ((ev->param[3] & 1) != ctrl->ev_ccs) {
                break; 
            }

            events_processed++;
            int trb_type = (ev->param[3] >> 10) & 0x3F;
            
            
            if (ctrl->poll_count < 50) {
                
                
            }

            if (trb_type == 32) { 
                int comp_code = (ev->param[2] >> 24) & 0xFF;
                int ep_id = (ev->param[3] >> 16) & 0x1F;
                
                
                if (ctrl->poll_count < 50 || (ctrl->poll_count % 1000) == 0) {
                    LOG_INFO("xHCI poll: Transfer Event ep=%d cc=%d (expected ep=%d) poll#%d", 
                             ep_id, comp_code, ctrl->kbd_ep_dci, ctrl->poll_count);
                }
                
                if (ep_id == ctrl->kbd_ep_dci && (comp_code == 1 || comp_code == 13)) {
                    
                    
                    
                    
                    
                    
                    
                    
                    
                    usb_kbd_handle_report(mem->kbd_report);

                    
                    
                    
                    
                    uint8_t new_leds = input_get_kbd_leds();
                    if (new_leds != ctrl->kbd_leds) {
                        ctrl->kbd_leds = new_leds;
                        xhci_set_kbd_leds(ctrl, new_leds);
                    }

                    
                    int idx = ctrl->int_in_enq;
                    uint64_t buf_phys = v2p(mem, mem_phys, mem->kbd_report);
                    mem->int_in_ring[idx].param[0] = (uint32_t)(buf_phys & 0xFFFFFFFF);
                    mem->int_in_ring[idx].param[1] = (uint32_t)(buf_phys >> 32);
                    mem->int_in_ring[idx].param[2] = 8;  
                    mem->int_in_ring[idx].param[3] = (1 << 10) | (1 << 5) | (ctrl->int_in_pcs & 1);
                    

                    ctrl->int_in_enq++;
                    if (ctrl->int_in_enq >= 255) {
                        
                        ctrl->int_in_enq = 0;
                        ctrl->int_in_pcs ^= 1;
                        
                        uint64_t ring_phys = v2p(mem, mem_phys, &mem->int_in_ring[0]);
                        mem->int_in_ring[255].param[0] = (uint32_t)(ring_phys & 0xFFFFFFFF);
                        mem->int_in_ring[255].param[1] = (uint32_t)(ring_phys >> 32);
                        mem->int_in_ring[255].param[2] = 0;
                        mem->int_in_ring[255].param[3] = (6 << 10) | (1 << 1) | (ctrl->int_in_pcs & 1);
                        
                    }

                    
                    ctrl->db_regs[ctrl->kbd_slot_id] = ctrl->kbd_ep_dci;
                } else if (ep_id == 1 && (comp_code == 1 || comp_code == 13)) {
                    
                } else if (comp_code != 1 && comp_code != 13) {
                    
                    if (ctrl->poll_count < 5) {
                        LOG_WARN("xHCI poll: Transfer Event error, ep=%d cc=%d", ep_id, comp_code);
                    }
                }

                xhci_advance_event(mem, mem_phys, ir0_regs, &ctrl->ev_ccs, &ctrl->ev_deq);
            } else if (trb_type == 33) {
                
                xhci_advance_event(mem, mem_phys, ir0_regs, &ctrl->ev_ccs, &ctrl->ev_deq);
            } else if (trb_type == 34) {
                
                xhci_advance_event(mem, mem_phys, ir0_regs, &ctrl->ev_ccs, &ctrl->ev_deq);
            } else {
                
                xhci_advance_event(mem, mem_phys, ir0_regs, &ctrl->ev_ccs, &ctrl->ev_deq);
            }
        }

        ctrl->poll_count++;
    }
}




static int xhci_control_transfer(xhci_mem_t* mem, xhci_mem_t* mem_phys,
                                  volatile uint32_t* ir0_regs, volatile uint32_t* db_regs,
                                  int* ev_ccs, int* ev_deq,
                                  int slot_id, int* ep0_enq, int* ep0_pcs,
                                  uint32_t setup_lo, uint32_t setup_hi,
                                  void* data_buf, uint16_t data_len, int dir) {
    int slot_idx = slot_id - 1;
    xhci_trb_t* ep0 = mem->slots[slot_idx].ep0_ring;
    int idx = *ep0_enq;

    
    
    int trt = (dir == 2) ? 0 : (dir == 1 ? 3 : 2);
    ep0[idx].param[0] = setup_lo;
    ep0[idx].param[1] = setup_hi;
    ep0[idx].param[2] = 8;
    ep0[idx].param[3] = (trt << 16) | (2 << 10) | (1 << 6) | (*ep0_pcs & 1);
    idx++;

    
    if (dir != 2 && data_len > 0) {
        uint64_t buf_phys = v2p(mem, mem_phys, data_buf);
        ep0[idx].param[0] = (uint32_t)(buf_phys & 0xFFFFFFFF);
        ep0[idx].param[1] = (uint32_t)(buf_phys >> 32);
        ep0[idx].param[2] = data_len;
        ep0[idx].param[3] = (dir << 16) | (3 << 10) | (*ep0_pcs & 1);
        idx++;
    }

    
    
    int status_dir = (dir == 1) ? 0 : 1;
    if (dir == 2) status_dir = 1;
    ep0[idx].param[0] = 0;
    ep0[idx].param[1] = 0;
    ep0[idx].param[2] = 0;
    ep0[idx].param[3] = (status_dir << 16) | (4 << 10) | (1 << 5) | (*ep0_pcs & 1);
    idx++;

    *ep0_enq = idx;

    
    db_regs[slot_id] = 1;

    
    xhci_trb_t* ev = xhci_wait_for_event(mem, mem_phys, ir0_regs, ev_ccs, ev_deq, 32);
    if (!ev) return -1;
    int cc = (ev->param[2] >> 24) & 0xFF;
    xhci_advance_event(mem, mem_phys, ir0_regs, ev_ccs, ev_deq);
    return cc;
}

void xhci_init(pcie_device_info_t* dev) {
    uint32_t bar0 = pcie_read_bar(dev, 0);
    uint32_t bar1 = pcie_read_bar(dev, 1);
    
    
    uint64_t mmio_phys = bar0 & 0xFFFFFFF0;
    if ((bar0 & 0x06) == 0x04) {
        mmio_phys |= ((uint64_t)bar1 << 32);
    }

    if (!mmio_phys) {
        LOG_WARN("xHCI: Invalid BAR");
        return;
    }

    if (xhci_ctrl_count >= XHCI_MAX_CONTROLLERS) {
        LOG_WARN("xHCI: Max controllers reached");
        return;
    }

    
    
    volatile uint16_t* pci_cmd = (volatile uint16_t*)(dev->ecam_base + 0x04);
    uint16_t cmd_val = *pci_cmd;
    LOG_INFO("xHCI: PCI Command Register = 0x%04x", cmd_val);
    *pci_cmd = cmd_val | 0x06; 

    volatile uint32_t* cap_regs = (volatile uint32_t*)phys_to_virt(mmio_phys);

    LOG_INFO("xHCI: BAR MMIO phys=0x%llx virt=0x%llx", mmio_phys, (uint64_t)cap_regs);

    
    uint32_t caplen_version = cap_regs[0];
    if (caplen_version == 0xFFFFFFFF || caplen_version == 0) {
        LOG_WARN("xHCI: MMIO read returned 0x%08x — device not accessible", caplen_version);
        return;
    }

    
    uint8_t caplength = caplen_version & 0xFF;
    uint16_t hciversion = (caplen_version >> 16) & 0xFFFF;

    LOG_INFO("xHCI Controller Version: %x.%02x, Cap Length: %d", 
             hciversion >> 8, hciversion & 0xFF, caplength);

    uint32_t hcsparams1 = cap_regs[1];
    uint8_t max_slots = hcsparams1 & 0xFF;
    uint16_t max_intrs = (hcsparams1 >> 8) & 0x7FF;
    uint8_t max_ports = (hcsparams1 >> 24) & 0xFF;

    LOG_INFO("xHCI Structural Info: Max Slots: %d, Max Intrs: %d, Max Ports: %d", 
             max_slots, max_intrs, max_ports);

    uint32_t hccparams1 = cap_regs[4];
    int csz = (hccparams1 >> 2) & 1;
    LOG_INFO("xHCI HCCPARAMS1: 0x%08x, CSZ=%d (%d-byte contexts)", hccparams1, csz, csz ? 64 : 32);
    if (csz != 0) {
        LOG_WARN("xHCI: 64-byte contexts not supported by this driver");
        return;
    }

    int usb_controller_index = 0;

    if (usb_controller_count < MAX_USB_CONTROLLERS) {
        usb_controller_index = usb_controller_count++;
        usb_controller_info_t* ctrl = &usb_controllers[usb_controller_index];
        ctrl->type = USB_CTRL_xHCI;
        ctrl->pcie_bus = dev->bus;
        ctrl->pcie_dev = dev->dev;
        ctrl->pcie_func = dev->func;
        ctrl->version_major = hciversion >> 8;
        ctrl->version_minor = hciversion & 0xFF;
        ctrl->num_ports = max_ports;
    } else {
        LOG_WARN("xHCI: Maximum USB controllers reached, additional controller ignored");
        return;
    }

    
    
    uint16_t xecp_off = (hccparams1 >> 16) & 0xFFFF;
    if (xecp_off != 0) {
        volatile uint32_t* xcap = (volatile uint32_t*)((uint8_t*)cap_regs + (xecp_off << 2));
        
        while (1) {
            uint32_t xcap_val = *xcap;
            uint8_t xcap_id = xcap_val & 0xFF;
            uint8_t next_off = (xcap_val >> 8) & 0xFF; 

            if (xcap_id == 1) {
                
                
                
                LOG_INFO("xHCI: Found USB Legacy Support at offset 0x%x, val=0x%08x",
                         (int)((uint8_t*)xcap - (uint8_t*)cap_regs), xcap_val);

                
                *xcap = xcap_val | (1 << 24);

                
                int handoff_timeout = 5000;
                while ((*xcap & (1 << 16)) && handoff_timeout > 0) {
                    dummy_mdelay(1);
                    handoff_timeout--;
                }
                if (handoff_timeout == 0) {
                    LOG_WARN("xHCI: BIOS handoff timed out, forcing...");
                    
                    *xcap = (*xcap & ~(1u << 16)) | (1u << 24);
                }
                LOG_INFO("xHCI: BIOS handoff complete (Legacy Support = 0x%08x)", *xcap);

                
                volatile uint32_t* usblegsup_ctrl = xcap + 1;
                *usblegsup_ctrl = 0; 
                LOG_INFO("xHCI: Legacy SMI disabled (ctrl=0x%08x)", *usblegsup_ctrl);
                break;
            }

            if (next_off == 0) break; 
            xcap = (volatile uint32_t*)((uint8_t*)xcap + (next_off << 2));
        }
    }

    
    
    uint8_t port_proto[256];
    memset(port_proto, 0, sizeof(port_proto));
    if (xecp_off != 0) {
        volatile uint32_t* xcap = (volatile uint32_t*)((uint8_t*)cap_regs + (xecp_off << 2));
        while (1) {
            uint32_t xcap_val = *xcap;
            uint8_t xcap_id = xcap_val & 0xFF;
            uint8_t next_off = (xcap_val >> 8) & 0xFF;

            if (xcap_id == 2) {
                
                
                
                uint8_t rev_major = (xcap_val >> 24) & 0xFF;
                
                uint32_t dw2 = xcap[2];
                uint8_t port_offset = dw2 & 0xFF;         
                uint8_t port_count = (dw2 >> 8) & 0xFF;
                uint8_t proto = (rev_major >= 3) ? 3 : 2;
                LOG_INFO("xHCI: Supported Protocol USB%d, ports %d-%d",
                         proto, port_offset, port_offset + port_count - 1);
                for (int p = 0; p < port_count && (port_offset + p - 1) < 256; p++) {
                    port_proto[port_offset + p - 1] = proto; 
                }
            }

            if (next_off == 0) break;
            xcap = (volatile uint32_t*)((uint8_t*)xcap + (next_off << 2));
        }
    }

    volatile uint32_t* op_regs = (volatile uint32_t*)((uint8_t*)cap_regs + caplength);

    uint32_t rtsoff = cap_regs[6] & ~0x1F; 
    volatile uint32_t* rt_regs = (volatile uint32_t*)((uint8_t*)cap_regs + rtsoff);

    LOG_INFO("xHCI: Stopping controller...");

    
    op_regs[0] &= ~1u;
    { int t = 1000; while (!(op_regs[1] & 1) && t > 0) { dummy_mdelay(1); t--; }
      if (t == 0) { LOG_WARN("xHCI: HC stop timed out"); } }

    
    op_regs[0] |= 2;
    { int t = 1000; while ((op_regs[0] & 2) && t > 0) { dummy_mdelay(1); t--; }
      if (t == 0) { LOG_WARN("xHCI: HC reset timed out"); return; } }
    { int t = 1000; while ((op_regs[1] & (1 << 11)) && t > 0) { dummy_mdelay(1); t--; }
      if (t == 0) { LOG_WARN("xHCI: HC CNR timed out"); return; } }

    LOG_INFO("xHCI: Controller reset complete");

    
    
    int pages_needed = (sizeof(xhci_mem_t) + 4095) / 4096;
    LOG_INFO("xHCI: Allocating %d pages (%d bytes) for driver structures", pages_needed, (int)sizeof(xhci_mem_t));
    void* p_mem = pmm_alloc(pages_needed);
    if (!p_mem) {
        LOG_ERROR("xHCI: Failed to allocate physical memory");
        return;
    }
    xhci_mem_t* mem_phys = (xhci_mem_t*)p_mem;
    xhci_mem_t* mem_virt = (xhci_mem_t*)phys_to_virt((uint64_t)mem_phys);
    memset(mem_virt, 0, pages_needed * 4096);

    op_regs[14] = max_slots;

    uint64_t dcbaap_phys = (uint64_t)mem_phys->dcbaa;
    op_regs[12] = (uint32_t)(dcbaap_phys & 0xFFFFFFFF);
    op_regs[13] = (uint32_t)(dcbaap_phys >> 32);

    uint64_t crcr_phys = (uint64_t)mem_phys->cmd_ring;
    crcr_phys |= 1;
    op_regs[6] = (uint32_t)(crcr_phys & 0xFFFFFFFF);
    op_regs[7] = (uint32_t)(crcr_phys >> 32);

    mem_virt->erst[0].base = (uint64_t)mem_phys->event_ring;
    mem_virt->erst[0].size = 256;
    
    volatile uint32_t* ir0_regs = rt_regs + 8; 

    ir0_regs[2] = 1;

    uint64_t erdp_phys = (uint64_t)mem_phys->event_ring;
    erdp_phys |= 8;
    ir0_regs[6] = (uint32_t)(erdp_phys & 0xFFFFFFFF);
    ir0_regs[7] = (uint32_t)(erdp_phys >> 32);
    
    uint64_t erst_phys = (uint64_t)mem_phys->erst;
    ir0_regs[4] = (uint32_t)(erst_phys & 0xFFFFFFFF);
    ir0_regs[5] = (uint32_t)(erst_phys >> 32);

    ir0_regs[0] |= 2;
    op_regs[0] |= 4;

    int cmd_enq = 0;
    int cmd_pcs = 1;
    int ev_deq = 0;
    int ev_ccs = 1;

    
    op_regs[0] |= 1;
    while (op_regs[1] & 1) { dummy_mdelay(1); }

    LOG_INFO("xHCI: Controller started. Checking port power...");

    
    
    int pp_forced = 0;
    for (int i = 0; i < max_ports; i++) {
        volatile uint32_t* portsc = (volatile uint32_t*)((uint8_t*)op_regs + 0x400 + (i * 0x10));
        uint32_t ps = *portsc;
        if (!(ps & (1 << 9))) {
            
            *portsc = (ps & 0x0E00C200) | (1 << 9);
            pp_forced++;
            LOG_INFO("xHCI Port %d: PP was off, forced on", i + 1);
        }
    }
    if (pp_forced > 0) {
        LOG_INFO("xHCI: Forced PP on %d ports, waiting for power stabilization...", pp_forced);
        dummy_mdelay(500); 
    } else {
        
        dummy_mdelay(200);
    }

    uint32_t dboff = cap_regs[5] & ~0x3;
    volatile uint32_t* db_regs = (volatile uint32_t*)((uint8_t*)cap_regs + dboff);

    
    int ctrl_idx = xhci_ctrl_count++;
    xhci_controller_state_t* ctrl = &xhci_ctrls[ctrl_idx];
    ctrl->mem_virt = mem_virt;
    ctrl->mem_phys = mem_phys;
    ctrl->ir0_regs = ir0_regs;
    ctrl->db_regs = db_regs;
    ctrl->op_regs = op_regs;
    ctrl->ev_ccs = ev_ccs;
    ctrl->ev_deq = ev_deq;
    ctrl->has_kbd = 0;
    ctrl->poll_count = 0;

    
    LOG_INFO("xHCI: Pre-enum check: USBCMD=0x%08x USBSTS=0x%08x", op_regs[0], op_regs[1]);
    if (!(op_regs[0] & 1) || (op_regs[1] & 1)) {
        LOG_WARN("xHCI: HC not running! USBCMD.RS=%d USBSTS.HCH=%d", op_regs[0] & 1, (op_regs[1] >> 0) & 1);
    }

    
    
    
    #define PORTSC_RW_MASK 0x0E00C200u

    int connected_devices = 0;
    for (int i = 0; i < max_ports; i++) {
        volatile uint32_t* portsc = (volatile uint32_t*)((uint8_t*)op_regs + 0x400 + (i * 0x10));
        uint32_t ps = *portsc;

        
        LOG_INFO("xHCI Port %d: PORTSC=0x%08x (CCS=%d PED=%d Speed=%d PLS=%d PP=%d PR=%d)",
                 i + 1, ps, ps & 1, (ps >> 1) & 1, (ps >> 10) & 0xF, (ps >> 5) & 0xF,
                 (ps >> 9) & 1, (ps >> 4) & 1);

        
        uint32_t change_bits = ps & 0x00FE0000; 
        if (change_bits) {
            *portsc = (ps & PORTSC_RW_MASK) | change_bits;
            dummy_mdelay(5);
            ps = *portsc; 
        }

        if (ps & 1) { 
            connected_devices++;
            
            uint32_t speed = (ps >> 10) & 0xF;
            uint32_t pls = (ps >> 5) & 0xF;
            uint8_t proto = port_proto[i]; 
            LOG_INFO("xHCI Port %d: Device connected (Speed=%d, PLS=%d, PED=%d, Proto=USB%d, PR=%d)",
                     i + 1, speed, pls, (ps >> 1) & 1, proto, (ps >> 4) & 1);

            
            if (ps & (1 << 1)) {
                LOG_INFO("xHCI Port %d: Already enabled, skipping reset", i + 1);
                speed = (ps >> 10) & 0xF;
                goto port_ready;
            }

            
            if (ps & (1 << 4)) {
                LOG_INFO("xHCI Port %d: PR already set, waiting for reset to complete...", i + 1);
                
                int pr_timeout = 2000;
                while ((*portsc & (1 << 4)) && pr_timeout > 0) {
                    dummy_mdelay(1);
                    pr_timeout--;
                }
                
                ps = *portsc;
                LOG_INFO("xHCI Port %d: After PR wait: PORTSC=0x%08x (PR=%d PED=%d PLS=%d) timeout_left=%d",
                         i + 1, ps, (ps >> 4) & 1, (ps >> 1) & 1, (ps >> 5) & 0xF, pr_timeout);
                
                
                uint32_t cb = ps & 0x00FE0000;
                if (cb) {
                    *portsc = (ps & PORTSC_RW_MASK) | cb;
                    dummy_mdelay(10);
                    ps = *portsc;
                }
                
                if ((ps & 1) && (ps & (1 << 1))) {
                    speed = (ps >> 10) & 0xF;
                    LOG_INFO("xHCI Port %d: Auto-reset completed, enabled, Speed=%d", i + 1, speed);
                    goto port_ready;
                }
                
                if (ps & (1 << 4)) {
                    LOG_WARN("xHCI Port %d: PR stuck after 2s wait (PORTSC=0x%08x), skipping", i + 1, ps);
                    continue;
                }
                
                if (!(ps & 1)) {
                    LOG_INFO("xHCI Port %d: Device disconnected after reset wait", i + 1);
                    continue;
                }
                
                speed = (ps >> 10) & 0xF;
                pls = (ps >> 5) & 0xF;
                LOG_INFO("xHCI Port %d: PR cleared but not enabled, will try our own reset", i + 1);
            }

            int reset_ok = 0;
            
            if (proto == 3) {
                
                LOG_INFO("xHCI Port %d: USB3 port — trying Warm Reset", i + 1);

                
                *portsc = (ps & PORTSC_RW_MASK) | (5 << 5) | (1 << 16);
                dummy_mdelay(100);

                ps = *portsc;
                pls = (ps >> 5) & 0xF;
                LOG_INFO("xHCI Port %d: After RxDetect: PORTSC=0x%08x PLS=%d CCS=%d",
                         i + 1, ps, pls, ps & 1);

                
                ps = *portsc;
                *portsc = (ps & PORTSC_RW_MASK) | (1u << 31);

                int reset_timeout = 1000;
                while ((*portsc & (1u << 31)) && reset_timeout > 0) {
                    dummy_mdelay(1);
                    reset_timeout--;
                }

                ps = *portsc;
                uint32_t clear_bits = ps & 0x00FE0000;
                if (clear_bits) {
                    *portsc = (ps & PORTSC_RW_MASK) | clear_bits;
                    dummy_mdelay(10);
                }

                ps = *portsc;
                speed = (ps >> 10) & 0xF;
                if (ps & (1 << 1)) {
                    LOG_INFO("xHCI Port %d: Warm Reset OK, enabled, Speed=%d", i + 1, speed);
                    reset_ok = 1;
                } else {
                    LOG_WARN("xHCI Port %d: Warm Reset failed (PORTSC=0x%08x), trying normal reset...", i + 1, ps);
                }
            }
            
            if (!reset_ok) {
                
                
                
                LOG_INFO("xHCI Port %d: Trying normal Port Reset (proto=%d)", i + 1, proto);
                
                ps = *portsc;
                LOG_INFO("xHCI Port %d: Before PR write: PORTSC=0x%08x", i + 1, ps);
                
                
                *portsc = (1 << 9) | (1 << 4);
                
                
                dummy_mdelay(1);
                uint32_t readback = *portsc;
                LOG_INFO("xHCI Port %d: After PR write: PORTSC=0x%08x (PR=%d)", i + 1, readback, (readback >> 4) & 1);
                
                
                int reset_timeout = 2000; 
                while ((*portsc & (1 << 4)) && reset_timeout > 0) {
                    dummy_mdelay(1);
                    reset_timeout--;
                }
                
                ps = *portsc;
                LOG_INFO("xHCI Port %d: After reset wait: PORTSC=0x%08x (PR=%d PED=%d) timeout_left=%d",
                         i + 1, ps, (ps >> 4) & 1, (ps >> 1) & 1, reset_timeout);

                
                uint32_t clear_bits = ps & 0x00FE0000;
                if (clear_bits) {
                    *portsc = (ps & PORTSC_RW_MASK) | clear_bits;
                    dummy_mdelay(10);
                }

                ps = *portsc;
                speed = (ps >> 10) & 0xF;
                if (ps & (1 << 1)) {
                    LOG_INFO("xHCI Port %d: Normal Port Reset OK, enabled, Speed=%d", i + 1, speed);
                    reset_ok = 1;
                } else {
                    LOG_WARN("xHCI Port %d: Normal Port Reset failed (PORTSC=0x%08x)", i + 1, ps);
                }
            }

            if (!reset_ok) {
                LOG_WARN("xHCI Port %d: All reset attempts failed (PORTSC=0x%08x), skipping", i + 1, ps);
                continue;
            }

            port_ready: ;

            
            memset(&mem_virt->in_ctx, 0, sizeof(mem_virt->in_ctx));
            memset(mem_virt->desc_buf, 0, sizeof(mem_virt->desc_buf));

            xhci_trb_t trb = { .param = {0, 0, 0, (9 << 10)} };
            xhci_enqueue_cmd(mem_virt, db_regs, &cmd_enq, &cmd_pcs, trb);
            xhci_trb_t* ev = xhci_wait_for_event(mem_virt, mem_phys, ir0_regs, &ev_ccs, &ev_deq, 33);
            if (!ev) {
                LOG_WARN("xHCI Port %d: Timeout waiting for Enable Slot", i + 1);
                continue;
            }
            int enable_cc = (ev->param[2] >> 24) & 0xFF;
            int slot_id = (ev->param[3] >> 24) & 0xFF;
            xhci_advance_event(mem_virt, mem_phys, ir0_regs, &ev_ccs, &ev_deq);
            if (enable_cc != 1 || slot_id == 0) {
                LOG_WARN("xHCI Port %d: Enable Slot failed, cc=%d, slot=%d", i + 1, enable_cc, slot_id);
                continue;
            }
            LOG_INFO("xHCI Slot ID assigned: %d", slot_id);

            
            int slot_idx = slot_id - 1;
            if (slot_idx < 0 || slot_idx >= XHCI_MAX_SLOTS) {
                LOG_WARN("xHCI Port %d: slot_id %d exceeds XHCI_MAX_SLOTS (%d), skipping", i + 1, slot_id, XHCI_MAX_SLOTS);
                continue;
            }

            
            xhci_slot_data_t* slot_data = &mem_virt->slots[slot_idx];
            memset(&slot_data->dev_ctx, 0, sizeof(slot_data->dev_ctx));
            memset(slot_data->ep0_ring, 0, sizeof(slot_data->ep0_ring));

            
            uint64_t dev_ctx_phys = v2p(mem_virt, mem_phys, &slot_data->dev_ctx);
            mem_virt->dcbaa[slot_id] = dev_ctx_phys;

            mem_virt->in_ctx.control.param[0] = 0;
            mem_virt->in_ctx.control.param[1] = 3;  
            
            mem_virt->in_ctx.slot.param[0] = (1 << 27) | (speed << 20);
            mem_virt->in_ctx.slot.param[1] = ((i+1) << 16);

            uint32_t max_packet_size = 8;
            if (speed == 4) max_packet_size = 512;
            else if (speed == 3) max_packet_size = 64;
            else if (speed == 2) max_packet_size = 64;

            mem_virt->in_ctx.ep[0].param[1] = (3 << 1) | (4 << 3) | (max_packet_size << 16);
            uint64_t ep0_ring_phys = v2p(mem_virt, mem_phys, &slot_data->ep0_ring[0]);
            mem_virt->in_ctx.ep[0].param[2] = (ep0_ring_phys & 0xFFFFFFF0) | 1;
            mem_virt->in_ctx.ep[0].param[3] = (ep0_ring_phys >> 32);
            mem_virt->in_ctx.ep[0].param[4] = 8;
            
            trb.param[0] = v2p(mem_virt, mem_phys, &mem_virt->in_ctx) & 0xFFFFFFFF;
            trb.param[1] = v2p(mem_virt, mem_phys, &mem_virt->in_ctx) >> 32;
            trb.param[2] = 0;
            trb.param[3] = (slot_id << 24) | (11 << 10);
            xhci_enqueue_cmd(mem_virt, db_regs, &cmd_enq, &cmd_pcs, trb);
            ev = xhci_wait_for_event(mem_virt, mem_phys, ir0_regs, &ev_ccs, &ev_deq, 33);
            if (!ev) {
                LOG_WARN("xHCI Port %d: Timeout waiting for Address Device", i + 1);
                continue;
            }
            int addr_cc = (ev->param[2] >> 24) & 0xFF;
            xhci_advance_event(mem_virt, mem_phys, ir0_regs, &ev_ccs, &ev_deq);
            if (addr_cc != 1) {
                LOG_WARN("xHCI Port %d: Address Device failed, completion code: %d", i + 1, addr_cc);
                continue;
            }
            LOG_INFO("xHCI Device %d Addressed", slot_id);
            
            
            if (speed <= 2) {
                dummy_mdelay(50);
            } else {
                dummy_mdelay(10);
            }
            
            
            int ep0_enq = 0;
            int ep0_pcs = 1;
            int cc;

            if (speed <= 2) {
                
                
                cc = xhci_control_transfer(mem_virt, mem_phys, ir0_regs, db_regs,
                    &ev_ccs, &ev_deq, slot_id, &ep0_enq, &ep0_pcs,
                    0x01000680, 0x00080000,   
                    mem_virt->desc_buf, 8, 1);
                
                if (cc != 1 && cc != 13) { 
                    LOG_WARN("xHCI Port %d: GET_DESCRIPTOR (8-byte) failed, cc=%d", i + 1, cc);
                    continue;
                }

                uint8_t real_max_pkt = mem_virt->desc_buf[7]; 
                LOG_INFO("xHCI Device %d: bMaxPacketSize0=%d (current=%d)", slot_id, real_max_pkt, max_packet_size);

                if (real_max_pkt > 0 && real_max_pkt != max_packet_size) {
                    
                    memset(&mem_virt->in_ctx, 0, sizeof(mem_virt->in_ctx));
                    mem_virt->in_ctx.control.param[1] = (1 << 1); 
                    
                    mem_virt->in_ctx.ep[0].param[1] = (3 << 1) | (4 << 3) | ((uint32_t)real_max_pkt << 16);

                    uint64_t in_ctx_phys2 = v2p(mem_virt, mem_phys, &mem_virt->in_ctx);
                    xhci_trb_t eval_trb = { .param = {
                        (uint32_t)(in_ctx_phys2 & 0xFFFFFFFF),
                        (uint32_t)(in_ctx_phys2 >> 32),
                        0,
                        (slot_id << 24) | (13 << 10) 
                    }};
                    xhci_enqueue_cmd(mem_virt, db_regs, &cmd_enq, &cmd_pcs, eval_trb);
                    ev = xhci_wait_for_event(mem_virt, mem_phys, ir0_regs, &ev_ccs, &ev_deq, 33);
                    if (ev) {
                        int eval_cc = (ev->param[2] >> 24) & 0xFF;
                        xhci_advance_event(mem_virt, mem_phys, ir0_regs, &ev_ccs, &ev_deq);
                        if (eval_cc == 1) {
                            LOG_INFO("xHCI Device %d: Evaluate Context OK, EP0 MaxPacket updated to %d",
                                     slot_id, real_max_pkt);
                            max_packet_size = real_max_pkt;
                        } else {
                            LOG_WARN("xHCI Device %d: Evaluate Context failed, cc=%d", slot_id, eval_cc);
                        }
                    }
                    dummy_mdelay(10);
                }

                
                cc = xhci_control_transfer(mem_virt, mem_phys, ir0_regs, db_regs,
                    &ev_ccs, &ev_deq, slot_id, &ep0_enq, &ep0_pcs,
                    0x01000680, 0x00120000,   
                    mem_virt->desc_buf, 18, 1);
                
                if (cc != 1 && cc != 13) {
                    LOG_WARN("xHCI Port %d: GET_DESCRIPTOR (Device 18-byte) failed, cc=%d", i + 1, cc);
                    continue;
                }
            } else {
                
                cc = xhci_control_transfer(mem_virt, mem_phys, ir0_regs, db_regs,
                    &ev_ccs, &ev_deq, slot_id, &ep0_enq, &ep0_pcs,
                    0x01000680, 0x00120000,   
                    mem_virt->desc_buf, 18, 1);
                
                if (cc != 1) {
                    LOG_WARN("xHCI Port %d: GET_DESCRIPTOR (Device) failed, cc=%d", i + 1, cc);
                    continue;
                }
            }

            struct usb_dev* new_dev = usb_alloc_dev();
            if (new_dev) {
                new_dev->address = addr_cc;
                new_dev->port_number = i + 1;
                new_dev->speed = speed;
                new_dev->connected = 1;
                
                new_dev->controller = &usb_controllers[usb_controller_index];
            }

            uint16_t vid = *(uint16_t*)(&mem_virt->desc_buf[8]);
            uint16_t pid = *(uint16_t*)(&mem_virt->desc_buf[10]);
            uint8_t class_id = mem_virt->desc_buf[4];
            uint8_t subclass_id = mem_virt->desc_buf[5];
            uint8_t protocol = mem_virt->desc_buf[6];
            uint8_t num_configs = mem_virt->desc_buf[17];

            usb_device_descriptor_t* desc = (usb_device_descriptor_t*)mem_virt->desc_buf;
            new_dev->desc = *desc;
            new_dev->desc_fetched = true;
            
            LOG_INFO("xHCI Device %d: VID=0x%04x PID=0x%04x Class=%d SubClass=%d Proto=%d Configs=%d",
                     slot_id, vid, pid, class_id, subclass_id, protocol, num_configs);

            vfs_node_t* dev_node = vfs_alloc_node();
            if (dev_node) {
                dev_node->type = VFS_CHAR_DEVICE;
                dev_node->ops = NULL;
                dev_node->size = 0;
                dev_node->device = new_dev;

                char path_buffer[64];
                snprintf(path_buffer, sizeof(path_buffer), "usb/xhci/%d", slot_id);

                devfs_register_device(path_buffer, dev_node);
                LOG_INFO("Registered USB device at /dev/%s", path_buffer);
            }

            
            
            memset(mem_virt->cfg_desc_buf, 0, sizeof(mem_virt->cfg_desc_buf));
            
            cc = xhci_control_transfer(mem_virt, mem_phys, ir0_regs, db_regs,
                &ev_ccs, &ev_deq, slot_id, &ep0_enq, &ep0_pcs,
                0x02000680, 0x00090000,   
                mem_virt->cfg_desc_buf, 9, 1);
            
            if (cc != 1) {
                LOG_WARN("xHCI Device %d: GET_DESCRIPTOR (Config short) failed, cc=%d", slot_id, cc);
                continue;
            }

            uint16_t total_len = *(uint16_t*)(&mem_virt->cfg_desc_buf[2]);
            if (total_len > sizeof(mem_virt->cfg_desc_buf)) total_len = sizeof(mem_virt->cfg_desc_buf);
            uint8_t config_val = mem_virt->cfg_desc_buf[5];

            LOG_INFO("xHCI Device %d: Config Descriptor wTotalLength=%d bConfigurationValue=%d",
                     slot_id, total_len, config_val);

            
            memset(mem_virt->cfg_desc_buf, 0, sizeof(mem_virt->cfg_desc_buf));
            
            uint32_t cfg_setup_hi = ((uint32_t)total_len << 16);
            cc = xhci_control_transfer(mem_virt, mem_phys, ir0_regs, db_regs,
                &ev_ccs, &ev_deq, slot_id, &ep0_enq, &ep0_pcs,
                0x02000680, cfg_setup_hi,
                mem_virt->cfg_desc_buf, total_len, 1);
            
            if (cc != 1 && cc != 13) { 
                LOG_WARN("xHCI Device %d: GET_DESCRIPTOR (Config full) failed, cc=%d", slot_id, cc);
                continue;
            }

            
            
            
            
            
            
            LOG_INFO("xHCI Device %d: Config desc dump (first 64 of %d bytes):", slot_id, total_len);
            for (int d = 0; d < 64 && d < total_len; d += 16) {
                int remain = total_len - d;
                if (remain > 16) remain = 16;
                LOG_INFO("  [%02x]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                    d,
                    d+0<total_len ? mem_virt->cfg_desc_buf[d+0] : 0,
                    d+1<total_len ? mem_virt->cfg_desc_buf[d+1] : 0,
                    d+2<total_len ? mem_virt->cfg_desc_buf[d+2] : 0,
                    d+3<total_len ? mem_virt->cfg_desc_buf[d+3] : 0,
                    d+4<total_len ? mem_virt->cfg_desc_buf[d+4] : 0,
                    d+5<total_len ? mem_virt->cfg_desc_buf[d+5] : 0,
                    d+6<total_len ? mem_virt->cfg_desc_buf[d+6] : 0,
                    d+7<total_len ? mem_virt->cfg_desc_buf[d+7] : 0,
                    d+8<total_len ? mem_virt->cfg_desc_buf[d+8] : 0,
                    d+9<total_len ? mem_virt->cfg_desc_buf[d+9] : 0,
                    d+10<total_len ? mem_virt->cfg_desc_buf[d+10] : 0,
                    d+11<total_len ? mem_virt->cfg_desc_buf[d+11] : 0,
                    d+12<total_len ? mem_virt->cfg_desc_buf[d+12] : 0,
                    d+13<total_len ? mem_virt->cfg_desc_buf[d+13] : 0,
                    d+14<total_len ? mem_virt->cfg_desc_buf[d+14] : 0,
                    d+15<total_len ? mem_virt->cfg_desc_buf[d+15] : 0);
            }

            int is_keyboard = 0;
            uint8_t kbd_ep_addr = 0;
            uint16_t kbd_ep_max_pkt = 0;
            uint8_t kbd_ep_interval = 0;
            uint8_t kbd_iface_num = 0;

            int offset = 0;
            int in_kbd_iface = 0;
            while (offset + 2 <= total_len) {
                uint8_t desc_len = mem_virt->cfg_desc_buf[offset];
                uint8_t desc_type = mem_virt->cfg_desc_buf[offset + 1];

                if (desc_len == 0) break; 

                if (desc_type == 4 && desc_len >= 9) {
                    
                    uint8_t iface_class = mem_virt->cfg_desc_buf[offset + 5];
                    uint8_t iface_subclass = mem_virt->cfg_desc_buf[offset + 6];
                    uint8_t iface_protocol = mem_virt->cfg_desc_buf[offset + 7];
                    uint8_t iface_num = mem_virt->cfg_desc_buf[offset + 2];
                    
                    LOG_INFO("xHCI Device %d: Interface %d: Class=%d Sub=%d Proto=%d",
                             slot_id, iface_num, iface_class, iface_subclass, iface_protocol);
                    
                    if (iface_class == 3 && iface_subclass == 1 && iface_protocol == 1) {
                        LOG_INFO("xHCI Device %d: Found HID Keyboard interface %d", slot_id, iface_num);
                        in_kbd_iface = 1;
                        kbd_iface_num = iface_num;
                    } else {
                        in_kbd_iface = 0;
                    }
                } else if (desc_type == 5 && desc_len >= 7 && in_kbd_iface) {
                    
                    uint8_t ep_addr = mem_virt->cfg_desc_buf[offset + 2];
                    uint8_t ep_attr = mem_virt->cfg_desc_buf[offset + 3];
                    uint16_t ep_max_pkt = *(uint16_t*)(&mem_virt->cfg_desc_buf[offset + 4]);
                    uint8_t ep_interval = mem_virt->cfg_desc_buf[offset + 6];
                    
                    
                    if ((ep_addr & 0x80) && (ep_attr & 0x03) == 3) {
                        kbd_ep_addr = ep_addr;
                        kbd_ep_max_pkt = ep_max_pkt & 0x7FF;
                        kbd_ep_interval = ep_interval;
                        is_keyboard = 1;
                        LOG_INFO("xHCI Device %d: Keyboard Interrupt IN EP 0x%02x MaxPkt=%d Interval=%d",
                                 slot_id, kbd_ep_addr, kbd_ep_max_pkt, kbd_ep_interval);
                        break;
                    }
                }

                offset += desc_len;
            }

            if (!is_keyboard) {
                LOG_INFO("xHCI Device %d: Not a keyboard (is_keyboard=0, parsed %d bytes), skipping", slot_id, offset);
                continue;
            }

            
            
            uint32_t setcfg_lo = 0x00000900 | ((uint32_t)config_val << 16);
            uint32_t setcfg_hi = 0;
            cc = xhci_control_transfer(mem_virt, mem_phys, ir0_regs, db_regs,
                &ev_ccs, &ev_deq, slot_id, &ep0_enq, &ep0_pcs,
                setcfg_lo, setcfg_hi,
                NULL, 0, 2); 
            
            if (cc != 1) {
                LOG_WARN("xHCI Device %d: SET_CONFIGURATION failed, cc=%d", slot_id, cc);
                continue;
            }
            LOG_INFO("xHCI Device %d: SET_CONFIGURATION(%d) OK", slot_id, config_val);

            
            
            
            
            uint32_t setproto_lo = 0x00000B21;
            uint32_t setproto_hi = kbd_iface_num;
            cc = xhci_control_transfer(mem_virt, mem_phys, ir0_regs, db_regs,
                &ev_ccs, &ev_deq, slot_id, &ep0_enq, &ep0_pcs,
                setproto_lo, setproto_hi,
                NULL, 0, 2);
            
            if (cc != 1) {
                LOG_WARN("xHCI Device %d: SET_PROTOCOL failed, cc=%d (non-fatal)", slot_id, cc);
                
            } else {
                LOG_INFO("xHCI Device %d: SET_PROTOCOL(Boot) OK", slot_id);
            }

            
            
            uint32_t setidle_lo = 0x00000A21;
            uint32_t setidle_hi = kbd_iface_num;
            cc = xhci_control_transfer(mem_virt, mem_phys, ir0_regs, db_regs,
                &ev_ccs, &ev_deq, slot_id, &ep0_enq, &ep0_pcs,
                setidle_lo, setidle_hi,
                NULL, 0, 2);
            
            if (cc != 1) {
                LOG_WARN("xHCI Device %d: SET_IDLE failed, cc=%d (non-fatal)", slot_id, cc);
            } else {
                LOG_INFO("xHCI Device %d: SET_IDLE OK", slot_id);
            }

            
            
            
            
            uint8_t ep_num = kbd_ep_addr & 0x0F;
            int dci = ep_num * 2 + 1;  
            LOG_INFO("xHCI Device %d: Configuring EP%d IN, DCI=%d", slot_id, ep_num, dci);

            
            memset(mem_virt->int_in_ring, 0, sizeof(mem_virt->int_in_ring));

            
            memset(&mem_virt->in_ctx, 0, sizeof(mem_virt->in_ctx));
            
            
            mem_virt->in_ctx.control.param[1] = (1 << 0) | (1 << dci);

            
            
            mem_virt->in_ctx.slot.param[0] = ((uint32_t)dci << 27) | (speed << 20);
            mem_virt->in_ctx.slot.param[1] = ((i+1) << 16);

            
            
            
            int ep_ctx_idx = dci - 1;
            
            
            
            
            
            
            uint32_t ctx_interval = kbd_ep_interval;
            if (speed <= 3 && kbd_ep_interval > 0) {
                
                
                
                ctx_interval = 3 + 3; 
            }
            
            
            mem_virt->in_ctx.ep[ep_ctx_idx].param[0] = (ctx_interval << 16);
            
            
            
            mem_virt->in_ctx.ep[ep_ctx_idx].param[1] = (3 << 1) | (7 << 3) | ((uint32_t)kbd_ep_max_pkt << 16);

            
            uint64_t int_ring_phys = v2p(mem_virt, mem_phys, &mem_virt->int_in_ring[0]);
            mem_virt->in_ctx.ep[ep_ctx_idx].param[2] = (int_ring_phys & 0xFFFFFFF0) | 1;
            mem_virt->in_ctx.ep[ep_ctx_idx].param[3] = (int_ring_phys >> 32);

            
            mem_virt->in_ctx.ep[ep_ctx_idx].param[4] = 8;

            
            uint64_t in_ctx_phys = v2p(mem_virt, mem_phys, &mem_virt->in_ctx);
            trb.param[0] = (uint32_t)(in_ctx_phys & 0xFFFFFFFF);
            trb.param[1] = (uint32_t)(in_ctx_phys >> 32);
            trb.param[2] = 0;
            trb.param[3] = (slot_id << 24) | (12 << 10); 
            xhci_enqueue_cmd(mem_virt, db_regs, &cmd_enq, &cmd_pcs, trb);
            
            ev = xhci_wait_for_event(mem_virt, mem_phys, ir0_regs, &ev_ccs, &ev_deq, 33);
            if (!ev) {
                LOG_WARN("xHCI Device %d: Timeout waiting for Configure Endpoint", slot_id);
                continue;
            }
            int cfg_cc = (ev->param[2] >> 24) & 0xFF;
            xhci_advance_event(mem_virt, mem_phys, ir0_regs, &ev_ccs, &ev_deq);
            if (cfg_cc != 1) {
                LOG_WARN("xHCI Device %d: Configure Endpoint failed, cc=%d", slot_id, cfg_cc);
                continue;
            }
            LOG_INFO("xHCI Device %d: Configure Endpoint OK", slot_id);

            
            {
                xhci_device_context_t* dctx = &mem_virt->slots[slot_idx].dev_ctx;
                uint32_t ep_dw0 = dctx->ep[dci - 1].param[0];
                uint32_t ep_dw1 = dctx->ep[dci - 1].param[1];
                uint32_t ep_dw2 = dctx->ep[dci - 1].param[2];
                uint32_t ep_dw3 = dctx->ep[dci - 1].param[3];
                uint32_t ep_dw4 = dctx->ep[dci - 1].param[4];
                LOG_INFO("xHCI Device %d: Output EP%d ctx: DW0=0x%08x DW1=0x%08x DW2=0x%08x DW3=0x%08x DW4=0x%08x",
                         slot_id, dci, ep_dw0, ep_dw1, ep_dw2, ep_dw3, ep_dw4);
                LOG_INFO("xHCI Device %d: EP State=%d, EPType=%d, CErr=%d, MaxPkt=%d",
                         slot_id, ep_dw0 & 7, (ep_dw1 >> 3) & 7, (ep_dw1 >> 1) & 3,
                         (ep_dw1 >> 16) & 0xFFFF);
                uint32_t slot_dw0 = dctx->slot.param[0];
                uint32_t slot_dw1 = dctx->slot.param[1];
                uint32_t slot_dw3 = dctx->slot.param[3];
                LOG_INFO("xHCI Device %d: Output Slot ctx: DW0=0x%08x (CtxEntries=%d Speed=%d) DW1=0x%08x DW3=0x%08x (SlotState=%d)",
                         slot_id, slot_dw0, slot_dw0 >> 27, (slot_dw0 >> 20) & 0xF,
                         slot_dw1, slot_dw3, slot_dw3 >> 27);
            }

            
            memset(mem_virt->kbd_report, 0, sizeof(mem_virt->kbd_report));
            
            int int_in_enq = 0;
            int int_in_pcs = 1;
            
            uint64_t kbd_buf_phys = v2p(mem_virt, mem_phys, mem_virt->kbd_report);
            
            mem_virt->int_in_ring[int_in_enq].param[0] = (uint32_t)(kbd_buf_phys & 0xFFFFFFFF);
            mem_virt->int_in_ring[int_in_enq].param[1] = (uint32_t)(kbd_buf_phys >> 32);
            mem_virt->int_in_ring[int_in_enq].param[2] = 8;  
            mem_virt->int_in_ring[int_in_enq].param[3] = (1 << 10) | (1 << 5) | (int_in_pcs & 1);
            
            int_in_enq++;

            
            db_regs[slot_id] = dci;

            LOG_INFO("xHCI Device %d: Keyboard Interrupt IN endpoint active (DCI=%d)", slot_id, dci);
            LOG_INFO("xHCI Device %d: int_in_ring phys=0x%llx, kbd_report phys=0x%llx",
                     slot_id, (unsigned long long)v2p(mem_virt, mem_phys, &mem_virt->int_in_ring[0]),
                     (unsigned long long)kbd_buf_phys);
            LOG_INFO("xHCI Device %d: TRB[0]: 0x%08x 0x%08x 0x%08x 0x%08x",
                     slot_id,
                     mem_virt->int_in_ring[0].param[0], mem_virt->int_in_ring[0].param[1],
                     mem_virt->int_in_ring[0].param[2], mem_virt->int_in_ring[0].param[3]);

            
            ctrl->kbd_slot_id = slot_id;
            ctrl->kbd_slot_idx = slot_idx;
            ctrl->kbd_ep_dci = dci;
            ctrl->int_in_enq = int_in_enq;
            ctrl->int_in_pcs = int_in_pcs;
            ctrl->ep0_enq = ep0_enq;
            ctrl->ep0_pcs = ep0_pcs;
            ctrl->kbd_iface_num = kbd_iface_num;
            ctrl->kbd_leds = 0;
            ctrl->poll_count = 0;
            ctrl->has_kbd = 1;

            
            ctrl->ev_ccs = ev_ccs;
            ctrl->ev_deq = ev_deq;
            
            usb_dev_present_by_type(USB_DEV_TYPE_KEYBOARD);
            LOG_INFO("xHCI: USB 3.0 Keyboard driver initialized for device %d", slot_id);
        }
    }
    LOG_INFO("xHCI: Total connected devices: %d", connected_devices);
}
