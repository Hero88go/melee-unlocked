/* Shared between the shim's own translation units. Game code never includes this. */
#ifndef MU_SHIM_H
#define MU_SHIM_H

#include "mu_host.h"

extern const MuHostApi* mu_host;

/* The retrace the host drives: runs the game's VI callbacks and its alarms. */
void mu_vi_retrace(void);
/* Interrupts, which the game uses as a critical section around its own data. While they are off
 * the shim defers anything the host would deliver. */
int mu_interrupts_enabled(void);
void mu_deliver_pending(void);

#endif
