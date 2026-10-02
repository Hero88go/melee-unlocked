/* Included ahead of every game and SDK source file in the native build (-include). It supplies what
 * the console compiler provided as intrinsics, and nothing else. */
#ifndef MU_NATIVE_H
#define MU_NATIVE_H

#ifndef TRUE
#define TRUE 1

/* Online gameplay rules, shim/mu_online_rules.c: per-match configuration set at online match start
 * (constant for the match) and the native sites of PreventWobbling, the frozen Stadium toggle,
 * InitPause and the Stadium codes. */
#define MU_WOBBLE_COUNT(fp) (*(u8*) ((u8*) &(fp)->mv + 0x44))    /* fp+0x2384 */
#define MU_WOBBLE_LAST(fp) (*(u16*) ((u8*) &(fp)->mv + 0x46))    /* fp+0x2386 */
struct MatchExitInfo;
void mu_online_rules_set(int mode, int frozen_stadium, int is_teams, int local_port);
void mu_online_rules_clear(void);
int mu_online_rules_mode(void);            /* Slippi online mode id, -1 when not set */
int mu_online_frozen_stadium(void);        /* match-state alt stage mode byte */
int mu_online_rules_is_teams(void);
int mu_online_rules_local_port(void);    /* -1 when not set */
int mu_ps_frozen_toggle(void);             /* online byte online, the replay's toggle in playback */

#endif
#ifndef FALSE
#define FALSE 0
#endif

/* Count leading zeros. The PowerPC instruction answers 32 for zero; the x86 one is undefined there. */
static inline int mu_cntlzw(unsigned int value) { return value ? __builtin_clz(value) : 32; }
#define __cntlzw(value) mu_cntlzw((unsigned int) (value))

/* The rest are real functions (shim/mu_math.c) because MetroTRK/intrinsics.h prototypes them by
 * these names, and a prototype must not meet a static inline. */
double mu_fabs(double x);
float mu_fabsf(float x);
float mu_fnmsubs(float a, float b, float c);
int mu_rlwinm(int value, int sh, int mb, int me);
int mu_rlwimi(int dst, int src, int sh, int mb, int me);
#define __fabs mu_fabs
#define __fabsf mu_fabsf
#define __fnmsubs mu_fnmsubs
#define __rlwinm mu_rlwinm
#define __rlwimi mu_rlwimi

/* dcbz: zero the 32-byte cache block holding base + offset. The THP decoder uses it to clear buffers. */
static inline void mu_dcbz(void* base, int offset)
{
    char* block = (char*) (((unsigned long long) (__UINTPTR_TYPE__) base + offset) & ~31ull);
    __builtin_memset(block, 0, 32);
}
#define __dcbz(base, offset) mu_dcbz((void*) (base), (int) (offset))

#include "math_native.h"
#include "mu_disc.h"

/* Deliver the handler the game installed for a hardware interrupt, the next time interrupts are on
 * (shim/mu_os.c). Used where the native build knows the hardware event has happened. */
void mu_raise_interrupt(int interrupt);

/* A wait the console left to the hardware: let the host run (retrace, disc, audio) and take what it
 * delivered. Placed in the game's busy-wait loops, which otherwise never see their flag change. */
void mu_poll(void);
#define HOST_POLL() mu_poll()

#include "mu_math_audit.h"

/* Player options the host passes live (MuHostApi.game_options): the native equivalents of the
 * Legacy port codes. Values match MU_GAME_OPTION_* in mu_host.h (checked in shim/mu_entry.c). */
#define MU_OPTION_NO_SCREEN_SHAKE 0x1u
#define MU_OPTION_PAL_STOCK_ICONS 0x2u
#define MU_OPTION_VANILLA 0x4u
/* Slippi's "Widescreen 16:9" code (shim/mu_gecko.c): every perspective camera widens by 320/219 as
 * it loads, with Slippi's offscreen-bubble and nametag values. Nonzero while the player has it on. */
#define MU_OPTION_WIDESCREEN 0x10000000u
int mu_widescreen(void);
/* True 16:9 is on: the renderer shows more to each side while the game's cameras stay Melee's own.
 * mu_wide_picture() is nonzero with either widescreen mode, for the one thing both need from the
 * game: a fighter in the added sides is drawn (ftdrawcommon.c). */
#define MU_OPTION_TRUE_WIDESCREEN 0x20000000u
int mu_wide_picture(void);
/* The language chosen in the PC settings, in the game's own numbering: 0 Japanese, 1 English, or -1
 * when the choice is left to the game's save (Options > Language). lblanguage.c answers with it
 * wherever the game reads its saved language; the save is not changed. */
#define MU_OPTION_LANGUAGE_MASK 0xC0000000u
#define MU_OPTION_LANGUAGE_SHIFT 30
int mu_language_override(void);
/* 20XX Tournament Edition features (shim/mu_te.c). mu_te(feature) is nonzero when the player turned
 * the feature on, 20XX TE is on, Tournament Mode allows it, and this is neither an online match nor
 * replay playback. Values match MU_GAME_OPTION_TE_* in mu_host.h. */
