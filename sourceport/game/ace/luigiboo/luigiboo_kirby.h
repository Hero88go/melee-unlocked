#ifndef MU_ACE_LUIGIBOO_KIRBY_H
#define MU_ACE_LUIGIBOO_KIRBY_H

#include "../../akaneia/common/mu_ak_kirby.h"

/* Shared support is requested in SHARED_REQUESTS.md. Until applied, this
 * ability remains unavailable rather than pretending the retail file has
 * an added-ability ftcmd table. */
#ifdef MU_AK_KIRBY_RETAIL_STATES
extern const MuAkKirbyCopy ftLb_KirbyCopy;
#endif

#endif
