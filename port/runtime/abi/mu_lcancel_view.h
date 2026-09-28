/* Plain C payload for the host's L-cancel helper (game API version 3, MuGameApi.lcancel_view): the
 * values port/runtime/host/lcancel.cpp reads from guest memory in the recompiled build, filled by the
 * native game from its own structures. Kept free of system includes, like mu_native_pose.h, so the
 * native decompilation units (their own C library) can include it.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MU_LCANCEL_VIEW_H
#define MU_LCANCEL_VIEW_H

#if defined(_MSC_VER)
typedef signed __int32 MuLcancelS32;
typedef unsigned __int32 MuLcancelU32;
#else
typedef __INT32_TYPE__ MuLcancelS32;
typedef __UINT32_TYPE__ MuLcancelU32;
#endif

/* Per controller port, the human fighter that port drives. */
typedef struct MuLcancelFighter {
    MuLcancelS32 present;
    MuLcancelS32 slot;                   /* player slot 0-5, what a draw's owner tint is keyed on */
    MuLcancelS32 motion_id;
    MuLcancelU32 ground_or_air;          /* 0 ground, 1 air */
    MuLcancelU32 frames_since_trigger;   /* Fighter x67F */
    float anim_frame;
} MuLcancelFighter;

typedef struct MuLcancelView {
    MuLcancelS32 pad_shift, pad_min, pad_max, pad_scale;   /* HSD_PadLibData analog L/R clamp, scale */
    MuLcancelS32 have_common;                              /* p_ftCommonData is loaded */
    float trigger_deadzone;                                /* ftCommonData analog_shoulder_deadzone */
    MuLcancelS32 lcancel_window;                           /* ftCommonData xE4 */
    MuLcancelFighter port[4];
} MuLcancelView;

#endif
