/* The boundary between the game and the machine it runs on.
 *
 * The game is built from the decompiled sources into its own library with its own compiler, so it
 * cannot link against the host directly. Instead the host hands it one table of function pointers
 * (MuHostApi) and takes one back (MuGameApi), both plain C and both versioned. Everything the
 * console's hardware used to do for the game arrives through the first table; everything the host
 * needs to drive the game goes through the second.
 *
 * Shared verbatim by the MSVC host and the GCC game library. No C++, no host types.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MU_HOST_H
#define MU_HOST_H

#include <stdint.h>
#include "mu_native_pose.h"
#include "mu_lcancel_view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MU_HOST_API_VERSION 18
#define MU_GAME_API_VERSION 6
#define MU_SLIPPI_RESPONSE_CAPACITY 4096u

/* Values are raw IEEE-754 bits so the two builds can be compared exactly. */
typedef struct MuFighterState {
    uint32_t present, stocks, action, anim_frame;
    uint32_t pos_x, pos_y, pos_z, vel_x, vel_y, vel_z;
    uint32_t percent, facing;
} MuFighterState;

/* scene is GameRouting::curr_state_id (the scene graph's local id, scoped to the current game
 * mode; the existing field). scene_major is GameRouting::curr_mode (::GameModeKind, e.g. GM_VS),
 * added so a script can wait for a specific game mode without also matching its scene ids against
 * a different mode's states. match_frame is VsSceneController::state.frame_count
 * (gm_GetFrameCount()): zero outside a VS-family match, counting from 1 once the match's fighters
 * start simulating, so it lines up native and vanilla traces regardless of how many retraces each
 * build spent booting or sitting in menus to get there. */
typedef struct MuStatePod {
    uint32_t rng, scene;
    MuFighterState player[6];
    uint32_t scene_major, match_frame;
} MuStatePod;

/* The console's 40.5 MHz timebase, which the host advances deterministically. */
#define MU_TB_HZ 40500000ull

/* Why the game stopped. */
enum { MU_STOP_EXIT = 0, MU_STOP_RESET = 1, MU_STOP_PANIC = 2 };

/* One controller, laid out as the game's PADStatus is. */
typedef struct MuPadStatus {
    uint16_t button;
    int8_t stick_x, stick_y, sub_x, sub_y;
    uint8_t trigger_l, trigger_r, analog_a, analog_b;
    int8_t err;
} MuPadStatus;

typedef void (*MuDiscDone)(int32_t result, void* user);
typedef void (*MuCardDone)(int32_t result, void* user);

/* What a scripted run wants a VS match to be, so the sweep does not have to
 * drive the character and stage screens by cursor position for every
 * combination. The menus still run; only the result is forced. */
typedef struct MuMatchPlayer {
    int32_t kind;        /* CharacterKind, or negative for an empty slot */
    int32_t cpu;         /* 0 human, 1 CPU */
    int32_t cpu_level;   /* 1..9, ignored for a human */
    int32_t costume;
} MuMatchPlayer;

typedef struct MuMatchOverride {
    int32_t stage;       /* the rules' stage kind */
    MuMatchPlayer players[6];
} MuMatchOverride;

/* Version 9: Slippi replay playback (--replay). The host parses the .slp and answers what Slippi's
 * playback codes ask Dolphin for over EXI (game info, frame data, stock steal); the game applies it
 * at the sites those codes patch and hands back Slippi pre-frame and post-frame payloads, which the
 * host writes as a new .slp. */
typedef struct MuReplayStart {
    uint8_t game_info[0x138];   /* the Game Start event's game info block: big-endian, console layout */
    uint32_t random_seed;       /* the game's seed at match start */
    uint32_t codes;             /* MU_REPLAY_CODE_*: the gameplay codes in the replay's code list */
    uint8_t is_pal, preload_ps, frozen_ps, resync;
    uint8_t ps_frozen_toggle;   /* the data byte of the replay's IngameCheckIfFrozen code */
    uint8_t reserved[3];
} MuReplayStart;

/* One character's record in a fetched frame (Dolphin's prepareCharacterFrameData). */
typedef struct MuReplayCharacter {
    uint32_t present;
    uint32_t random_seed;
    float joystick_x, joystick_y, cstick_x, cstick_y, trigger;
    uint32_t buttons;           /* processed buttons, as the game held them */
    float x, y, facing;
    uint32_t action_state;
    float percent;              /* NaN bits 0xFFFFFFFF when the replay has none */
    uint8_t joystick_x_raw, joystick_y_raw, cstick_x_raw, cstick_y_raw;
} MuReplayCharacter;

enum { MU_REPLAY_WAIT = 0, MU_REPLAY_CONTINUE = 1, MU_REPLAY_TERMINATE = 2 };

