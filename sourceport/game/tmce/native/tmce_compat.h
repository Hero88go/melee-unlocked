/* Training Mode CE, native build: the few MexTK views that span more than one game object.
 *
 * On the console some MexTK structs run past the end of one game object into the next one in memory
 * (the match camera into the debug camera, the HUD into the damage-display state). Natively each
 * object has its own layout and place, so TM-CE reaches those parts through these instead.
 * Included at the end of the native MexTK mex.h. */
#ifndef TMCE_COMPAT_H
#define TMCE_COMPAT_H

/* MatchCamera.devcam_* (console 0x80452C68 + 0x3D8..): the debug camera, CameraDebugMode cm_80453004
 * (free_int_pos, free_eye_pos, free_fov). No pointers in it, so the console offsets hold natively. */
extern char mu_mx_cm_80453004[] __asm__("cm_80453004");
#define MEX_DEVCAM_POS() ((Vec3 *)(mu_mx_cm_80453004 + 0x3C))
#define MEX_DEVCAM_ROT() ((Vec3 *)(mu_mx_cm_80453004 + 0x48))
#define MEX_DEVCAM_FOV() ((float *)(mu_mx_cm_80453004 + 0x54))

/* MatchHUD.element_data[i] (console 0x804A0FD8 + 0xF0): the damage display of player i,
 * ifStatus_HudInfo.players[i] (MatchHUDElement is its native twin). */
extern char mu_mx_ifStatus_HudInfo[] __asm__("ifStatus_HudInfo");
#define MEX_MATCHHUD_ELEMENT(i) (&((MatchHUDElement *)mu_mx_ifStatus_HudInfo)[i])

/* A disc pointer slot written from host code: the address fits in 32 bits natively (MEM1, the image). */
#define MEX_DP_SET(slot, ptr) ((slot) = (unsigned int)(uintptr_t)(ptr))
/* Element i of a disc array of pointer slots (big-endian words) */
#define MEX_DP_AT(base, i) ((void *)(uintptr_t)__builtin_bswap32(((const unsigned int *)(base))[i]))
/* TM-CE's own structs read straight out of its .dat files */
#define MEX_DISC_STRUCT __attribute__((scalar_storage_order("big-endian")))

/* Decomp objects TM-CE reaches by console address. */
extern char mu_mx_hsd_804D0F60[] __asm__("hsd_804D0F60");         /* particle allocator */
extern char mu_mx_cm_803BCCA0[] __asm__("cm_803BCCA0");           /* camera tuning (floats) */
extern char mu_mx_HSD_GObj_CameraKind[] __asm__("HSD_GObj_CameraKind");
extern char mu_mx_HSD_GObjPLinkHead[] __asm__("HSD_GObjPLinkHead");
extern char mu_mx_it_803F58E0[] __asm__("it_803F58E0");           /* barrel item states */
extern char mu_mx_it_803F5988[] __asm__("it_803F5988");
extern char mu_mx_it_3F14_Logic2_Spawned[] __asm__("it_3F14_Logic2_Spawned");
extern char mu_mx_itTaru_Logic2_PickedUp[] __asm__("itTaru_Logic2_PickedUp");
extern char mu_mx_itTaru_Logic2_Dropped[] __asm__("itTaru_Logic2_Dropped");
extern char mu_mx_itTaru_Logic2_Thrown[] __asm__("itTaru_Logic2_Thrown");
extern char mu_mx_it_3F14_Logic2_DmgDealt[] __asm__("it_3F14_Logic2_DmgDealt");
extern char mu_mx_it_3F14_Logic2_Reflected[] __asm__("it_3F14_Logic2_Reflected");
extern char mu_mx_it_3F14_Logic2_Clanked[] __asm__("it_3F14_Logic2_Clanked");
extern char mu_mx_it_3F14_Logic2_HitShield[] __asm__("it_3F14_Logic2_HitShield");
extern char mu_mx_itTaru_Logic2_EvtUnk[] __asm__("itTaru_Logic2_EvtUnk");

/* The small-data (r13) objects TM-CE reads: each offset TM-CE uses, mapped to its decomp object. An
 * offset missing here fails to link (mex_r13_unmapped), so a new use cannot silently read MEM1. */
void mex_r13_unmapped(void);
static inline void *mex_r13_addr(int off)
{
    switch (off) {
    case -0x3E55: return mu_mx_HSD_GObj_CameraKind;
    case -0x3E74: return mu_mx_HSD_GObjPLinkHead;
    default: mex_r13_unmapped(); return 0;
    }
}
#undef R13_OFFSET
#undef R13_PTR
#undef R13_INT
#undef R13_U8
#undef R13_FLOAT
#define R13_OFFSET(offset) mex_r13_addr(offset)
#define R13_PTR(offset) (*(void **)mex_r13_addr(offset))
#define R13_INT(offset) (*(int *)mex_r13_addr(offset))
#define R13_U8(offset) (*(u8 *)mex_r13_addr(offset))
#define R13_FLOAT(offset) (*(float *)mex_r13_addr(offset))

/* Runtime state shared by the modules (native/tmce_runtime.c, shim/mu_tmce.c, gr/). On the console
 * these sat at fixed addresses every module knew. */
extern void *mu_tmce_event_vars;
extern s8 mu_tmce_onload_fileno;
extern s8 mu_tmce_onload_slot;
extern int mu_tmce_stadium_override;
void mu_tmce_stadium_transform(GOBJ *map_gobj, int transformation_id);
void mu_tmce_fod_platform_set(GOBJ *platform_gobj, int mode, int timer);

#endif
