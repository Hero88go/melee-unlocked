/* The real overlay-position routine must tolerate fighter creation and destruction boundaries. */
#include <assert.h>
#include <math.h>
#include <stddef.h>

typedef struct { float x, y, z; } Vec3;
typedef struct HSD_GObj { struct HSD_GObj* next; void* user_data; void* hsd_obj; } HSD_GObj;
enum { Gm_Player_NumMax = 6, Gm_PKind_NA = 3, HSD_GOBJ_PLINK_FIGHTER = 8 };
static HSD_GObj fighter, other, camera;
static HSD_GObj* active;
static HSD_GObj* heads[16];
static HSD_GObj** HSD_GObjPLinkHead = heads;
static int kind, state, stocks, coordinate_reads, height_reads, projections;
static Vec3 position;
static int Player_GetPlayerSlotType(int slot) { assert(slot >= 0 && slot < 6); return kind; }
static int Player_GetPlayerState(int slot) { (void)slot; return state; }
static int Player_GetStocks(int slot) { (void)slot; return stocks; }
static HSD_GObj* Player_GetEntity(int slot) { (void)slot; return active; }
static HSD_GObj* Camera_80030A50(void) { return camera.user_data ? &camera : NULL; }
static void Player_LoadPlayerCoords(int slot, Vec3* out) { (void)slot; ++coordinate_reads; *out = position; }
static float Player_800360D8(int slot)
{
    (void)slot; ++height_reads;
    /* As the real getter, this requires a live fighter. A scene's saved slot alone is insufficient. */
    assert(active == &fighter && fighter.user_data != NULL);
    return 12.5f;
}
static void lbVector_WorldToScreen(void* cobj, Vec3* in, Vec3* out, int unused)
{ (void)cobj; (void)unused; ++projections; *out = *in; out->x *= 2; out->y *= 2; }

#include "fighter_screen_position_body.h"

static void reset(void)
{
    fighter = (HSD_GObj){0}; other = (HSD_GObj){0}; camera = (HSD_GObj){0};
    fighter.user_data = &fighter; camera.user_data = &camera; camera.hsd_obj = &camera;
    active = &fighter; HSD_GObjPLinkHead = heads; heads[HSD_GOBJ_PLINK_FIGHTER] = &fighter;
    kind = 0; state = 1; stocks = 4; position = (Vec3){10, 20, 0};
    coordinate_reads = height_reads = projections = 0;
}
static void hidden(int slot)
{
    float x = 42, y = 24;
    assert(mu_fighter_screen_position(slot, &x, &y) == 0);
    assert(x == 42 && y == 24 && projections == 0);
}
int main(void)
{
    /* Report #413: slots retain state/stocks after Training frees their fighter on return to CSS. */
    reset(); active = NULL; heads[HSD_GOBJ_PLINK_FIGHTER] = NULL; hidden(0);
    assert(coordinate_reads == 0 && height_reads == 0);
    reset(); active = (HSD_GObj*)1; heads[HSD_GOBJ_PLINK_FIGHTER] = &other; hidden(0);
    assert(coordinate_reads == 0 && height_reads == 0);
    reset(); HSD_GObjPLinkHead = NULL; hidden(0);
    reset(); fighter.user_data = NULL; hidden(0);
    reset(); kind = Gm_PKind_NA; hidden(0);
    reset(); state = 0; hidden(0);
    reset(); stocks = 0; hidden(0);
    reset(); camera.user_data = NULL; hidden(0);
    reset(); camera.hsd_obj = NULL; hidden(0);
    reset(); hidden(-1); hidden(Gm_Player_NumMax);
    reset(); position.x = NAN; hidden(0);
    reset(); position.y = INFINITY; hidden(0);
    /* A live transformed fighter can follow another fighter on the link; the position keeps moving. */
    reset(); other.next = &fighter; heads[HSD_GOBJ_PLINK_FIGHTER] = &other;
    for (int frame = 0; frame < 60; ++frame) {
        float x, y;
        position.x = frame; position.y = frame * 2;
        assert(mu_fighter_screen_position(0, &x, &y) == 1);
        assert(x == frame * 2 && y == frame * 4 + 20);
    }
    return 0;
}