#define MU_OPTION_TE 0x10u
#define MU_OPTION_TE_TOURNAMENT 0x20u
#define MU_TE_HOLD_START_PAUSE 0x40u
#define MU_TE_FROZEN_STAGES 0x80u
#define MU_TE_NO_STAR_KO 0x100u
#define MU_TE_INFINITE_SHIELDS 0x200u
#define MU_TE_TAUNT_CANCEL 0x400u
#define MU_TE_UNFREEZE_ENDGAME 0x800u
#define MU_TE_FIXED_CAMERA 0x1000u
#define MU_TE_CPU_ZELDA_SHEIK 0x2000u
#define MU_TE_HANDICAP_STOCKS 0x4000u
int mu_te(unsigned int feature);
/* Training Lab (shim/mu_lab.c); values match MU_GAME_OPTION_LAB* in mu_host.h. */
#define MU_OPTION_LAB 0x8000u
#define MU_OPTION_LAB_BUBBLES 0x10000u
#define MU_OPTION_LAB_BUBBLES_ONLY 0x20000u
/* Training dummy behavior: DI while hit (none, toward you, away from you, random per hit) and
 * its first action once hitstun ends (none, jump, shield, spot dodge). */
#define MU_OPTION_LAB_DI_SHIFT 19
#define MU_OPTION_LAB_DI_MASK 0x180000u
#define MU_OPTION_LAB_REACT_SHIFT 21
#define MU_OPTION_LAB_REACT_MASK 0x600000u
/* Dummy tech on landing in hitstun or tumble: its own behavior, in place, toward you, away, random. */
#define MU_OPTION_LAB_TECH_SHIFT 23
#define MU_OPTION_LAB_TECH_MASK 0x3800000u
#define MU_OPTION_LAB_LOOP 0x4000000u   /* recording playback repeats until a D-pad press */
void mu_lab_frame_begin(void);
void mu_te_frame_begin(void);   /* 20XX TE per-frame match features (collision bubbles) */
/* Training Lab recording: nonzero when the lab drives this fighter's input this frame (the dummy
 * while recording or playing back; P1 stands still while its controller records the dummy). */
typedef struct MuLabInput {
    float lx, ly, cx, cy, l, r;
    unsigned int buttons;
} MuLabInput;
int mu_lab_input(void* fighter, MuLabInput* out);   /* fighter: Fighter* */

/* 20XX TE second feature word (shim/mu_te.c); values match MU_GAME_OPTION2_TE_* in mu_host.h. */
#define MU_TE2_NEUTRAL_SPAWNS 0x1u
#define MU_TE2_V100 0x2u
#define MU_TE2_DL64_QUIET 0x4u
#define MU_TE2_RESET_TOURNAMENT 0x8u
#define MU_TE2_FROZEN_TOGGLE 0x10u
#define MU_TE2_SKIP_RESULTS 0x20u
#define MU_TE2_RANDOM_MUSIC 0x40u
#define MU_TE2_SHIELD_COLORS 0x80u
#define MU_TE2_NO_SCREEN_RUMBLE 0x100u
#define MU_TE2_LCANCEL_FLASH 0x200u
#define MU_TE2_SPOOF_PLUGINS 0x400u
#define MU_TE2_LCANCEL_WHEELS 0x800u
#define MU_TE2_BUBBLES 0x1000u
#define MU_TE2_INPUT_DISPLAY 0x2000u
#define MU_TE2_CPU_SMART_DI 0x4000u
#define MU_TE2_COLOR_OVERLAYS 0x8000u
#define MU_TE2_HANDWARMERS 0x10000u
#define MU_TE2_STAGE_STRIKE 0x20000u
#define MU_TE2_LOCK 0x40000u   /* the settings cannot change until this is off */
/* shim/mu_te_debugmenu.c: 20XX TE's settings menu (Tournament Melee with TE on). */
void* mu_te_debug_menu(void);   /* the debug menu's root table, or NULL for the game's own */
void mu_te_debug_menu_save(void);
unsigned int mu_game_options2(void);
int mu_te2(unsigned int feature);   /* a second-word 20XX TE feature is in effect */
/* 20XX TE's general conveniences (unlocks, boot rules, no title demo, C-Stick in 1P, menu tweaks):
 * on whenever TE is on, offline, outside the online menus. */
int mu_te_general(void);
int mu_te_casual(void);               /* TE on, offline, Tournament Mode off */
int mu_te_menu_music(void);
void mu_test_seed_nametags(void);     /* tests only: MELEE_TEST_NAMETAGS */           /* Sound Test choice: 0 none, -1 song 0, else the song */
void mu_te_set_menu_music(int song);
int mu_te_handwarmers(void);
void mu_te_css_rules(int doubles);   /* TE boot rules once; Tournament Mode rules at every VS CSS */   /* hand-warmer mode is on and this is a 1-minute time match */

/* Content views (shim/mu_content.c): with a mod profile, the retail files online (Unranked,
 * Teams, Party, and Direct unless the player uses the mod there), the mod's files otherwise. */
