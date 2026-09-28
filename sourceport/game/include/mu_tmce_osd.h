/* Training Mode CE, native build: the on-screen displays and the per-fighter data they read.
 *
 * On the console these are the PowerPC patches under ASM/training-mode/Onscreen Display and
 * ASM/training-mode/Misc (ledger: run-source/tmce-port/ledger_T2.md). Natively each is a call from
 * the decomp statement its address lands on. Every call site checks mu_tmce_active() first (or, for
 * the pass-through ones, the hook checks it and returns its argument unchanged), so with TM-CE off
 * (always online, and in replays recorded without it) nothing here runs.
 *
 * Bodies: sourceport/game/shim/mu_tmce_osd.c (decomp types) and
 * sourceport/game/tmce/native/tmce_osd.c (TM-CE's MexTK types).
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#ifndef MU_TMCE_OSD_H
#define MU_TMCE_OSD_H

struct Fighter;
struct HSD_GObj;
struct Item;

/* ---- per-fighter bookkeeping (Additional Playerblock Variables, fp->mu_tm) ---- */
void mu_tmce_osd_change_motion(struct Fighter* fp);      /* ft/fighter.c Fighter_ChangeMotionState */
void mu_tmce_osd_fastfall_reset(struct Fighter* fp);     /* same, where fall_fast is cleared */
void mu_tmce_osd_anim_counts(struct Fighter* fp);        /* ft/fighter.c Fighter_procAnim */
void mu_tmce_osd_post_hitstun(struct Fighter* fp);       /* same, before anim_cb */
void mu_tmce_osd_anim_end(struct HSD_GObj* gobj);        /* same, at its end */
void mu_tmce_osd_input(struct HSD_GObj* gobj);           /* ft/fighter.c Fighter_procInput */
int mu_tmce_osd_ff_tick(struct Fighter* fp);             /* ft/ftcommon.c ftCommon_CheckFallFast (returns 1) */
void mu_tmce_osd_ff_display(struct Fighter* fp);         /* same, on a fastfall */
void mu_tmce_osd_shield_hit(struct Fighter* attacker, struct Fighter* defender); /* ftColl_80076CBC */
void mu_tmce_osd_hit_backup(struct Fighter* victim);     /* pl/pltrick.c pl_80038144 */
void mu_tmce_osd_throw_source(struct Fighter* victim, struct HSD_GObj* thrower); /* ftCo_800DDDE4 */
int mu_tmce_osd_same_projectile(struct Item* ip, struct HSD_GObj* victim);     /* ftColl_80078998 */
int mu_tmce_osd_same_move_again(struct Fighter* fp);     /* ft/ft_0892.c ft_800895E0 */
int mu_tmce_osd_combo_move(int slot, int victim_slot, int move); /* pl/pl_040D.c pl_80040ED4 */
void mu_tmce_osd_combo_increment(int slot, int victim_slot, int arg3); /* same, at its end */
void mu_tmce_osd_sdi_init(struct HSD_GObj* gobj);        /* ftCo_Damage.c ftCo_8008EC90 */
int mu_tmce_osd_sdi(struct Fighter* fp);                 /* ftCo_Damage_OnEveryHitlag (returns 1) */
void mu_tmce_osd_airdodge_angle(struct Fighter* fp);     /* ftCo_EscapeAir.c ftCo_80099A9C */

/* ---- displays ---- */
void mu_tmce_osd_act_oo_float(struct Fighter* fp);
void mu_tmce_osd_airdodge_item(struct Fighter* fp, int is_throw);
void mu_tmce_osd_boost_grab(struct Fighter* fp);
void mu_tmce_osd_fortress_nil(struct Fighter* fp);
void mu_tmce_osd_glide_toss(struct Fighter* fp);
void mu_tmce_osd_grab_breakout(struct Fighter* victim);
void mu_tmce_osd_lcancel(struct HSD_GObj* gobj);
void mu_tmce_osd_powershield(struct Fighter* fp);
void mu_tmce_osd_egg_toss(struct Fighter* fp);
void mu_tmce_osd_shield_count(struct Fighter* fp);
void mu_tmce_osd_shield_count_reset(struct Fighter* fp);
void mu_tmce_osd_dashback(struct HSD_GObj* gobj);
void mu_tmce_osd_cliffwait_frames(struct Fighter* fp);
void mu_tmce_osd_galint_aerial(struct HSD_GObj* gobj);
void mu_tmce_osd_galint_laser(struct HSD_GObj* gobj);
void mu_tmce_osd_galint_ledgedash(struct HSD_GObj* gobj);
void mu_tmce_osd_galint_noimpact(struct HSD_GObj* gobj);
void mu_tmce_osd_ledgestall(struct HSD_GObj* gobj);
void mu_tmce_osd_wavedash(struct HSD_GObj* gobj);

