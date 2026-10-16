/* Wobbuffet's counter damage decays by a fixed step every frame (it_802CD4FC) and goes below zero
 * at the end of the swing. The console converts that negative float to 0; a plain C cast on the PC
 * wraps it to 4294967295, which the hit code stops on ("attack power over 500", ftcoll.c:1089). */
#include "melee/it/kinds/itsonans.c"

extern int printf(const char*, ...);
__declspec(dllimport) void* __stdcall VirtualAlloc(void*, unsigned long long, unsigned long, unsigned long);

static u32 g_damage = 12345;
void it_80272460(HitCapsule* hitbox, u32 damage, Item_GObj* gobj)
{
    (void) hitbox;
    (void) gobj;
    g_damage = damage;
}

int main(void)
{
    /* Disc pointers are 32 bits wide: the test data lives below 4 GB like the game's. */
    unsigned char* low = VirtualAlloc((void*) 0x10000000, 0x10000, 0x3000, 0x04);
    if (low == NULL) {
        printf("SKIP: no low memory\n");
        return 0;
    }
    HSD_GObj* gobj = (HSD_GObj*) low;
    Item* ip = (Item*) (low + 0x1000);
    Article* article = (Article*) (low + 0x8000);
    itsonansAttributes* attrs = (itsonansAttributes*) (low + 0x9000);
    gobj->user_data = ip;
    ip->xC4_article_data = article;
    article->x4_specialAttributes.raw = (mu_u32) (mu_uptr) attrs;
    ip->ground_or_air = GA_Ground;
    ip->xDAC_itcmd_var0 = 0;

    ip->xDD4_itemVar.sonans.x68 = 3.0f;
    it_802CD4FC(gobj);
    if (g_damage != 3) {
        printf("FAIL: counter damage 3 became %u\n", g_damage);
        return 1;
    }
    ip->xDD4_itemVar.sonans.x68 = -2.0f;
    it_802CD4FC(gobj);
    if (g_damage != 0) {
        printf("FAIL: counter damage below zero became %u, the console gives 0\n", g_damage);
        return 2;
    }
    return 0;
}