int mu_content_mode(int online_mode);   /* 0-4 = the Slippi mode picked, -1 = offline; 1 = retail view */
int mu_content_vanilla(void);
/* After the previous major unloads, before the next major loads any files. */
void mu_content_begin_major(int mode);

/* m-ex content (shim/mu_mex.c): MxDt.dat read natively when the disc has it, active in the mod view. */
void mu_mex_boot(void);
int mu_mex_active(void);
/* m-ex puts its added fighters right after Roy and moves the special fighters (Master Hand to
 * Sandbag) behind them; per-kind tables in its PlCo.dat follow that order. The shift, 0 without m-ex. */
int mu_mex_special_kind_shift(void);
int mu_mex_parts_costume(int kind, int costume);   /* retail costume whose parts tables it uses */
int mu_mex_insignia(int ext);                      /* emblem frame of an external fighter id */
int mu_mex_costume_info(int ckind, int which);     /* 0 count, 1 red, 2 blue, 3 green; -1 retail */
/* The m-ex tables behind the added fighters (ids are m-ex internal ids, special fighters shifted). */
int mu_mex_fighter_internal_count(void);                        /* 0 without m-ex */
const char* mu_mex_fighter_file(int mex_internal);              /* "PlWf.dat"; NULL when none */
unsigned int mu_mex_fighter_function(int table, int mex_internal); /* console address, 0 empty */
int mu_mex_fighter_item(int mex_internal, int local);          /* item kind of article, -1 none */
/* The creation layer's tables (sourceport/game/akaneia/CREATION_LAYER_PLAN.md); NULL / -1 when none. */
const char* mu_mex_fighter_symbol(int mex_internal);            /* "ftDataWolf" */
const char* mu_mex_fighter_anim_file(int mex_internal);         /* "PlWfAJ.dat" */
int mu_mex_fighter_anim_count(int mex_internal);
int mu_mex_fighter_effect_file(int mex_internal);               /* index into mexData.effect.files */
int mu_mex_effect_file(int index, const char** file, const char** symbol);   /* 1 when usable */
const char* mu_mex_fighter_demo(int mex_internal, int which);   /* 0 result, 1 intro, 2 ending, 3 wait */
int mu_mex_fighter_ssm(int ext);                                /* sound bank id, by EXTERNAL id */
int mu_mex_ssm_count(void);                                     /* sound banks on the disc */
int mu_mex_ssm_bank(int id, const char** file, unsigned int* size, signed char row[4]);
int mu_mex_fighter_walljump(int mex_internal);
const char* mu_mex_fighter_name(int ext);
const char* mu_mex_fighter_result_file(int ext);
float mu_mex_fighter_result_scale(int ext);
int mu_mex_fighter_victory_theme(int ext);
int mu_mex_fighter_announcer(int ext);
int mu_mex_external_of_internal(int mex_internal);
int mu_mex_internal_of_external(int ext);
#ifdef MU_AKANEIA_FIGHTERS
int mu_mex_ak_costumes(int kind, int mex_internal);            /* usable costume count, 0 none */
#endif

/* Akaneia's added fighters, native (sourceport/game/akaneia/mu_ak_fighters.c). In the mod view each
 * fighter m-ex adds gets a native kind past the retail ones (Ft_Kind_Max + 1 on: Ft_Kind_None stays
 * "no fighter"); the per-kind tables have room for them (FT_KIND_TABLE_MAX, ft/forward.h) and the
 * registry fills those slots. No kind in that range exists in the retail view or on a retail disc,
 * so every hook below is inert there. */
#define MU_AK_KIND_BASE 0x22
#define MU_AK_KIND_SLOTS 16
#define MU_FT_KIND_CAP (MU_AK_KIND_BASE + MU_AK_KIND_SLOTS)
#define MU_AK_KIND(kind) ((unsigned) ((int) (kind) - MU_AK_KIND_BASE) < (unsigned) MU_AK_KIND_SLOTS)
/* Native character ids also keep the retail special kinds and None at their original values.
 * External m-ex ids stay at the disc / Slippi boundary, never in per-character native tables. */
#define MU_AK_CKIND_BASE 0x22
#define MU_AK_CKIND_SLOTS MU_AK_KIND_SLOTS
#define MU_CK_KIND_CAP (MU_AK_CKIND_BASE + MU_AK_CKIND_SLOTS)
#define MU_AK_CKIND(kind) ((unsigned) ((int) (kind) - MU_AK_CKIND_BASE) < (unsigned) MU_AK_CKIND_SLOTS)
int mu_ak_ckind_from_mex(int ext);      /* native character kind, -1 if the disc slot is absent */
int mu_ak_mex_external(int ckind);      /* disc / Slippi external id, -1 for an absent native slot */
int mu_ak_mex_internal(int kind);       /* disc internal id for a native fighter kind */
int mu_ak_ckind_from_kind(int kind);    /* added fighter kind -> native character kind, -1 otherwise */
/* The index of a fighter kind into the CPU tables of PlCo.dat (Fighter_804D64FC, read in
 * ft/ftcpuattack.c and ftCo_0A01.c). Those tables stay in the disc's order, so an added fighter
 * reads the entry of its m-ex internal id, as on the console. Every other kind is its own index,
 * and without the experimental fighters this is the kind itself. */