typedef struct MuReplayFrame {
    int32_t result;             /* MU_REPLAY_* */
    uint32_t seed_exists, seed; /* the frame's starting seed (Frame Start event) */
    MuReplayCharacter character[4][2];   /* [port][0 leader, 1 follower] */
} MuReplayFrame;

/* Gameplay codes a replay's list can carry (the Dolphin-denylisted list, by injection address). */
#define MU_REPLAY_CODE_UCF084            0x00000001u
#define MU_REPLAY_CODE_NEUTRAL_SPAWN     0x00000002u
#define MU_REPLAY_CODE_FREEZE_GLITCH     0x00000004u
#define MU_REPLAY_CODE_INIT_STAGE_DATA   0x00000008u
#define MU_REPLAY_CODE_INIT_PLAYER_DATA  0x00000010u
#define MU_REPLAY_CODE_OFFSCREEN_DAMAGE  0x00000020u
#define MU_REPLAY_CODE_DEAD_UP_FALL      0x00000040u
#define MU_REPLAY_CODE_FD_BG_SEED        0x00000080u
#define MU_REPLAY_CODE_PS_ZERO_BUFFER    0x00000100u
#define MU_REPLAY_CODE_PS_IS_VALID       0x00000200u
#define MU_REPLAY_CODE_PS_FROZEN_CHECK   0x00000400u
#define MU_REPLAY_CODE_PS_FILE_LOAD      0x00000800u
#define MU_REPLAY_CODE_WHISPY_FIX        0x00001000u
#define MU_REPLAY_CODE_NANA_DETERMINISM  0x00002000u
#define MU_REPLAY_CODE_PS_MONITOR        0x00004000u
#define MU_REPLAY_CODE_PREVENT_WOBBLING  0x00008000u
#define MU_REPLAY_CODE_UCF_OTHER         0x00010000u   /* a UCF version other than 0.84 */

/* Version 13, test harness only (--online-test): an online match the game enters without its
 * menus, the way replay playback does. */
typedef struct MuOnlineMatch {
    uint8_t game_info[0x138];   /* the Game Start event's game info block: big-endian, console layout */
    uint32_t random_seed;       /* the game's seed at match start */
    uint8_t local_port;         /* 0-3: the controller port the local player's pad is read from */
    uint8_t delay_frames;       /* local input delay */
    uint8_t mode;               /* Slippi mode id: 0 Ranked, 1 Unranked, 2 Direct, 3 Teams, 4 Party */
    uint8_t reserved;
} MuOnlineMatch;

/* Native-only HSD_PObj scope event used by the optional render-stream audit. */
typedef struct MuNativeRenderScopeEvent {
    uint32_t sequence;
    uint32_t scope_id;
    uint32_t phase; /* 1: begin PObj; 2: end PObj */
    uint32_t reserved;
} MuNativeRenderScopeEvent;

