#ifndef MU_SWORDPLAY_H
#define MU_SWORDPLAY_H
#include "../common/mu_gr_common.h"
#include <melee/if/forward.h>
/* provided by common/mu_gr_mex_services.c */
void mu_gr_mex_devtext_remove(DevText* text);
void sword_menu_init(void);
extern int sword_spoof, sword_players;
extern float sword_speed;
#endif