#ifdef MU_AKANEIA_FIGHTERS
#define MU_AK_CPU_KIND(kind) (MU_AK_KIND(kind) ? mu_ak_mex_internal(kind) : (int) (kind))
#else
#define MU_AK_CPU_KIND(kind) (kind)
#endif
/* The m-ex fighter hooks that have no retail per-kind table (m-ex ftFunction indexes). */
enum {
    MU_AK_HOOK_FLOAT = 31,        /* bool (*)(HSD_GObj*, int): enter float; returns entered */
    MU_AK_HOOK_DOUBLEJUMP = 32,   /* void (*)(HSD_GObj*): enter the double jump */
    MU_AK_HOOK_ZAIR = 33,         /* void (*)(HSD_GObj*): enter the tether (Z-air) */
    MU_AK_HOOK_LANDING = 34,      /* void (*)(HSD_GObj*): entered a grounded state */
    MU_AK_HOOK_FSMASH = 35,       /* void (*)(HSD_GObj*): enter the forward smash */
    MU_AK_HOOK_USMASH = 36,       /* void (*)(HSD_GObj*): enter the up smash */
    MU_AK_HOOK_DSMASH = 37,       /* void (*)(HSD_GObj*): enter the down smash */
};
struct HSD_GObj;
void mu_ak_apply(void);                  /* at each content view change (shim/mu_mex.c) */
void* mu_ak_hook(int kind, int hook);    /* the callback, NULL when the slot is empty */
int mu_ak_call(int kind, int hook, struct HSD_GObj* gobj);   /* calls a (HSD_GObj*) hook; 1 if run */
int mu_ak_css_selectable(int ext);       /* an added fighter the character select may offer */
int mu_ak_ckind_selectable(int ckind);   /* the same, by native character kind */
#ifdef MU_AKANEIA_FIGHTERS
/* The added fighters' sounds (akaneia/common/mu_ak_sound.c, lb/lbaudio_ax.c). */
struct Fighter;
struct Item;
int mu_ak_sound_bank(int ckind);                         /* sound bank of an added kind, -1 none */
int mu_ak_sound_request(int bank);                       /* ask for an added bank at the next load */
int mu_ak_sound_id(struct Fighter* fp, int id);          /* 5000+n of fp's bank -> the game's id */
int mu_ak_sound_item_id(struct Item* ip, int id);        /* the same, by the item's creator */
void mu_ak_sound_view(int mod_view);                     /* at each content view change */
#endif
/* Articles of the added fighters: item kinds past the retail ones. */
int mu_ak_article(int item_kind, void** article, void** logic);   /* 1 when item_kind is one */
void mu_ak_article_store(int item_kind, void* article);
unsigned int mu_game_options(void);
/* shim/mu_hud_scale.c: the HUD size sliders, applied from the stock (stock=1) and damage (0) draw
 * callbacks. */
struct HSD_GObj;
void mu_hud_scale(struct HSD_GObj* gobj, int stock);
/* shim/mu_hud_scale.c: each drawn frame, the players' tag positions, damage and stocks for the
 * host's overlays (nicknames above the fighters). */
void mu_hud_report(void);
unsigned int mu_mod_flags(void);
#ifndef MU_MOD_ASSETS_PRESENT
#define MU_MOD_ASSETS_PRESENT 0x1u
#endif
unsigned int mu_music_volume(void);   /* MuHostApi.music_volume, 0-100 */

/* Legacy runs Slippi's "General Codes" and "Lagless FoD" on every boot; the native game carries the
 * same behavior as C at each patched site (shim/mu_gecko.c holds the shared parts). Nonzero unless
 * the host asked for the vanilla game. Latched at boot. */
int mu_general_codes(void);
void mu_general_codes_boot(void);

/* UCF 0.84 (part of the General Codes). The pad-buffer code keeps, per controller port, the last
 * four raw stick samples and a fast-down-flick counter; the other UCF codes read them. */
struct Fighter;
void mu_ucf_pad_buffer(struct Fighter* fp);
void mu_ucf_dashback(struct Fighter* fp);
float mu_ucf_squatrv_threshold(struct Fighter* fp, float vanilla);
int mu_ucf_sdi(struct Fighter* fp);
int mu_ucf_shield_sdi(struct Fighter* fp);
int mu_ucf_shield_drop(struct Fighter* fp);
int mu_ucf_shield_drop_extended(struct Fighter* fp);
int mu_ucf_tumble_wiggle(struct Fighter* fp);
int mu_ucf_enabled(void);   /* General Codes on, and the replay (if any) carries UCF 0.84 */

/* Slippi replay playback (--replay), shim/mu_replay.c: the native form of Slippi's playback codes,
 * called from the sites those codes patch. Codes are MU_REPLAY_CODE_* in mu_host.h. */
