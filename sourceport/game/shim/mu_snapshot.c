/* Native half of the per-retrace state comparison. Keep game headers out of mu_entry.c:
 * the console's ssize_t conflicts with the Windows host ABI headers there. */
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmvs.h>
#include <melee/pl/player.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/random.h>

static u32 float_bits(float value)
{
    u32 bits;
    __builtin_memcpy(&bits, &value, sizeof bits);
    return bits;
}

/* Called from mu_match.c's mu_seed_request (--rng-seed, M7 lockstep parity): mu_match.c is the
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
        if (!gobj || !(fp = gobj->user_data))
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
