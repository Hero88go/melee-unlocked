/* Training Mode CE, native build: the "Additional Codes" (ASM/Additional Codes in TM-CE's source).
 *
 * On the console each is a PowerPC patch; natively each is a call from the decomp statement its
 * address lands on, gated on mu_tmce_active() at the site (or, for the pass-through ones, inside
 * the hook, which then returns the vanilla value unchanged). With TM-CE off nothing here runs.
 * Ledger: run-source/tmce-port/ledger_T4.md. Bodies: sourceport/game/shim/mu_tmce_codes.c.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#ifndef MU_TMCE_CODES_H
#define MU_TMCE_CODES_H

struct Fighter;
struct HSD_GObj;
struct HitCapsule;
struct HurtCapsule;
struct HSD_PadStatus;
struct GameRules;
struct GamePrefs;
struct DevText;
struct _GXColor;

/* ---- Hitbox Visualizer, Display Ungrabbable Hurtboxes (ft/ftdrawcommon.c, lb/lbcollision.c) ---- */
/* Reverse Hitbox ID Order: the i-th drawn hitbox is x914[3 - i]; remembers i for the color. */
struct HitCapsule* mu_tmce_code_hitbox_order(struct HitCapsule* hitboxes, int i);
/* Main.asm, after the hitbox loop: Link's returning boomerang, Popo's up-B search radius. Also
 * ends the hitbox pass (the color code then treats hitboxes as items). */
void mu_tmce_code_hitbox_extras(struct Fighter* fp);
/* Color Changes Based On Damage: the color of a hitbox of the default element. */
struct _GXColor* mu_tmce_code_hit_color(struct HitCapsule* hit);
/* Main.asm (ungrabbable): while ftDrawCommon draws a fighter's hurtboxes, a vulnerable one that
 * cannot be grabbed is purple. */
void mu_tmce_code_fighter_hurt_pass(int on);
struct _GXColor* mu_tmce_code_hurt_color(struct HurtCapsule* hurt, struct _GXColor* color);

/* ---- fighters ---- */
/* Disable FoD Reflection: the reflection render callback, or none with 4 or more fighters. */
void* mu_tmce_code_fod_reflection(void* render_cb);
/* Hide OnHit GFX When Hitboxes Enabled: nonzero when the victim shows its hitboxes. */
int mu_tmce_code_hide_hit_gfx(struct Fighter* victim);
/* Hide GFX When Hitboxes Enabled (ef/efsync.c): nonzero when the effect's owner shows hitboxes. */
int mu_tmce_code_hide_gfx(struct HSD_GObj* owner);

/* ---- Better Camera (cm/camera.c) ---- */
void mu_tmce_code_cstick_pan(void);                 /* C-Stick Pans Camera (Camera_8002CB0C) */
int mu_tmce_code_pauser_alive(void);                /* SnapToOffscreenPlayers (Snap, Always Onscreen) */
void mu_tmce_code_unrestricted_camera(void);        /* Unrestricted Camera (Camera_SetUpPauseCamera) */

/* ---- develop mode (db/) ---- */
float mu_tmce_code_slow_camera(float normal, float slow); /* Slow Camera With R: normal or slow */
int mu_tmce_code_dpad_down_held(int port);          /* Rotate Camera with Dpad Down and C Stick */
const char* mu_tmce_code_anim_name(struct Fighter* fp); /* Display Animation Name for Special Moves */
void mu_tmce_code_physics_display(void);            /* Physics Display (db_Setup) */

/* ---- sites in files other agents own or outside this helper's directories (main session wires) ---- */
unsigned int mu_tmce_code_frame_advance_input(const struct HSD_PadStatus* pad); /* gm_AnyControllerPressedZ */
int mu_tmce_code_pause_during_start(void);          /* gm_DoPauseChecksAndRoutine / gm_DoUnpauseChecksAndRoutine */
int mu_tmce_code_respawn_point(int slot);           /* fn_8016719C: Neutral Respawn */
int mu_tmce_code_runback(int vanilla_next);         /* gmVsMelee_ExitVs: runback / random stage / CSS */
int mu_tmce_code_force_game_loop(void);             /* gm_801A4D34: Force Game Loop */
int mu_tmce_code_pad_update_paused(void);           /* gm_801A4D34: Pad - Update During Frame Advance */
void mu_tmce_code_default_rules(struct GameRules* rules, struct GamePrefs* prefs); /* boot */
int mu_tmce_code_sss_return_mode(int vanilla_mode); /* mnStageSel_Scene_OnFrame: L+R+A */
int mu_tmce_code_css_min_players(void);             /* fn_80262F44: Enable 1P in VS Mode No Time */
int mu_tmce_code_css_all_unplugged(void);           /* mnCharSel_CursorThink: Last Unplug Closes All */
void mu_tmce_code_css_rumble(int port, unsigned int pressed, unsigned char* shaking, float* x,
                             float* amplitude);     /* mnCharSel_CursorThink: Toggle Rumble From CSS */
void mu_tmce_code_pad_unplug(int port, signed char new_err, signed char old_err); /* HSD_PadRenewMasterStatus */
int mu_tmce_code_item_topn(struct HSD_GObj* item_gobj); /* mpLib_DrawSnapping: Always Draw TopN For Items */
void mu_tmce_code_chain_ecbs(void);                 /* mpLib_DrawSnapping end: Display Chain ECBs */

#endif
