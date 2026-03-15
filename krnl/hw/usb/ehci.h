#pragma once

#include "../pcie.h"

void ehci_init(pcie_device_info_t* dev);

void ehci_poll(void);