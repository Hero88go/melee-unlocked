#include <assert.h>
#include <stddef.h>
typedef struct Fighter { int kind, is_sub_fighter, player_idx; struct { float x1830_percent; } dmg; } Fighter;
typedef struct HSD_GObj { struct HSD_GObj* next; void* user_data; } HSD_GObj;
enum { HSD_GOBJ_PLINK_FIGHTER = 8, Ft_Kind_Nana = 11 };
static HSD_GObj* heads[16];
static HSD_GObj** HSD_GObjPLinkHead = heads;
static HSD_GObj* partners[4];
static HSD_GObj* Player_GetEntityAtIndex(int slot, int index) { assert(index == 1); return partners[slot]; }
#include "mu_live_fighter_body.h"
#include "mu_nana_damage_body.h"
int main(void)
{
    Fighter sheik = {7,0,0,{10}}, nana = {Ft_Kind_Nana,1,2,{37}};
    HSD_GObj first = {NULL,&sheik}, second = {NULL,&nana};
    first.next = &second; heads[8] = &first;
    partners[0] = &first; partners[1] = (HSD_GObj*)1; partners[2] = &second;
    assert(mu_nana_damage(0) == -1 && mu_nana_damage(1) == -1);
    assert(mu_nana_damage(2) == 37 && mu_nana_damage(3) == -1);
    nana.is_sub_fighter = 0; assert(mu_nana_damage(2) == -1);
    nana.is_sub_fighter = 1; nana.player_idx = 1; assert(mu_nana_damage(2) == -1);
    heads[8] = NULL; assert(mu_nana_damage(2) == -1);
    return 0;
}
