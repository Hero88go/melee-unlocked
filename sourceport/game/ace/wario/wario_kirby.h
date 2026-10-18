/* ACE 2.0.0's Wario: the ability Kirby copies from him (wario_kirby.c, PlKbCpWr.dat). */
#ifndef MU_ACE_WARIO_KIRBY_H
#define MU_ACE_WARIO_KIRBY_H

#include "../../akaneia/common/mu_ak_kirby.h"

/* The kbFunction exports of PlKbCpWr.dat. Wario's MuAkFighter (ace/wario/wario.c) points at it:
 *     .kirby = &ftKbWr_Copy, */
extern const MuAkKirbyCopy ftKbWr_Copy;

#endif