#define MU_RC_UCF084 0x00000001u
#define MU_RC_NEUTRAL_SPAWN 0x00000002u
#define MU_RC_FREEZE_GLITCH 0x00000004u
#define MU_RC_INIT_STAGE_DATA 0x00000008u
#define MU_RC_INIT_PLAYER_DATA 0x00000010u
#define MU_RC_OFFSCREEN_DAMAGE 0x00000020u
#define MU_RC_DEAD_UP_FALL 0x00000040u
#define MU_RC_FD_BG_SEED 0x00000080u
#define MU_RC_PS_ZERO_BUFFER 0x00000100u
#define MU_RC_PS_IS_VALID 0x00000200u
#define MU_RC_PS_FROZEN_CHECK 0x00000400u
#define MU_RC_PS_FILE_LOAD 0x00000800u
#define MU_RC_WHISPY_FIX 0x00001000u
#define MU_RC_NANA_DETERMINISM 0x00002000u
#define MU_RC_PS_MONITOR 0x00004000u
#define MU_RC_PREVENT_WOBBLING 0x00008000u
struct StartMeleeData;
int mu_replay_on(void);
int mu_replay_code(unsigned int code);     /* playback and the replay carries the code */
int mu_replay_allows(unsigned int code);   /* not playback, or the replay carries the code */
int mu_replay_frame_index(void);
void mu_replay_boot_mode(unsigned char* mode);
void mu_replay_prepare_scene(void);
void mu_replay_start_melee(struct StartMeleeData* data);
void mu_replay_scene_think(int match_result);
int mu_replay_terminated(void);
int mu_replay_stock_steal(int pad_port);
void mu_replay_input(struct Fighter* fp);
void mu_replay_post_frame(struct Fighter* fp);
void mu_replay_match_exit(void);
int mu_offscreen_damage_zone(struct Fighter* fp);
int mu_replay_ps_frozen(void);
void mu_refresh_part_matrices(struct Fighter* fp);
/* The native recording (Slippi's Recording codes), shim/mu_replay.c. */
void mu_replay_online_start(struct StartMeleeData* data, const unsigned char* msrb);   /* SendGameInfo, online */
void mu_replay_flush_frame(void);                  /* FlushFrameBuffer: the frame bookend */
void mu_replay_lcancel_reset(struct Fighter* fp);  /* ResetLCancelStatus */
void mu_replay_lcancel_landing(struct Fighter* fp);   /* GetLCancelStatus */
void mu_replay_record_whispy(int direction);       /* SendDreamlandInfo */
struct HSD_JObj;
void mu_replay_record_fountain(const struct HSD_JObj* platform, float height);   /* SendFountainInfo */
void mu_replay_record_stadium(int state, int kind);   /* SendStadiumInfo */
int mu_online_pending(void);                       /* shim/mu_online.c */
void mu_online_record_state(int* stable_finalized, int* game_over, int* disconnected);
struct gm_80479D58_t* mu_gm_engine_state(void);    /* gm/gmscene.c */

/* Rollback snapshot exclusions (shim/mu_exclusions.c). A file that owns state outside the
 * simulation (audio driver and mixer state, host plumbing, renderer bridges) names it with
 * MU_EXCLUSIONS(name, MU_EXCLUDE(sym), ...); a rename is then a compile error, not a silent gap.
 * Laid out as MuStateRegion. */
typedef struct MuExclusion {
    void* address;
    unsigned int size;
} MuExclusion;
#define MU_EXCLUDE(sym) { (void*) &(sym), (unsigned int) sizeof(sym) }
#define MU_EXCLUSIONS(name, ...)                                                   \
    unsigned int mu_exclusions_##name(const MuExclusion** out)                     \
    {                                                                              \
        static const MuExclusion list[] = { __VA_ARGS__ };                         \
        *out = list;                                                               \
        return (unsigned int) (sizeof list / sizeof list[0]);                      \
    }

/* Slippi online play, shim/mu_online.c: the native form of Slippi's online codes, called from the
 * sites those codes patch. Every hook does nothing unless an online match is running. */
struct PADStatus;
int mu_online_active(void);
unsigned int mu_online_codes(void);   /* MU_RC_* an online match plays with (0 offline) */
int mu_online_is_test_run(void);
int mu_online_engine_gate(int* pad_queue_count);   /* ForceEngineOnRollback */
void mu_audio_idle(void);                          /* audio blocks at their times during the frame wait (mu_os.c) */
void mu_online_frame_begin(void);                  /* StartEngineLoop */
int mu_online_frame_end(void);                     /* LoopEngineForRollback: 1 re-run, 2 done */
int mu_online_skip_pad_read(void);                 /* SkipNewInputFetchOnRollback */
int mu_online_pad_renew(struct PADStatus* stat);   /* TriggerSendInput: nonzero skips the sample */
int mu_online_pad_alarm_gate(void);                /* PreventPadAlarmDuringRollback */
void mu_online_after_pad_renew(void);              /* ForceInputRefetchOnAdvance */
void mu_online_start_melee(struct StartMeleeData* data);   /* InitOnlinePlay */
void mu_online_match_exit(void);
void mu_online_scene_loop_exit(void);              /* the scene loop left, maybe mid rollback */

