/* Native half of the per-retrace state comparison. Keep game headers out of mu_entry.c:
 * the console's ssize_t conflicts with the Windows host ABI headers there. */
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmvs.h>
#include <melee/pl/player.h>
#include <melee/ft/types.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/jobj.h>
#include <melee/lb/types.h>
#include <melee/mp/mplib.h>
#include <melee/mp/types.h>
#include "../../../port/runtime/abi/mu_look_snapshot.h"

int mu_is_game_memory(const void* p, unsigned long size);   /* shim/mu_entry.c */
extern StaticPlayer player_slots[];

/* Optional read-only item snapshot. Lab styles replace the 3D scene, so their projectiles
 * must come from live items rather than relying on recording events being enabled. */
MU_EXPORT void mu_look_items_v1(MuLookItems* out)
{
    HSD_GObj* gobj;
    int visited = 0;
    __builtin_memset(out, 0, sizeof *out);
    if (!HSD_GObjPLinkHead) return;
    for (gobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_ITEM]; gobj && visited++ < MU_LOOK_ITEMS; gobj = gobj->next) {
        const Item* ip;
        MuLookItem* dst;
        unsigned int word0, word6;
        if (!mu_is_game_memory(gobj, sizeof *gobj)) break;
        ip = gobj->user_data;
        if (!ip || !mu_is_game_memory(ip, sizeof *ip)) continue;
        dst = &out->items[out->count++];
        dst->type = ip->kind; dst->state = ip->msid; dst->owner = -1;
        dst->facing = ip->facing_dir; dst->vx = ip->x40_vel.x; dst->vy = ip->x40_vel.y;
        dst->x = ip->pos.x; dst->y = ip->pos.y;
        __builtin_memcpy(&word0, &ip->xDD4_itemVar, 4);
        __builtin_memcpy(&word6, (const char*)&ip->xDD4_itemVar + 0x18, 4);
        dst->missile_type = word0 & 255; dst->charge_level = word6 & 255;
        if (ip->owner && mu_is_game_memory(ip->owner, sizeof *ip->owner) &&
            ip->owner->classifier == HSD_GOBJ_CLASS_FIGHTER && ip->owner->user_data &&
            mu_is_game_memory(ip->owner->user_data, sizeof(Fighter)))
            dst->owner = ((Fighter*)ip->owner->user_data)->player_idx;
    }
}

/* Host calls at a retrace boundary only while a low-detail view is active. This copies live
 * geometry; it never changes fighter state, collision, inputs, or the recorded match. */
MU_EXPORT void mu_look_snapshot_v1(MuLookSnapshot* out)
{
    int slot, follower;
    __builtin_memset(out, 0, sizeof *out);
    out->major = gm_GetCurrentGameMode();
    out->minor = gm_GetCurrentSceneIndex();
    out->frame = gm_GetFrameCount();
    {
        VsSceneController* vs = gmVs_GetSceneController();
        if (vs && mu_is_game_memory(vs, sizeof *vs)) out->stage = vs->start.stkind;
    }
    {
        MapCollData* map = mpLib_8004D164();
        CollVtx* verts = mpGetGroundCollVtx();
        CollLine* lines = mpGetGroundCollLine();
        if (map && mu_is_game_memory(map, sizeof *map) && map->vert_count > 0 && map->vert_count <= 2048 &&
            map->line_count > 0 && map->line_count <= MU_LOOK_STAGE_LINES &&
            mu_is_game_memory(verts, sizeof *verts * map->vert_count) &&
            mu_is_game_memory(lines, sizeof *lines * map->line_count)) {
            int i;
            for (i = 0; i < map->line_count; ++i) {
                CollLine* line = &lines[i];
                unsigned flags = line->flags;
                MapLine* def = line->x0;
                unsigned a, b;
                MuLookStageLine* dst;
                if (!(flags & LINE_FLAG_ENABLED) || (flags & (LINE_FLAG_HIDDEN | LINE_FLAG_EMPTY)) || !(flags & LINE_FLAG_KIND) ||
                    !def || !mu_is_game_memory(def, sizeof *def)) continue;
                a = def->v0_idx; b = def->v1_idx;
                if (a >= (unsigned)map->vert_count || b >= (unsigned)map->vert_count) continue;
                dst = &out->stage_lines[out->stage_line_count++];
                dst->x0 = verts[a].pos.x; dst->y0 = verts[a].pos.y;
                dst->x1 = verts[b].pos.x; dst->y1 = verts[b].pos.y;
                dst->floor = (flags & CollLine_Floor) != 0; dst->platform = (flags & LINE_FLAG_PLATFORM) != 0;
            }
        }
    }
    for (slot = 0; slot < 4; ++slot) {
        if (Player_GetPlayerState(slot) != 2) continue;
        for (follower = 0; follower < 2; ++follower) {
            HSD_GObj* gobj = Player_GetEntityAtIndex(slot, follower);
            Fighter* fp;
            MuLookFighter* dst = &out->fighters[slot][follower];
            int i;
            struct Node { HSD_JObj* jobj; int depth; } stack[MU_LOOK_BONES];
            int top = 0, seen = 0;
            if (!gobj || !mu_is_game_memory(gobj, sizeof *gobj)) continue;
            fp = gobj->user_data;
            if (!fp || !mu_is_game_memory(fp, sizeof *fp) || fp->gobj != gobj) continue;
            dst->present = 1; dst->kind = fp->kind; dst->external = player_slots[slot].ckind;
            dst->costume = player_slots[slot].costume_id; dst->team = player_slots[slot].team;
            dst->stocks = Player_GetStocks(slot); dst->action = fp->motion_id;
            dst->x = fp->cur_pos.x; dst->y = fp->cur_pos.y; dst->facing = fp->facing_dir;
            dst->percent = fp->dmg.x1830_percent; dst->shield = fp->shield_health; dst->animation = fp->cur_anim_frame;
            for (i = 0; i < fp->hurt_capsules_len && i < MU_LOOK_CAPSULES; ++i) {
                const HurtCapsule* h = &fp->hurt_capsules[i].capsule;
                MuLookCapsule* c = &dst->capsules[dst->capsule_count++];
                c->ax = h->a_pos.x; c->ay = h->a_pos.y; c->bx = h->b_pos.x; c->by = h->b_pos.y; c->radius = h->scale;
            }
            if (gobj->hsd_obj && mu_is_game_memory(gobj->hsd_obj, sizeof(HSD_JObj)))
                stack[top++] = (struct Node){ gobj->hsd_obj, 0 };
            while (top && seen++ < MU_LOOK_BONES) {
                struct Node item = stack[--top];
                HSD_JObj* j = item.jobj;
                if (j->next && top < MU_LOOK_BONES && mu_is_game_memory(j->next, sizeof *j)) stack[top++] = (struct Node){j->next, item.depth};
                if (j->child && top < MU_LOOK_BONES && mu_is_game_memory(j->child, sizeof *j)) stack[top++] = (struct Node){j->child, item.depth + 1};
                if (item.depth >= 5 && j->parent && mu_is_game_memory(j->parent, sizeof *j)) {
                    MuLookBone* b = &dst->bones[dst->bone_count++];
                    b->ax = j->parent->mtx[0][3]; b->ay = j->parent->mtx[1][3];
                    b->bx = j->mtx[0][3]; b->by = j->mtx[1][3];
                }
            }
        }
    }
}