typedef struct MuHostApi {
    uint32_t version;   /* MU_HOST_API_VERSION */

    /* ---- diagnostics ---- */
    void (*log)(const char* text);
    void (*panic)(const char* file, int32_t line, const char* message);

    /* ---- time ----
     * ticks() is the console timebase: monotonic, 40.5 MHz, and under --time-base it is a pure
     * function of the frame count so two runs agree. boot_time() is the console epoch value the
     * game's calendar functions work from. */
    uint64_t (*ticks)(void);
    uint64_t (*boot_time)(void);

    /* ---- the point where the host gets to run ----
     * The game is single-threaded and spins waiting for things the hardware used to finish on its
     * own, so every one of those spins calls poll(): the host delivers disc completions, audio,
     * alarms and the retrace here, in the order the console's interrupts would have. */
    void (*poll)(void);

    /* ---- graphics ----
     * The game's own GX library builds the command stream, exactly as it did on the console; these
     * bytes are that stream. The host decodes them into draws (the same decoder the recompiled
     * build uses). */
    void (*gx_fifo)(const uint8_t* data, uint32_t size);
    /* The video interface: which framebuffer to show, and where in the field we are. */
    void (*vi_configure)(uint32_t width, uint32_t height, uint32_t interlaced);
    void (*vi_set_next_framebuffer)(void* xfb);
    void (*vi_flush)(void);
    uint32_t (*vi_retrace_count)(void);
    uint32_t (*vi_next_field)(void);
    void (*vi_set_black)(int32_t black);
    /* Blocks, running poll(), until the retrace count moves. */
    void (*vi_wait_retrace)(void);

    /* ---- input ---- */
    void (*pad_read)(MuPadStatus out[4]);
    void (*pad_rumble)(int32_t port, int32_t on);

    /* ---- disc ----
     * The host owns the image. Paths are resolved through the disc's own filesystem table, so the
     * game's entry numbers are the console's. */
    int32_t (*disc_entrynum)(const char* path);
    int32_t (*disc_file)(int32_t entrynum, uint32_t* start, uint32_t* length);   /* nonzero: found */
    /* Completion is delivered from poll(), after the same virtual delay the recompiled build uses,
     * so the game sees the timing it saw on the console. */
    void (*disc_read)(uint32_t offset, void* dst, uint32_t size, MuDiscDone done, void* user);
    int32_t (*disc_status)(void);
    uint32_t (*disc_id)(void* out, uint32_t size);

    /* ---- memory card ----
     * Slot A as a folder of .gci files, the way the shipped build already stores it. */
    int32_t (*card_probe)(int32_t chan, int32_t* memSize, int32_t* sectorSize);
    int32_t (*card_mount)(int32_t chan);
    int32_t (*card_unmount)(int32_t chan);
    int32_t (*card_open)(int32_t chan, const char* filename, int32_t* file_no, uint32_t* length);
    int32_t (*card_close)(int32_t chan, int32_t file_no);
    int32_t (*card_create)(int32_t chan, const char* filename, uint32_t size, int32_t* file_no);
    int32_t (*card_delete)(int32_t chan, const char* filename);
    int32_t (*card_rename)(int32_t chan, const char* old_name, const char* new_name);
    int32_t (*card_read)(int32_t chan, int32_t file_no, void* dst, uint32_t length, uint32_t offset);
    int32_t (*card_write)(int32_t chan, int32_t file_no, const void* src, uint32_t length, uint32_t offset);
    int32_t (*card_stat)(int32_t chan, int32_t file_no, void* stat, uint32_t stat_size);
    int32_t (*card_set_stat)(int32_t chan, int32_t file_no, const void* stat, uint32_t stat_size);
    int32_t (*card_free_blocks)(int32_t chan, int32_t* byte_not_used, int32_t* files_not_used);
    int32_t (*card_format)(int32_t chan);

    /* ---- audio ----
     * The game's AX library runs natively and builds its command list in memory; the host's mixer
     * reads it when the mail arrives, exactly as the DSP did. */
    void (*ai_init_dma)(void* buffer, uint32_t length);
    void (*ai_start_dma)(int32_t on);
    void (*ai_set_sample_rate)(uint32_t rate48khz);
    void (*ai_set_stream_volume)(int32_t left, int32_t right);
    void (*dsp_mail)(uint32_t mail);
    uint32_t (*dsp_mail_pending)(void);
    /* Audio RAM: the console's separate ARAM; native runs may expose a larger host backing. */
    void* (*aram_base)(void);
    uint32_t (*aram_size)(void);
    void (*aram_dma)(int32_t to_aram, void* mainmem, uint32_t aram_offset, uint32_t length);

    /* Native-only process storage for host-layout metadata that must not consume the game's
     * console-sized audio heap. NULL for hosts that do not run MU_NATIVE. */
    void* (*native_alloc)(uint32_t size);
    void (*native_free)(void* ptr);

    /* ---- machine ---- */
    uint32_t (*mem1_size)(void);
    int32_t (*sound_mode)(void);              /* 0 mono, 1 stereo */
    void (*set_sound_mode)(int32_t mode);
    int32_t (*progressive_mode)(void);
    void (*set_progressive_mode)(int32_t mode);
    int32_t (*reset_code)(void);
    int32_t (*reset_switch)(void);
    void (*stop)(int32_t reason, int32_t code);   /* MU_STOP_* */

    /* ---- scripted runs (version 2) ---- */
    /* The match a --match run asked for, or NULL for an ordinary run. */
    const MuMatchOverride* (*match_override)(void);

    /* ---- scripted runs (version 3) ----
     * The seed a --rng-seed run asked for. *has_value is set to 0 by the game before calling
     * (an older or non-conforming host that fills *seed without touching *has_value would
     * otherwise look like it asked for seed 0); the host sets it to 1 and fills *seed only when
     * --rng-seed was given. NULL for a host built before version 3, same as match_override for
     * version 2: the game must check for NULL before calling. */
    void (*rng_seed_override)(uint32_t* seed, int32_t* has_value);
    /* Marks "the match's rules and players are now finalised" as the instant an @match-relative
     * script section starts counting from (mirrors what an online match reaching frame 1 already
     * does for input_mark_match_start on the host side). The game calls this once, right where it
     * calls rng_seed_override, regardless of whether --rng-seed was given, so @match sections work
     * the same way for a scripted offline run as they already do online. NULL for a host built
     * before version 3. */
    void (*mark_match_start)(void);

    /* Version 5. Optional diagnostic sidecar from the native HSD_PObj submission path. */
    void (*native_render_scope_event)(const MuNativeRenderScopeEvent* event);

    /* Version 6. Native authored pose capture. Snapshot arrays are valid only during this call.
     * Version 7 appends the envelope (skinned) fields to MuNativePoseSnapshot. */
    int32_t (*native_pose_capture_enabled)(void);
    void (*native_pose_snapshot)(const MuNativePoseSnapshot* snapshot);

    /* Version 8. Game-side options the player can change at any time (settings panel): the native
     * equivalents of the Legacy build's port Gecko codes. Read when the game uses them. */
    uint32_t (*game_options)(void);
    /* Nonzero while the host wants each draw's owning player (the missed L-cancel indicator). The
     * game then marks its fighter draws with the private FIFO token 0xF1 <slot> ... 0xF1 0xFF. */
    int32_t (*native_owner_tracking)(void);
    /* The settings panel's Music level, 0-100 (100 leaves the game's own music volume unchanged). On
     * Legacy it drives the Slippi jukebox; the native game scales its music voice group by it. */
    uint32_t (*music_volume)(void);

    /* Version 9. Slippi replay playback. replay_start is NULL-returning when no --replay was given
     * (and the whole group may be NULL on an older host: the game checks). */
    const MuReplayStart* (*replay_start)(void);
    /* The replay's data for one frame index (-123 is the first). */
    void (*replay_frame)(int32_t frame, MuReplayFrame* out);
    /* Nonzero when the replay has player `port` on `frame` (a teams stock steal). */
    int32_t (*replay_stock_steal)(int32_t frame, int32_t port);
    /* One Slippi event (command byte and payload) for the recording of the played-back game. */
    void (*replay_event)(uint8_t command, const uint8_t* payload, uint32_t size);
    /* The match is over: the host writes the recording and exits. */
    void (*replay_finished)(void);

    /* Version 10. Nonzero while the host pairs draws between simulation frames (sub-frame
     * animation). HSD_PObjDisp then brackets every PObj with the private FIFO token 0xF0 and names
     * the joint it draws, so a draw keeps its identity when other draws come and go. */
    int32_t (*native_draw_identity)(void);

    /* Version 11. Asset overlays can exceed retail's fixed audio and Stay-heap budgets.
     * Only disc-file overlays set this bit; a GCI import alone does not alter asset sizing. */
    uint32_t (*mod_flags)(void);

    /* Version 12. The native game may use the existing Slippi matchmaking and pad transport.
     * Payloads are the same big-endian EXI command payloads used by regular Slippi clients.
     * Returns 0 on success, -1 for an unknown command, -2 for invalid bounds, and -3 for a
     * command that needs a native rollback implementation. In particular, B1/B2 (console-memory
     * savestates) MUST NOT be used by the native game. `response_size` is always initialized. */
    int32_t (*slippi_command)(uint8_t command, const uint8_t* payload, uint32_t payload_size,
                              uint8_t* response, uint32_t response_capacity,
                              uint32_t* response_size);

    /* Version 13. Native online play. The game calls resim_phase(1) right after a rollback load,
     * before it re-simulates frames, and resim_phase(0) when it returns to real time. In between
     * the host must not advance time, fire alarms, tick audio, retrace or present. */
    void (*resim_phase)(int32_t entering);
    /* Test harness only (--online-test): the online match to enter without menus. Returns 1 and
     * fills *out when one is pending, 0 otherwise. */
    int32_t (*online_test_match)(MuOnlineMatch* out);

    /* Version 14. The second word of 20XX TE features (MU_GAME_OPTION2_TE_*); during replay
     * playback, the word the replay was recorded with. */
    uint32_t (*game_options2)(void);

    /* Version 15. The HUD sizes from the settings panel, display only: stock icon percent in the
     * low byte, damage number percent in the next (75..175; 100 is the game's own size). */
    uint32_t (*hud_scales)(void);
    /* Version 15. Each drawn frame, per player slot 0..3: whether the slot is in the match, its
     * damage and stocks, and its player tag in the HUD's 640x480 space (the nickname anchor). */
    void (*hud_player)(int32_t slot, int32_t present, int32_t damage, int32_t stocks,
                       float tag_x, float tag_y, int32_t tag_visible);

    /* Version 16. The idle part of the wait for the next retrace, one audio period at a time.
     * VIWaitForRetrace calls it while it returns nonzero and delivers pending events after each
     * call: the host waits until the real time of the next audio (AI) deadline before that retrace,
     * advances the console clock to it and plays the block, so the game's DMA-done handler mixes the
     * next block then, as the console's AI interrupt did, instead of three blocks in a burst after
     * the retrace. Returns 0 when no deadline is left before the retrace, or when pacing is off. */
    int32_t (*vi_idle_step)(void);

    /* Version 17. The third option word: the 20XX Hack Pack training options for CPUs
     * (MU_GAME_OPTION3_CPU_*), plain offline options like "20XX CPUs". During replay playback, the
     * word the replay was recorded with ("muOptions3"). */
    uint32_t (*game_options3)(void);

    /* Version 18. Display-only options (MU_DISPLAY_OPTION_*), read live each draw. Never recorded
     * in a replay and never sent to an opponent: nothing the simulation reads follows them. */
    uint32_t (*display_options)(void);
} MuHostApi;