/* Rollback-safe audio and rumble for online play, shim/mu_online_audio.c: sounds a re-executed
 * frame starts again adopt the voices its previous execution started; music and rumble requests
 * of a re-simulation wait for its end. Every hook does nothing unless an online match is running. */
enum { MU_SFX_NO_TICKET = -1, MU_SFX_ADOPTED = -2, MU_MUSIC_START = 1, MU_MUSIC_STOP = 2 };
int mu_online_resim_active(void);
void mu_online_audio_match_start(int local_index, int input_source);
void mu_online_audio_frame_begin(void);
void mu_online_audio_frame_end(void);
void mu_online_audio_rollback_end(void);
void mu_online_audio_match_exit(void);
void* mu_online_audio_state_address(void);
unsigned int mu_online_audio_state_size(void);
int mu_online_sfx_begin(int sound_id, int track, int* adopted_id);   /* PreventDuplicateSounds */
void mu_online_sfx_started(int ticket, int instance);                /* AssignSoundInstanceId */
int mu_online_sfx_protected(int instance);                           /* NoDestroyVoice */
int mu_online_music_defer(int op, const char* path, int volume, int track);   /* StartSong, Stop */

/* Music through the host jukebox, as the static recomp plays it (shim/mu_audio.c): nonzero unless
 * --vanilla-game. The sites are Slippi's MuteMusic, PreventMusicAlarm, StartSong, Stop and
 * VolumeChange codes. */
int mu_jukebox_music(void);
void mu_jukebox_play(int entrynum);            /* StartSong: the song's disc location to the jukebox */
void mu_jukebox_stop(void);                    /* Stop */
void mu_jukebox_volume(unsigned char volume);  /* VolumeChange: the music group volume, 0-254 */
int mu_online_motor_port(int chan, unsigned int command);            /* HandleRumble: -1 drops */
void mu_pad_motor(int port, int on);                                 /* shim/mu_pad.c */

/* Slippi online menus, shim/mu_slippi_menu.c (contract: run-source/s6-core-api.md). Everything is
 * inert unless the host set MU_OPTION_SLIPPI_MENUS (latched at boot). */
#define MU_OPTION_SLIPPI_MENUS 0x8u
enum { MU_SLP_MODE_RANKED, MU_SLP_MODE_UNRANKED, MU_SLP_MODE_DIRECT, MU_SLP_MODE_TEAMS,
       MU_SLP_MODE_PARTY };
enum { MU_SLP_OPT_LOGIN = 5, MU_SLP_OPT_LOGOUT = 6, MU_SLP_OPT_UPDATE = 7, MU_SLP_OPT_COUNT = 8 };
enum { MU_SLP_APP_LOGGED_OUT, MU_SLP_APP_LOGGED_IN, MU_SLP_APP_UPDATE };
enum { MU_SLP_MM_IDLE, MU_SLP_MM_INITIALIZING, MU_SLP_MM_MATCHMAKING, MU_SLP_MM_OPPONENT_CONNECTING,
       MU_SLP_MM_CONNECTION_SUCCESS, MU_SLP_MM_ERROR };
enum { MU_SLP_STATE_CSS, MU_SLP_STATE_SSS, MU_SLP_STATE_VS, MU_SLP_STATE_RESULTS,
       MU_SLP_STATE_SPLASH };
enum { MU_SLP_WINNER_NULL = -1, MU_SLP_WINNER_LOST = 0, MU_SLP_WINNER_WON = 1 };
enum { MU_SLP_CB_NONE, MU_SLP_CB_CODE_LOCK_IN, MU_SLP_CB_SSS_LOCK_IN };
enum { MU_SLP_STAGE_UNSET = 0, MU_SLP_STAGE_PICK = 1, MU_SLP_STAGE_RANDOM = 3 };

typedef struct MuSlippiMenuState {
    unsigned char mode, app_state, force_clear, code_entry;
    unsigned char pause;
    signed char is_winner;
    unsigned char chose_stage, name_entry_index_flag;
    unsigned char callback;
    unsigned char team_idx;
    unsigned char frozen_toggle;
    unsigned char game_local_index;
    unsigned char sss_name_state;
    unsigned char css_scratch[32];
    unsigned char code_scratch[64];
} MuSlippiMenuState;

typedef struct MuMatchState {
    unsigned char connection_state, local_ready, remote_ready, local_index, remote_index;
    unsigned int rng_offset;
    unsigned char delay_frames;
    unsigned char user_chat_msg, opp_chat_msg, chat_player_index, user_rank, opp_rank;
    char local_name[31];
    char names[4][31];
    char opp_name[31];
    char codes[4][10];
    char uids[4][29];
    char error[241];
    unsigned char game_info[312];
    char match_id[51];
    unsigned char alt_stage_mode;
    unsigned char raw[962];
} MuMatchState;

typedef struct MuSlippiSelections {
    unsigned char team_id, char_id, char_color, char_opt;
    unsigned short stage_id;
    unsigned char stage_opt, online_mode, alt_stage_mode;
} MuSlippiSelections;
typedef struct MuCodeSuggestionReq {
    unsigned char input[24];
    unsigned char length;
    unsigned int index;
    unsigned char scroll, mode;
} MuCodeSuggestionReq;
typedef struct MuCodeSuggestion {
    unsigned char found;
    unsigned char text[24];
    unsigned char length;
    unsigned int index;
} MuCodeSuggestion;

