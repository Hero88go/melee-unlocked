/* Training Mode CE, native build: the Event Select Screen (TM-CE's "Event Select Screen" patches,
 * hand-written in shim/mu_tmce_ess.c). Every call is made only while mu_tmce_active().
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#ifndef MU_TMCE_ESS_H
#define MU_TMCE_ESS_H

#include <Runtime/platform.h>
#include <sysdolphin/baselib/forward.h>

struct MnEventData;

/* mn/mnevent.c */
s32 mu_tmce_ess_last_scroll(void);
bool mu_tmce_ess_short_page(struct MnEventData* data, s32 event_idx);
bool mu_tmce_ess_skip_row(struct MnEventData* data, s32 idx, s32 event_id);
void mu_tmce_ess_event_name(struct MnEventData* data, s32 idx, s32 event_id, f32 x, f32 y);
void mu_tmce_ess_description(struct MnEventData* data);
void mu_tmce_ess_page_arrows(void);
bool mu_tmce_ess_button_actions(HSD_GObj* ess_gobj);
bool mu_tmce_ess_jump_down(struct MnEventData* data);
bool mu_tmce_ess_jump_up(struct MnEventData* data);
void mu_tmce_ess_cursor_to_row(HSD_GObj* ess_gobj);
bool mu_tmce_ess_down_blocked(struct MnEventData* data);
void mu_tmce_ess_create_page_texts(void);
void mu_tmce_ess_remove_page_texts(void);

/* mn/mnmain.c */
void mu_tmce_ess_remove_event_texture(HSD_Archive* archive);

/* gm/gmmain_lib.c */
s32 mu_tmce_ess_load_score(s32 event);
void mu_tmce_ess_save_score(s32 event, s32 score);

/* gm/gmevent.c gm_801BEB8C (main session) */
u8 mu_tmce_ess_score_type(u8 event);

#endif