/* "Low poly fighters": the fighter draw shows the game's own far model (the one it draws in the
 * Fountain of Dreams reflection, the shadow pass and the magnifier bubble) in place of the full
 * one, for a fighter that has one. Only the DObj hidden flags change (ftdrawcommon.c). */
#define MU_DISPLAY_OPTION_LOW_POLY 0x1u

#define MU_MOD_ASSETS_PRESENT 0x1u
/* The 20XX Hack Pack's disc files are among the overlays (StageSwapTable.bin, numbered music,
 * stage variants). None of its code runs: the game's native rewrites of its features
 * (sourceport/game/shim/mu_hp.c) are offered only with this bit, offline. No host API version
 * change: an older host never sets the bit. */
#define MU_MOD_HACKPACK 0x2u

/* Private command 0xFA on slippi_command, 20XX Hack Pack data and settings for the native game
 * (host: port/runtime/host/hackpack_source.h, game: shim/mu_hp.c). The first payload byte picks:
 *   0x01 addr(4) len(2)   bytes of the pack's main.dol at a console address (its data tables: music
 *                         names, playlists, default stage file names). Data only, never run.
 *   0x02                  the settings block (MU_HP_SETTINGS_SIZE bytes), kept in the settings file
 *   0x03 index value      set one settings byte; the reply is the block
 *   0x04 word(4)          the stage variant of the match about to start, as MU_GAME_OPTION3_HP_STAGE_*
 *                         bits; the host puts them in option word 3 so a replay records them
 *   0x05                  the pack's StageSwapTable.bin, whole
 *   0x06 [word(4)]        the CPU training options (MU_GAME_OPTION3_CPU_* bits), the Game tab's store:
 *                         with the word sets them (offline, not in playback), empty asks; the reply
 *                         is the word in effect. For the pack's debug menu (shim/mu_hp_menu.c).
 * An empty reply means the pack is not loaded (or the request is out of range). */
