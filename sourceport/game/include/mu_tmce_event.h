/* Training Mode CE, native build: event mode and its CSS.
 *
 * On the console these are the PowerPC patches under ASM/training-mode/Custom Events (CSS and SSS,
 * Custom Event Preload Behavior, A+B to Reset Event, Choose Any Character for Events), the event
 * dispatch at the head of Custom Event Code - Rewrite.asm and Hooks/OnCSSLoad.asm (ledger:
 * run-source/tmce-port/ledger_T1_event.md). Natively each is a call from the decomp statement its
 * address lands on (gm/gmevent.c, mn/mncharsel.c, gm/gm_1A3F.c), and every call site checks
 * mu_tmce_active() first, so with TM-CE off nothing here runs.
 *
 * Bodies: sourceport/game/shim/mu_tmce_event.c.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#ifndef MU_TMCE_EVENT_H
#define MU_TMCE_EVENT_H

struct CSSData;
struct StartMeleeData;
struct EventData;
struct gm_804D6900_t;
struct GameMode;

/* ---- gm/gmevent.c ---- */
/* onEnterCss: Load CSS + Preload CPU (returns the event's CSS type, the match_type for gm_801B06B0) */
unsigned char mu_tmce_ev_css_enter(struct CSSData* css);
/* onEnterCss: Restore Event CPU (in place of gm_801B06B0) */
void mu_tmce_ev_css_restore(struct CSSData* css, unsigned char css_type, unsigned char port);
/* onExitCss: Load Character ssm Files */
void mu_tmce_ev_css_load_ssm(struct CSSData* css);
/* onExitCss: Backup Event CPU (in place of gm_801B0730) */
void mu_tmce_ev_css_backup(struct CSSData* css, struct EventData* ev);
/* onExitCss: Load SSS For Certain Events */
void mu_tmce_ev_css_exit_sss(void);
/* onEnterVs: Custom Event Code - Rewrite prologue. 1 = a C event was set up (skip to the end of
 * onEnterVs), 0 = an original event (the legacy prologue ran, onEnterVs carries on). */
int mu_tmce_ev_enter_vs(struct StartMeleeData* md, int event_id, struct gm_804D6900_t** level_slot);
/* onExitVs: A+B to Reset Event (1 = A and B held: take the retry path) */
int mu_tmce_ev_ab_reset(void);
/* gm_801BEB8C: Custom Event Highscore Types (body in shim/mu_tmce_ess.c) */
unsigned char mu_tmce_ess_score_type(unsigned char event_id);

/* ---- gm/gm_1A3F.c runGameMode: Add SSS To Event Match Scene List ---- */
/* active: install the event mode's CSS/match/SSS table and the main menu's credits minor;
 * otherwise put the vanilla tables back if they were swapped */
void mu_tmce_ev_scene_lists(int active);

/* ---- mn/mncharsel.c mnCharSel_802640A0 ---- */
void mu_tmce_ev_css_event_name(void);                  /* Display Event Name on CSS */
void mu_tmce_ev_css_icons_init(struct CSSData* css);   /* Toggle CSS Icon Visibility */
int mu_tmce_ev_css_vs_text(void);                      /* Overwrite VS Text (1 = done, skip 8025BD30) */
void mu_tmce_ev_css_on_load(void);                     /* Hooks/OnCSSLoad.asm */

/* mn/mncharsel.c gives the shim the CSS state the icon toggle reads (file-private there) */
void mu_tmce_css_cursor0(unsigned char* hand_state, unsigned char* held_puck);
void mu_tmce_css_icon_show(int icon, int visible);

#endif
