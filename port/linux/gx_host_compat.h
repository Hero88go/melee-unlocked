// Bridge shared GX decoding to the native Linux machine services.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace linux_renderer {
// Call after linux_host::initialize, before the first FIFO batch. The Linux host
// owns the underlying fixed-address MEM1 and native game image mappings.
void connect_host();
}