#define MU_HP_COMMAND            0xFAu
#define MU_HP_OP_DOL_READ        0x01u
#define MU_HP_OP_GET_SETTINGS    0x02u
#define MU_HP_OP_SET_SETTING     0x03u
#define MU_HP_OP_STAGE_STATE     0x04u
#define MU_HP_OP_STAGE_TABLE     0x05u
#define MU_HP_OP_CPU_WORD        0x06u
#define MU_HP_SETTINGS_SIZE      24u
/* Settings block layout. Playlist types are the pack's: 0 the list's first track, 1 random from the
 * list, 2 random original track, 3 random custom track, 4 random of both. */
#define MU_HP_SET_PLAYLIST_TYPE  0u    /* 11 bytes: menu, then the pack's nine stage lists, then global */
#define MU_HP_SET_CUSTOM_SONGS   11u   /* how many custom tracks (0x31 and up) take part in types 3 and 4 */
#define MU_HP_SET_GLOBAL_ON      12u   /* the global playlist replaces every stage song */
#define MU_HP_SET_GLOBAL_MENUS   13u   /* and the menu songs */
#define MU_HP_SET_LEGAL_VARIANT  14u   /* 6 bytes: Stadium, Dream Land, Fountain, Yoshi's Story, Final
                                          Destination, Battlefield; 0..14 a variant, 15 and up random */
#define MU_HP_SET_STAGE_PAGE     20u   /* stage select page, 0..3 */
#define MU_HP_SET_TRAINING       21u   /* the pack's "SAVE STATES/REPLAYS" switches (shim/mu_lab.c): */
#define MU_HP_TRAINING_ON        0x01u /*   savestates and recording on the D-pad in Training mode */
#define MU_HP_TRAINING_LOOP      0x02u /*   a played back recording repeats until a D-pad press */

#define MU_GAME_OPTION_NO_SCREEN_SHAKE 0x1u   /* camera quake offset zeroed before it is applied */
#define MU_GAME_OPTION_PAL_STOCK_ICONS 0x2u   /* stock row at PAL size and height; lost stocks hide */
/* The retail game without Legacy's always-on code set (Slippi's "General Codes" and "Lagless FoD",
 * built into the native game): for parity runs against the code-free recompilation. Read once at
 * boot, as Legacy applies those codes once. */
#define MU_GAME_OPTION_VANILLA 0x4u
#define MU_GAME_OPT_SLIPPI_MENUS 0x8u
/* 20XX Tournament Edition features, native (M2). Offline only: the game ignores every one of them in
 * an online match and in replay playback. TE is the master switch; TE_TOURNAMENT is 20XX TE's
 * Tournament Mode, which keeps only the features in MU_GAME_OPTION_TE_TOURNAMENT_SAFE. */