/* Pass-through hooks at `if (check(gobj))` sites: they return `interrupted` unchanged, and do
 * nothing unless TM-CE is active. */
enum {
    MU_TMCE_OO_SPECIAL_JUMP,
    MU_TMCE_OO_AERIAL_JUMP,
    MU_TMCE_OO_DBLJUMP_JUMP,
    MU_TMCE_OO_SPECIAL_DBLJUMP,
    MU_TMCE_OO_AERIAL_DBLJUMP,
    MU_TMCE_OO_SPECIAL_DROP,
    MU_TMCE_OO_AERIAL_DROP,
    MU_TMCE_OO_SPECIAL_FALL,
    MU_TMCE_OO_AERIAL_FALL,
};
int mu_tmce_osd_oo_airborne(int interrupted, struct HSD_GObj* gobj, int which);
int mu_tmce_osd_oo_hitstun(int interrupted, struct HSD_GObj* gobj);

/* Patches at a function's shared epilogue (reached from every return): MU_TMCE_ON_EXIT(fn, gobj)
 * as the function's first statement runs fn when the function returns, with the motion state the
 * fighter had on entry. */
typedef struct MuTmceExit {
    struct HSD_GObj* gobj;
    int msid;
} MuTmceExit;
int mu_tmce_osd_msid(struct HSD_GObj* gobj); /* the fighter's motion state, -1 with TM-CE off */
#define MU_TMCE_ON_EXIT(fn, g)                                                                    \
    MuTmceExit mu_tmce_exit __attribute__((cleanup(fn), unused)) = { (struct HSD_GObj*) (g),      \
                                                                     mu_tmce_osd_msid((struct HSD_GObj*) (g)) }
void mu_tmce_osd_kneebend_exit(MuTmceExit* e);   /* ftCo_KneeBend_IASA: Jump Cancel */
void mu_tmce_osd_guard_iasa_exit(MuTmceExit* e); /* ftCo_Guard_IASA, ftCo_GuardOff_IASA: Act OoS */
void mu_tmce_osd_damagefall_exit(MuTmceExit* e); /* ftCo_DamageFall_IASA: Act OoHitstun */

/* ---- Misc: character randomness (the lab's RNG settings), frame advance, frame counter ---- */
int mu_tmce_osd_gnw_hammer(int roll);
int mu_tmce_osd_luigi_misfire(int chance);
float mu_tmce_osd_nana_throw(struct Fighter* fp, float roll);
int mu_tmce_osd_peach_fsmash(struct Fighter* fp, int count);
int mu_tmce_osd_peach_item(int kind);
int mu_tmce_osd_turnip(int sum);
int mu_tmce_osd_frame_advance(void);          /* gm/gmvs.c gm_AnyControllerPressedZ */
extern int mu_tmce_game_frame_counter;        /* the legacy events read it (console r13 -0x49A8) */
void mu_tmce_osd_frame_counter_init(void);    /* gm/gmvs.c fn_8016E730 */
void mu_tmce_osd_frame_counter_inc(void);     /* gm/gmvs.c fn_8016CFE0, at its end */
int mu_tmce_osd_lras_light_press(void);       /* gm/gmvs.c fn_8016CFE0: LRA+Start with light presses */

/* ---- the MexTK side (tmce/native/tmce_osd.c) ---- */
void mu_tmce_osd_start_melee(void);           /* right after TM-CE's OnStartMelee */
void mu_tmce_osd_hud_init(int canvas);        /* if/if_2FF2.c un_802FF498, with un_804A1F58.x0 */
void mu_tmce_osd_pause_stick_create(void);    /* gm/gmpause.c gm_801A0FEC */
void mu_tmce_osd_pause_stick_remove(void);    /* gm/gmpause.c gm_801A10FC */
void mu_tmce_osd_no_save_defaults(void);      /* gm/gmscmemcard.c: no save on the card */
void* mu_tmce_legacy_message(int ply, int timeout, int area, int window_id); /* Display Message 2.0 */
unsigned mu_tmce_osd_enabled(void);

#endif