static u32 float_bits(float value)
{
    u32 bits;
    __builtin_memcpy(&bits, &value, sizeof bits);
    return bits;
}

/* Called from mu_match.c's mu_rng_seed_request (--rng-seed, M7 lockstep parity): mu_match.c is the
 * ABI side and cannot include random.h itself (it also includes mu_host.h, whose stdint.h
 * conflicts with Runtime/platform.h's ssize_t in the same translation unit), so the actual write
 * happens here where random.h is already pulled in for HSD_RandSeedPtr. */
void mu_apply_seed(u32 seed)
{
    *HSD_RandSeedPtr = seed;
}

void mu_state_snapshot_words(u32* out)
{
    int slot;
    /* Layout must track MuStatePod in mu_host.h exactly: rng, scene, 6*12 player words,
     * scene_major, match_frame. */
    __builtin_memset(out, 0, (2 + 6 * 12 + 2) * sizeof *out);
    out[0] = *HSD_RandSeedPtr;
    out[1] = gm_GetCurrentSceneIndex();
    for (slot = 0; slot < 6; ++slot) {
        u32* state = out + 2 + slot * 12;
        if (Player_GetPlayerState(slot) != 2)
            continue;
        HSD_GObj* gobj = Player_GetEntity(slot);
        Fighter* fp;
        /* As the recompiled build's reader: a slot can still say "in game" after its fighter was
         * freed (a match that ends straight into character select), so follow the pointers only
         * inside game memory and only to a fighter that points back at the same object. */
        if (!gobj || !mu_is_game_memory(gobj, sizeof *gobj))
            continue;
        fp = gobj->user_data;
        if (!fp || !mu_is_game_memory(fp, sizeof *fp) || fp->gobj != gobj)
            continue;
        state[0] = 1;
        state[1] = Player_GetStocks(slot);
        state[2] = fp->motion_id;
        state[3] = float_bits(fp->cur_anim_frame);
        state[4] = float_bits(fp->cur_pos.x);
        state[5] = float_bits(fp->cur_pos.y);
        state[6] = float_bits(fp->cur_pos.z);
        state[7] = float_bits(fp->self_vel.x);
        state[8] = float_bits(fp->self_vel.y);
        state[9] = float_bits(fp->self_vel.z);
        state[10] = float_bits(fp->dmg.x1830_percent);
        state[11] = float_bits(fp->facing_dir);
    }
    out[2 + 6 * 12] = gm_GetCurrentGameMode();
    out[2 + 6 * 12 + 1] = gm_GetFrameCount();
}
