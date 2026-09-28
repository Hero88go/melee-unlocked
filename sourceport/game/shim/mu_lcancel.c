/* What the host's L-cancel helper reads (port/runtime/host/lcancel.cpp). The recompiled build reads
 * these straight out of guest memory at the console's addresses; the native game keeps them in its
 * own structures with the host's layout, so it hands them over through MuGameApi.lcancel_view. */
#include "mu_lcancel_view.h"

#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>

void mu_lcancel_view(MuLcancelView* out)
{
    int slot;
    __builtin_memset(out, 0, sizeof(*out));
    out->pad_shift = HSD_PadLibData.clamp_analogLRShift;
    out->pad_max = HSD_PadLibData.clamp_analogLRMax;
    out->pad_min = HSD_PadLibData.clamp_analogLRMin;
    out->pad_scale = HSD_PadLibData.scale_analogLR;
    if (p_ftCommonData != NULL) {
        out->have_common = 1;
        out->trigger_deadzone = p_ftCommonData->analog_shoulder_deadzone;
        out->lcancel_window = p_ftCommonData->xE4;
    }
    /* As the recompiled build: human slots 0-3, first one on each controller port. */
    for (slot = 0; slot < 4; ++slot) {
        HSD_GObj* gobj;
        Fighter* fp;
        int port;
        if (Player_GetPlayerSlotType(slot) != Gm_PKind_Human)
            continue;
        port = (s8) Player_GetSubColor(slot);
        if (port < 0 || port > 3 || out->port[port].present)
            continue;
        if (Player_GetPlayerState(slot) == 0)
            continue;
        gobj = Player_GetEntityAtIndex(slot, 0);
        if (gobj == NULL || (fp = gobj->user_data) == NULL || fp->gobj != gobj)
            continue;
        out->port[port].present = 1;
        out->port[port].slot = slot;
        out->port[port].motion_id = fp->motion_id;
        out->port[port].ground_or_air = fp->ground_or_air;
        out->port[port].frames_since_trigger = fp->x67F;
        out->port[port].anim_frame = fp->cur_anim_frame;
    }
}
