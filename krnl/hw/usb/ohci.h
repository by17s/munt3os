#pragma once

#include "../pcie.h"

void ohci_init(pcie_device_info_t* dev);
void ohci_poll(void);
