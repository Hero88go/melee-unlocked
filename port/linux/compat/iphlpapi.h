// Linux build: the default route, read from /proc/net/route. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "winsock2.h"

typedef struct _MIB_IPFORWARDROW {
  DWORD dwForwardDest, dwForwardMask, dwForwardPolicy, dwForwardNextHop, dwForwardIfIndex, dwForwardType,
        dwForwardProto, dwForwardAge, dwForwardNextHopAS, dwForwardMetric1, dwForwardMetric2, dwForwardMetric3,
        dwForwardMetric4, dwForwardMetric5;
} MIB_IPFORWARDROW;
// dwForwardNextHop is in network byte order, as on Windows.
DWORD GetBestRoute(DWORD destination, DWORD source, MIB_IPFORWARDROW* row);