#define MU_GAME_OPTION_TE                  0x00000010u
#define MU_GAME_OPTION_TE_TOURNAMENT       0x00000020u
#define MU_GAME_OPTION_TE_HOLD_START_PAUSE 0x00000040u   /* hold Start half a second to pause a VS match */
#define MU_GAME_OPTION_TE_FROZEN_STAGES    0x00000080u   /* stage hazards off */
#define MU_GAME_OPTION_TE_NO_STAR_KO       0x00000100u   /* top blast zone KOs never become Star or screen KOs */
#define MU_GAME_OPTION_TE_INFINITE_SHIELDS 0x00000200u   /* shields never shrink; CPUs hold shield */
#define MU_GAME_OPTION_TE_TAUNT_CANCEL     0x00000400u   /* dash into a taunt; taunt cancels on landing */
#define MU_GAME_OPTION_TE_UNFREEZE_ENDGAME 0x00000800u   /* play on for a moment after GAME! */
#define MU_GAME_OPTION_TE_FIXED_CAMERA     0x00001000u   /* the camera stays fixed on the whole stage */
#define MU_GAME_OPTION_TE_CPU_ZELDA_SHEIK  0x00002000u   /* a CPU Zelda starts as Sheik and never transforms */
#define MU_GAME_OPTION_TE_HANDICAP_STOCKS  0x00004000u   /* in stock matches, handicap sets each player's stocks */
/* Training Lab (M3), Training mode only, offline only: savestates on D-pad Right / Left, and the
 * game's collision bubbles over the model or alone. */
#define MU_GAME_OPTION_LAB                 0x00008000u
#define MU_GAME_OPTION_LAB_BUBBLES         0x00010000u
#define MU_GAME_OPTION_LAB_BUBBLES_ONLY    0x00020000u
#define MU_GAME_OPTION_LAB_OVERLAY         0x00040000u   /* host only: inputs and frame data */
#define MU_GAME_OPTION_LAB_DI_MASK         0x00180000u   /* dummy DI: 0 none, 1 in, 2 out, 3 random */
#define MU_GAME_OPTION_LAB_DI_SHIFT        19
#define MU_GAME_OPTION_LAB_REACT_MASK      0x00600000u   /* after hitstun: 0 none, 1 jump, 2 shield, 3 spot dodge */
#define MU_GAME_OPTION_LAB_REACT_SHIFT     21
#define MU_GAME_OPTION_LAB_TECH_MASK       0x03800000u   /* tech: 0 own, 1 in place, 2 toward, 3 away, 4 random */
#define MU_GAME_OPTION_LAB_TECH_SHIFT      23
#define MU_GAME_OPTION_LAB_LOOP            0x04000000u   /* playback returns to the kept moment and repeats */
/* Training Mode CE's disc files are in the mod profile, on the vanilla game (sourceport/game/tmce). */
#define MU_GAME_OPTION_TMCE                0x08000000u
/* Slippi's "Widescreen 16:9" optional code, native (shim/mu_gecko.c). Only what is drawn changes,
 * so it follows each player's own setting online and in replays. */
#define MU_GAME_OPTION_WIDESCREEN          0x10000000u
/* True 16:9 is on (the renderer widens the picture; the game's cameras stay as they are). The game
 * only uses it to draw a fighter who is in the added sides, and puts the fighter's state back. */
#define MU_GAME_OPTION_TRUE_WIDESCREEN     0x20000000u
/* The language the player chose in the PC settings: 0 the game's own choice (its save), 1 Japanese,
 * 2 English. The game answers with it wherever it reads its saved language (lblanguage.c). */
#define MU_GAME_OPTION_LANGUAGE_MASK       0xC0000000u
#define MU_GAME_OPTION_LANGUAGE_SHIFT      30
#define MU_GAME_OPTION_TE_TOURNAMENT_SAFE                                                         \
    (MU_GAME_OPTION_TE_HOLD_START_PAUSE | MU_GAME_OPTION_TE_FROZEN_STAGES |                     \
     MU_GAME_OPTION_TE_CPU_ZELDA_SHEIK | MU_GAME_OPTION_TE_HANDICAP_STOCKS)

/* 20XX TE, second feature word (host API 14, game_options2). Offline only, with the TE save loaded
 * and "Use 20XX TE features" on. Tournament Mode keeps only MU_GAME_OPTION2_TE_TOURNAMENT_SAFE. */
