#pragma once

#include "../pcie.h"

void xhci_init(pcie_device_info_t* dev);

void xhci_poll(void);