int mu_slippi_menus_enabled(void);
MuSlippiMenuState* mu_slippi_state(void);
int mu_slippi_cmd(unsigned int cmd, const void* payload, unsigned int size, void* reply,
                  unsigned int reply_cap, unsigned int* reply_size);
int mu_slippi_fetch_app_state(void);                      /* B9 */
int mu_slippi_app_state(void);
const char* mu_slippi_user_name(void);
const char* mu_slippi_user_code(void);
int mu_slippi_load_match_state(MuMatchState* out);        /* B3; 0 on success */
const MuMatchState* mu_slippi_match_state(void);
void mu_slippi_find_opponent(int mode, const unsigned char* code_sjis18);   /* B4 */
void mu_slippi_set_selections(const MuSlippiSelections* s);                /* B5 */
void mu_slippi_cleanup_connections(void);                                   /* BA */
unsigned int mu_slippi_new_seed(void);                                      /* BC */
void mu_slippi_send_chat(int message_id);                                   /* BB */
int mu_slippi_code_suggestion(const MuCodeSuggestionReq* in, MuCodeSuggestion* out);   /* BE */
int mu_slippi_in_online_mode(void);
int mu_slippi_online_state(void);
int mu_slippi_on_online_css(void);
int mu_slippi_on_online_sss(void);
int mu_slippi_in_online_game(void);
int mu_slippi_on_online_results(void);
int mu_slippi_on_online_splash(void);
int mu_slippi_zelda_is_sheik(void);
void mu_slippi_splash_init(void);
unsigned char mu_slippi_team_costume(int team, int ckind);
int mu_slippi_option_unlocked(int sel);
int mu_slippi_first_unlocked(void);
void mu_online_arm(int mode, int input_port);             /* shim/mu_online.c */
/* Hook-site entry points (core's own decomp sites). */
struct HSD_GObj;
void mu_slippi_scene_install(void);                       /* gm_801A4510 */
unsigned char mu_slippi_boot_mode(unsigned char mode);    /* bootOnLeave */
void mu_slippi_menu_prep(void);                           /* gmmenumode onEnter */
int mu_slippi_menu_enter(int previous_mode, unsigned char* menu_kind, unsigned char* hovered);
void mu_slippi_main_menu_loaded(void);                    /* end of mnMain_Scene_OnEnter */
void mu_slippi_switch_to_online_menu(void);               /* 1P menu, row 2 */
int mu_slippi_menu_clear_check(int menus_differ);         /* fn_8022AFEC */
void mu_slippi_online_menu_think(struct HSD_GObj* gp);
int mu_slippi_css_text_heap(void);                        /* preloadState: 0 = vanilla size */
void mu_mnmain_install_online_menu(void);                 /* mn/mnmain.c */

#endif

/* Functions where the console compiler fused no single-precision multiply-add: GCC must not either
 * (tools/fma_exact/tag_no_contract.py lists them from the retail code and tags their definitions). */
#define MU_NO_CONTRACT __attribute__((optimize("fp-contract=off")))

/* One product rounded on its own before the surrounding sum: the console never fused a product
 * into a sqrtf argument (its sqrtf is inline; the argument is a variable first). GCC's
 * -ffp-contract=on only fuses inside one expression, so a statement expression blocks it
 * (tools/fma_exact/wrap_sqrt_args.py places these). */
#define MU_P(x) ({ __typeof__(x) mu_p_ = (x); mu_p_; })

/* An explicit console fmadds (exact product, one rounding in double, then to single). */
#define MU_FMADDS(a, b, c) ((float) __builtin_fma((double) (float) (a), (double) (float) (b), (double) (float) (c)))

/* Slippi online menus: VS splash, in-game names, online results (shim/mu_slippi_splash.c). */
#ifndef MU_SLIPPI_SPLASH_DECLS
#define MU_SLIPPI_SPLASH_DECLS
struct HSD_JObj;
struct HSD_Text;
int mu_slippi_splash_active(void);                              /* 8:4 with the flag on */
void mu_slippi_splash_text(void);                               /* gm_Scene_IntroEasy_OnEnter */
unsigned int mu_slippi_splash_announce_char(unsigned int vanilla);   /* fn_80184AB8 */
void mu_slippi_splash_hide_letters(struct HSD_JObj* jobj);      /* fn_80184AB8 */
int mu_slippi_splash_skip_stage_number(void);                   /* fn_80184AB8 */
int mu_slippi_splash_hide_stage(struct HSD_JObj* jobj);         /* fn_8018504C: 1 = return */
void mu_slippi_ingame_hud_init(void);                           /* ifStatus_802F665C */
struct HSD_Text* mu_slippi_hud_text(void);                      /* disconnect/desync text */
int mu_slippi_hud_canvas(void);
int mu_slippi_digits_no_kerning(void);                          /* HSD_SisLib_803A67EC */
void mu_slippi_results_control_all_panels(void);                /* fn_80179350 */
unsigned char mu_slippi_results_slot_type(int port, unsigned char slot_type);  /* fn_80178BB4 */
#endif