#define MU_GAME_OPTION2_TE_NEUTRAL_SPAWNS   0x00000001u   /* players always start on the neutral spawns */
#define MU_GAME_OPTION2_TE_V100             0x00000002u   /* v1.00 game rules (hitlag, flame/boomerang cancel, PK Thunder) */
#define MU_GAME_OPTION2_TE_DL64_QUIET       0x00000004u   /* Dream Land 64 music slightly quieter */
#define MU_GAME_OPTION2_TE_RESET_TOURNAMENT 0x00000008u   /* Tournament Mode: rules and stages reset between games */
#define MU_GAME_OPTION2_TE_FROZEN_TOGGLE    0x00000010u   /* Y on stage select toggles frozen stages */
#define MU_GAME_OPTION2_TE_SKIP_RESULTS     0x00000020u
#define MU_GAME_OPTION2_TE_RANDOM_MUSIC     0x00000040u
#define MU_GAME_OPTION2_TE_SHIELD_COLORS    0x00000080u   /* L/R on character select cycle shield colors */
#define MU_GAME_OPTION2_TE_NO_SCREEN_RUMBLE 0x00000100u
#define MU_GAME_OPTION2_TE_LCANCEL_FLASH    0x00000200u
#define MU_GAME_OPTION2_TE_SPOOF_PLUGINS    0x00000400u   /* character select treats every port as plugged in */
#define MU_GAME_OPTION2_TE_LCANCEL_WHEELS   0x00000800u   /* automatic L-cancel, flash on a real one */
#define MU_GAME_OPTION2_TE_BUBBLES          0x00001000u   /* collision bubbles in matches */
#define MU_GAME_OPTION2_TE_INPUT_DISPLAY    0x00002000u   /* host only: input display in matches and replays */
#define MU_GAME_OPTION2_TE_CPU_SMART_DI     0x00004000u   /* CPUs DI and tech at random, survival DI on strong hits */
#define MU_GAME_OPTION2_TE_COLOR_OVERLAYS   0x00008000u   /* fighters turn green when they can act */
#define MU_GAME_OPTION2_TE_HANDWARMERS      0x00010000u   /* 1-minute time matches: fighters pass through each other */
#define MU_GAME_OPTION2_TE_STAGE_STRIKE     0x00020000u   /* X on stage select strikes a stage */
#define MU_GAME_OPTION2_TE_LOCK_SETTINGS    0x00040000u   /* host only: the TE settings cannot be changed */
/* 0x00080000 to 0x00400000: the L-cancel flash choices (mu_lcancel_flash.h). */
#define MU_GAME_OPTION2_TE_20XX_CPUS        0x00800000u   /* CPUs play with the 20XX Hack Pack's AI */
#define MU_GAME_OPTION2_TE_TOURNAMENT_SAFE                                                        \
    (MU_GAME_OPTION2_TE_NEUTRAL_SPAWNS | MU_GAME_OPTION2_TE_V100 | MU_GAME_OPTION2_TE_DL64_QUIET |  \
     MU_GAME_OPTION2_TE_RESET_TOURNAMENT | MU_GAME_OPTION2_TE_FROZEN_TOGGLE |                     \
     MU_GAME_OPTION2_TE_HANDWARMERS | MU_GAME_OPTION2_TE_STAGE_STRIKE | MU_GAME_OPTION2_TE_LOCK_SETTINGS)

/* Third option word (host API 17, game_options3): 20XX Hack Pack training options for CPUs, native
 * (sourceport/game/shim/mu_20xx_cpu.c). Plain options from the Game tab: no pack or save needed,
 * offline only, never in a TM-CE event; recorded in a replay as "muOptions3". */
#define MU_GAME_OPTION3_CPU_TECH_MASK    0x00000007u   /* 0 off, 1 in place, 2 roll forward, 3 roll back, 4 miss, 5 random */
#define MU_GAME_OPTION3_CPU_TECH_SHIFT   0
#define MU_GAME_OPTION3_CPU_GETUP_MASK   0x00000038u   /* 0 off, 1 stand, 2 roll forward, 3 roll back, 4 attack, 5 random */
#define MU_GAME_OPTION3_CPU_GETUP_SHIFT  3
#define MU_GAME_OPTION3_CPU_DI_MASK      0x000000C0u   /* 0 off, 1 none, 2 random, 3 survival */
#define MU_GAME_OPTION3_CPU_DI_SHIFT     6
#define MU_GAME_OPTION3_CPU_SDI_MASK     0x00000700u   /* 0 off, 1 none, 2 random, 3 with the hit, 4 against it, 5 up, 6 down */
#define MU_GAME_OPTION3_CPU_SDI_SHIFT    8
#define MU_GAME_OPTION3_CPU_NO_TAUNT     0x00000800u   /* CPUs never taunt */
#define MU_GAME_OPTION3_CPU_LCANCEL      0x00001000u   /* CPUs L-cancel every aerial landing */
#define MU_GAME_OPTION3_CPU_NO_RAPID_JAB 0x00002000u   /* CPU Captain Falcon never starts his rapid jab */
#define MU_GAME_OPTION3_CPU_NO_TRANSFORM 0x00004000u   /* CPU Zelda and Sheik never use their down special */
#define MU_GAME_OPTION3_CPU_ALL          0x00007FFFu
/* The same word, upper bits: the 20XX Hack Pack stage of the match (shim/mu_hp.c), so a replay
 * loads the stage file the match was played on. Not a setting: the game reports them when a match
 * is picked (MU_HP_OP_STAGE_STATE) and the host only carries them, offline, with the pack loaded.
 * ON: the pack's stage file names were in use. CHAR: the variant letter of the picked stage's file
 * (0 the pack's default name, 1..10 '0'..'9', 11..36 'a'..'z'). FLAGS: the stage swap table's
 * custom flag byte for the pick. */
#define MU_GAME_OPTION3_HP_STAGE_ON          0x00008000u
#define MU_GAME_OPTION3_HP_STAGE_CHAR_MASK   0x003F0000u
#define MU_GAME_OPTION3_HP_STAGE_CHAR_SHIFT  16
#define MU_GAME_OPTION3_HP_STAGE_FLAGS_MASK  0x3FC00000u
#define MU_GAME_OPTION3_HP_STAGE_FLAGS_SHIFT 22
#define MU_GAME_OPTION3_HP_STAGE_ALL         0x3FFF8000u

/* One writable region of game-owned state. Regions do not include the live native stack, host
 * timing, ARAM, or process allocations; callers must capture those separately for rollback. */
typedef struct MuStateRegion {
    void* address;
    uint32_t size;
} MuStateRegion;

/* Game API version 4: the game publishes its state regions for native snapshot work.
 * Version 5: and the ranges inside them that snapshots must skip. */

typedef struct MuGameApi {
    uint32_t version;   /* MU_GAME_API_VERSION */

    /* Runs the game. Returns when it stops. */
    int32_t (*run)(void);
    /* One 60 Hz tick: the host calls this from its own frame loop, inside the game's poll(). */
    void (*retrace)(void);
    /* Alarms the game registered; the host calls this with the current timebase. */
    void (*fire_alarms)(uint64_t now);
    /* The audio buffer the game handed to AI has been played. */
    void (*ai_dma_done)(void);
    /* Snapshot at a retrace boundary, before the next game update. */
    void (*state_snapshot)(MuStatePod* out);
    /* Version 3. L-cancel helper state, read when the host reads the pads. */
    void (*lcancel_view)(MuLcancelView* out);
    /* Version 4. Returns the required region count; fills up to `capacity` entries. */
    uint32_t (*state_regions)(MuStateRegion* out, uint32_t capacity);
    /* Version 5. Ranges inside the state regions that a rollback snapshot must neither save nor
     * restore: audio driver state, host-coupled shim state and the online bookkeeping itself.
     * Same calling convention as state_regions. */
    uint32_t (*state_exclusions)(MuStateRegion* out, uint32_t capacity);
    /* Version 5. The ranges a rollback snapshot covers right now: the scene's main heap, that
     * heap's descriptor, and the image's writable sections. Call it inside a match (the main heap
     * is re-created at every scene change). Same calling convention as state_regions. */
    uint32_t (*snapshot_ranges)(MuStateRegion* out, uint32_t capacity);
    /* Version 6. Practice matchmaking (Tab during an offline match): the host's coordinator reads
     * and changes the few pieces of scene state it needs through these operations (MU_PRACTICE_*).
     * `args` carries inputs and receives outputs; returns 0 on success, -1 for an unknown op. */
    int32_t (*practice)(int32_t op, int32_t* args, int32_t count);
} MuGameApi;

enum {
    MU_PRACTICE_SCENE = 0,          /* out: [0] major, [1] minor state id */
    MU_PRACTICE_GET_PLAYER = 1,     /* in [0] slot; out [1..8] state, character, kind, costume,
                                       pad port, cpu level, damage, start damage */
    MU_PRACTICE_SET_PLAYER = 2,     /* in [0] slot, [1..8] as above (state ignored) */
    MU_PRACTICE_GET_STAGE = 3,      /* out [0] stage kind of the current match */
    MU_PRACTICE_SET_STAGE = 4,      /* in [0] */
    MU_PRACTICE_SET_EVENT_BACKUP = 5,   /* in [0] character, [1] costume, [2] pad port */
    MU_PRACTICE_REQUEST_MAJOR = 6,  /* in [0] major: leave the current scene for it */
    MU_PRACTICE_SET_ONLINE_MODE = 7,    /* in [0] Slippi mode id */
    MU_PRACTICE_GET_ONLINE_MODE = 8,    /* out [0] */
    MU_PRACTICE_DIRECT_FIRST_MATCH = 9, /* reset Direct's winner and chose-stage state */
};

/* The library's one export. The host fills `host`, the game fills `game`. */
typedef int32_t (*MuGameEntry)(const MuHostApi* host, MuGameApi* game);

#ifdef __cplusplus
}
#endif
#endif