/* Slippi online menus: connect-code entry and stage select (shim/mu_slippi_sss.c). */
#ifndef MU_SLIPPI_SSS_DECLS
#define MU_SLIPPI_SSS_DECLS
void mu_slippi_sss_set_slpcss(void* root);   /* CSS: the slpCSS symbol after SceneLoadCSS loads it */
void* mu_slippi_sss_slpcss(void);            /* NULL until the CSS registered it */
int mu_slippi_sss_frozen(void);              /* frozen Stadium cave byte */
void mu_slippi_sss_frozen_flip(void);        /* CursorOnHoverStadium Z */
void mu_slippi_sss_lock_in(int stage_behavior);          /* FN_TX_LOCK_IN: -2 random, -1 unset, 0+ pick */
void mu_slippi_sss_find_match(void);                     /* FN_TX_FIND_MATCH (B4, typed code) */
void mu_slippi_sss_run_callback(int stage_behavior);     /* r13-0x5018 lock-in callback */
#endif

/* Slippi online menus: online character select (shim/mu_slippi_css.c; hooks in mn/mncharsel.c). */
#ifndef MU_SLIPPI_CSS_DECLS
#define MU_SLIPPI_CSS_DECLS
struct CSSData;
struct HSD_JObj;
int mu_slippi_css_online(void);              /* flag on and major 8 state 0 */
int mu_slippi_css_teams(void);               /* ... and the Teams mode */
void mu_slippi_css_enter(struct CSSData** css, unsigned char* char_chosen, signed char* ctrl_port,
                         unsigned char* scene_request, signed char* name_entry_port,
                         struct HSD_JObj** single_menu_root);   /* SceneLoadCSS */
void mu_slippi_css_poll(void);               /* FetchMatchInfo: B3 per cursor think */
int mu_slippi_css_inputs(unsigned int trigger);   /* HandleInputsOnCSS: 1 = both ready, start */
void mu_slippi_css_text_init(void);          /* LoadCSSText */
int mu_slippi_css_skip_return_sound(void);   /* SkipReturnToCssSound: 1 = skip the voice */
int mu_slippi_css_block_unselect(void);      /* PreventA/BPressCharUnselect */
int mu_slippi_css_block_costume_change(void);   /* PreventColorChange */
int mu_slippi_css_local_ready(void);         /* MSRB local lock-in of the last poll */
int mu_slippi_css_team_idx(void);            /* CSSDT team index (1 red, 2 blue, 3 green) */
void mu_slippi_css_set_team_idx(int team);   /* also the injection byte kept across loads */
#endif

/* Slippi online menus: online CSS chat (shim/mu_slippi_chat.c; hooks in mn/mncharsel.c). */
#ifndef MU_SLIPPI_CHAT_DECLS
#define MU_SLIPPI_CHAT_DECLS
void mu_slippi_chat_enter(void);             /* end of mnCharSel_Scene_OnEnter: C3 */
void mu_slippi_chat_frame(void);             /* end of mnCharSel_Scene_OnFrame: chat listeners */
const MuMatchState* mu_slippi_css_msrb(void);   /* the CSS data table's MSRB (last poll) */
void* mu_slippi_css_slpcss(void);            /* slpCSS symbol of this CSS load, or NULL */
void mu_slippi_css_set_chat_open(int open);  /* CSS data table chat-window byte */
int mu_slippi_css_scene_request(void);       /* mnCharSel_804D6CF6 */
int mu_slippi_css_port(void);                /* mnCharSel_804D6CF0 as a byte */
int mu_slippi_css_zelda_icon_reset(void);    /* OnEnter: 1 = apply the OnLoad/OnUnload icon kind */
unsigned mu_slippi_online_load_count(void);  /* online major OnLoad count (mu_slippi_menu.c) */
#endif

/* The explicit practice bridge owns the pending destination until the origin mode exits. */
int mu_practice_resolve_pending_mode(int requested);
void mu_practice_enter_mode(int mode);

/* Training Mode CE, native build (sourceport/game/tmce, hooks in shim/mu_tmce.c and the sites below). */
#ifndef MU_TMCE_DECLS
#define MU_TMCE_DECLS
extern int mu_tmce_stadium_override;         /* gr/grpstadium.c: the lab picks the next transformation */
extern int mu_tmce_stadium_transformation;
void mu_tmce_boot(void);                     /* gm/gm_1A3F.c mode load: TM/eventMenu.dat, OnFileLoad, OnBoot */
int mu_tmce_enabled(void);                   /* TM-CE files present, vanilla base, not online */
void mu_tmce_on_scene_change(void);         /* TM-CE OnSceneChange / OnStartMelee hooks */
void mu_tmce_on_start_melee(void);
int mu_tmce_active(void);                    /* enabled and loaded: the hooks run */
int mu_test_classic_stage(void);             /* MELEE_TEST_CLASSIC_STAGE, -1 when unset (tests only) */
#endif
