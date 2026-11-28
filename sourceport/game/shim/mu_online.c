#if MU_SQUAD
/* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
#include "mu_squad.h"
#include <melee/gm/gm_1A3F.h>
#endif
/* Native Slippi online play.
 *
 * The console game gets online play from Slippi's patches: it exchanges pads with the host's Slippi
 * device every frame, saves and loads its state around rollbacks, and re-simulates the frames a
 * late remote input changed, all inside the engine loop (gm_801A4D34). This file is the native
 * equivalent, called from the same sites; each function names the patch it stands for. Nothing
 * Slippi's original paths run only for its online matches. An explicit console-only backend reuses
 * the predictor and runner, with its own lifecycle and input adapter (mu_console_rollback.inc).
 *
 * Frame numbers: the online pad frame N (the number sent with each pad) is consumed by the engine
 * body whose unpaused-frame counter (gm_80479D58.unk_8) is N at its start; savestates and
 * checksums are keyed by that counter. Measured on the console code: 4746 of 4746 samples.
 * Console rollback instead keys each body by unk_0, including paused bodies.
 *
 * All of this bookkeeping is excluded from snapshots: a rollback load restores the game, never the
 * record of which frames were predicted or confirmed.
 */
#include <string.h>
#ifndef MU_NO_SLIPPI
#include "mu_tourney_game.h"
#endif
#include <math.h>
char* getenv(const char* name);
#include <dolphin/pad.h>
#include <melee/cm/camera.h>
#include <melee/ft/forward.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/types.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/types.h>
#include <melee/it/forward.h>
#include <melee/it/types.h>
#include <melee/lb/lb_0195.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/random.h>
#include <mu_native.h>
#ifndef MU_NO_SLIPPI
#include <mu_ranked_setup_online.h>
#endif
#include <sysdolphin/baselib/sislib.h>

int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size);
void mu_online_abi_resim_phase(int entering);
int mu_online_abi_test_match(int* mode, int* input_port);
unsigned int mu_online_abi_retrace_count(void);
void mu_online_abi_log(const char* text);
int snprintf(char* buffer, __SIZE_TYPE__ size, const char* format, ...);
void mu_replay_apply_game_info(StartMeleeData* data, const unsigned char* info);
struct gm_80479D58_t* mu_gm_engine_state(void);
void mu_gmvs_request_online_end(int pauser);
void mu_gmvs_online_end_time(int frames);
void mu_alarms_hold(void);
void mu_alarms_release(void);
void mu_replay_game_info_block(unsigned char* out, const StartMeleeData* data);
u32 gm_801A4BA8(void);    /* MenuController_LoadTimer1: engine bodies run this match */
u32 pl_80040AF0(int slot);   /* PlayerBlock_GetCliffhangerStat: ledge grabs */
extern StaticPlayer player_slots[];
extern struct GameSceneInfo* gm_804D6720;

enum {
    ROLLBACK_MAX = 7,
    LOCAL_INPUTS_COUNT = 2 * ROLLBACK_MAX,
    PREDICTED_INPUTS_COUNT = 2 * ROLLBACK_MAX,
    RXB_INPUTS_COUNT = ROLLBACK_MAX,
    REMOTES = 3,
    PAD_SIZE = 12,
    MIN_DELAY = 1,
    MAX_DELAY = 15,
    UNFREEZE_FRAME = 84,
    START_SYNC_FRAME = UNFREEZE_FRAME - 6,
    DESYNC_ENTRIES = ROLLBACK_MAX * 3,
    USED_PADS_COUNT = 16,
    RESP_NORMAL = 1,
    RESP_SKIP = 2,
    RESP_DISCONNECTED = 3,
    RESP_ADVANCE = 4,
    CMD_ONLINE_INPUTS = 0xB0,
    CMD_CAPTURE_SAVESTATE = 0xB1,
    CMD_LOAD_SAVESTATE = 0xB2,
    CMD_GET_MATCH_STATE = 0xB3,
    CMD_REPORT_GAME = 0xBD,
    CMD_REPORT_MATCH_STATUS = 0xC4,
    CMD_PROAI_SEAT = 0xC6,       /* this machine's Pro AI seat: active, port, pro (slippi_online.cpp) */
    CMD_SELFTEST_KEEP = 0xF0,     /* native test only: keep the state as a reference */
    CMD_SELFTEST_COMPARE = 0xF1,   /* native test only: compare the state with the reference */
    RESPONSE_CAPACITY = 4096,
    /* Slippi 3.7.0 (Online.s): the frame-indexed RNG history (a power of 2 reaching further back
     * than the longest rollback) and the two extra bytes of a pad report. Byte 8 goes over the
     * network with the inputs and the game never sees it; byte 11 marks a kept prediction only. */
    RNG_HISTORY_COUNT = 16,
    PAD_RNG_BITS = 8,
    PAD_RNG_ROLLBACK_MARKER = 11,
    SIDE_B_MIN_STICK_X = 48,
    DOWN_B_MAX_STICK_Y = -44,
    /* The inputs command payload (Slippi's TXB without its command byte), byte offsets. */
    TXB_FRAME = 0,
    TXB_FINALIZED_FRAME = 4,
    TXB_FINALIZED_FRAME_CHECKSUM = 8,
    TXB_DELAY = 12,
    TXB_PAD = 13,
    TXB_INPUT_SOURCE_INDEX = TXB_PAD + PAD_SIZE,   /* controller port the local inputs come from */
    TXB_IS_LEAVING = TXB_INPUT_SOURCE_INDEX + 1,    /* the local player chose to leave the game */
    TXB_SIZE = TXB_IS_LEAVING + 1,                  /* 27 */
    /* YouGame local partners: the partner's pad after Slippi's payload (8 input bytes and a zero RNG
     * bits byte, the host's PAD_WIRE_SIZE); always sent, zero without a partner. */
    TXB_PARTNER_PAD_SIZE = PAD_RNG_BITS + 1,
    /* The match state response (Slippi's MSRB), byte offsets. */
    MSRB_CONNECTION_STATE = 0,
    MSRB_LOCAL_READY = 1,
    MSRB_REMOTE_READY = 2,
    MSRB_LOCAL_PLAYER_INDEX = 3,
    MSRB_REMOTE_PLAYER_INDEX = 4,
    MSRB_RNG_OFFSET = 5,
    MSRB_DELAY_FRAMES = 9,
    MSRB_ERROR_MSG = 357,
    MSRB_GAME_INFO_BLOCK = 598,
    MSRB_ALT_STAGE_MODE = 961,
    /* Slippi 3.7.0: per port the ranked info (rating f32, update count u32, global and regional
     * placement u16, mmr f32; RPI_SIZE bytes, recorded as is) and then a bot flag per port. */
    MSRB_RANKED_PLAYER_INFO = MSRB_ALT_STAGE_MODE + 1,
    RPI_SIZE = 16,
    MSRB_IS_BOT = MSRB_RANKED_PLAYER_INFO + RPI_SIZE * 4,
    MSRB_SIZE = MSRB_IS_BOT + 4,   /* 1030 */
    /* The 3.6.4 reply ends where the ranked info starts. The peer-to-peer host still answers that
     * one; the fields past it only go into the recording, which then gets zeroes. */
    MSRB_MIN_SIZE = MSRB_RANKED_PLAYER_INFO,
    MM_CONNECTION_SUCCESS = 4,
    MM_ERROR_ENCOUNTERED = 5,
    /* The inputs response (Slippi's RXB), byte offsets. */
    RXB_RESULT = 0,
    RXB_OPNT_COUNT = 1,
    RXB_OPNT_DESYNC = 2,
    RXB_OPNT_FRAME_NUMS = RXB_OPNT_DESYNC + 8 * REMOTES,
    RXB_SMALLEST_LATEST = RXB_OPNT_FRAME_NUMS + 4 * REMOTES,
    RXB_OPNT_INPUTS = RXB_SMALLEST_LATEST + 4,
    /* Slippi 3.7.0: per remote player the frame its fighter despawns on (s32, 0 if it does not),
     * then per remote player the pads to predict each of the RXB_INPUTS_COUNT frames after its
     * latest frame with (entry 0 is latest + 1), then the random bits to send with the next pad. */
    RXB_DESPAWN_FRAMES = RXB_OPNT_INPUTS + PAD_SIZE * RXB_INPUTS_COUNT * REMOTES,
    RXB_OPNT_PREDICTED_INPUTS = RXB_DESPAWN_FRAMES + 4 * REMOTES,
    RXB_NEXT_PAD_RNG_BITS = RXB_OPNT_PREDICTED_INPUTS + PAD_SIZE * RXB_INPUTS_COUNT * REMOTES,
    RXB_SIZE = RXB_NEXT_PAD_RNG_BITS + 1,   /* 559 */
    /* Early inputs, after Slippi's reply (the host appends them for this engine only): per remote
     * a count, then entries of a target pad frame (big-endian) and a 12-byte pad. Each is a
     * controller change the other player sent the moment it happened, before the pad of the frame
     * it lands in. Used for prediction only: the confirmed pad for a frame always decides. */
    EARLY_MAX = 8,
    EARLY_ENTRY = 4 + PAD_SIZE,
    RXB_EARLY = RXB_SIZE,
    RXB_EARLY_BYTES = REMOTES * (1 + EARLY_MAX * EARLY_ENTRY),
    /* Triples (5 and 6 players, Melee Unlocked only): remotes 4 and 5 follow Slippi's reply and the
     * early block, which then keeps its full size (no early pads in these matches). Per extra remote:
     * the desync pair, the latest frame, its pads, its despawn frame and its predicted pads, as in the
     * reply above. A 4-player match never gets this block, so Slippi's layout stays as it is. */
    REMOTES_MAX = 5,
    ONLINE_PORTS = 6,
    TRI_DESYNC = 0,
    TRI_FRAME_NUM = 8,
    TRI_INPUTS = 12,
    TRI_DESPAWN = TRI_INPUTS + PAD_SIZE * RXB_INPUTS_COUNT,
    TRI_PREDICTED = TRI_DESPAWN + 4,
    TRI_ENTRY = TRI_PREDICTED + PAD_SIZE * RXB_INPUTS_COUNT,
    RXB_TRI = RXB_EARLY + RXB_EARLY_BYTES,
    RXB_TRI_BYTES = (REMOTES_MAX - REMOTES) * TRI_ENTRY,
    /* The match state's Triples byte, after Slippi's (only a Triples match has it): 6. */
    MSRB_TRIPLES = MSRB_SIZE,
    /* The game report (Slippi's RGB without its command byte), byte offsets. */
    RGB_ONLINE_MODE = 0,
    RGB_FRAME_LENGTH = 1,
    RGB_GAME_INDEX = 5,
    RGB_TIEBREAKER_INDEX = 9,
    RGB_WINNER_IDX = 13,
    RGB_GAME_END_METHOD = 14,
    RGB_LRAS_INITIATOR = 15,
    RGB_SYNCED_TIMER = 16,
    RGB_P1_RGPB = 20,
    RGPB_SIZE = 9,   /* slot type, stocks, damage done (float), synced stocks, synced damage (u16) */
    RGB_GAME_INFO_BLOCK = RGB_P1_RGPB + 4 * RGPB_SIZE,
    MATCH_STRUCT_LEN = 0x138,
    RGB_SIZE = RGB_GAME_INFO_BLOCK + MATCH_STRUCT_LEN,   /* 368, the bridge's CMD_REPORT_GAME size */
    ONLINE_MODE_RANKED = 0,
    ONLINE_MODE_PARTY = 4,
    LGL_LIMIT = 45,   /* ledge grabs past this lose a timeout */
    END_TIME = 1,
    END_GAME = 2,
    END_LRAS = 7,
    LEAVING_GAME_END_FRAMES = 0x1E,   /* StartEngineLoop: GAME! for someone who chose to leave */
};

/* Slippi 3.7.0's sizes (Output/Netplay InitOnlinePlay: TXB 0x1C with the command byte, RXB 0x22F). */
_Static_assert(TXB_SIZE == 27 && RXB_SIZE == 559 && MSRB_SIZE == 1030, "Slippi 3.7.0 buffer layouts");

enum { ENGINE_CONTINUE = 0, ENGINE_RESIM = 1, ENGINE_ROLLBACK_DONE = 2 };

typedef struct DesyncLocal {
    int frame;
    unsigned int checksum;
    /* Desync recovery (DDLE_RECOVERY_*): the frame's timer and every port's stocks and percent. */
    unsigned int recovery_timer;
    u8 recovery_stocks[4];
    u16 recovery_percent[4];
} DesyncLocal;

typedef struct MuOnlineState {
    int active;               /* an online match is running */
    int pending;              /* an online match has been negotiated and waits for its scene */
    int test;                 /* the harness entered this match without menus */
    int mode;                 /* Slippi mode id */
    u8 local_index;           /* the local player's in-game port */
    u8 ports;                 /* in-game ports the match's pads cover: 4, or 6 in a Triples match */
    u8 remote_index;
    u8 input_source;          /* the physical port the local pad is read from */
    u8 delay;
    int frame;                /* next online pad frame to fetch */
    unsigned int rng_offset;
    u8 last_partner[PAD_SIZE];
    u8 last_local[PAD_SIZE];
    u8 delay_index;
    u8 delay_buffer[MAX_DELAY][PAD_SIZE];
    u8 rxb[RXB_TRI + RXB_TRI_BYTES];
    unsigned int rxb_size;    /* bytes the host answered the last inputs command with */
    int rollback_active, rollback_should_load, rollback_end_frame;
    u8 local_inputs_idx;
    u8 local_inputs[LOCAL_INPUTS_COUNT][PAD_SIZE];
    u8 predicted_read[REMOTES_MAX], predicted_write[REMOTES_MAX];
    u8 predicted[REMOTES_MAX][PREDICTED_INPUTS_COUNT][PAD_SIZE];
    u8 predicted_early[REMOTES_MAX][PREDICTED_INPUTS_COUNT];   /* the prediction came from an early pad */
    int savestate_predicting, savestate_frame;
    int player_savestate_frame[REMOTES_MAX];
    u8 player_predicting[REMOTES_MAX];
    int latest_frame;
    int stable_rollback_active, stable_rollback_end_frame, stable_should_load, stable_savestate_frame;
    int stable_finalized, finalized;
    int force_pad_renew;
    int frame_advance;
    int disconnected, disconnect_displayed, game_over, game_end_frame;
    int desync_displayed, desync_risk_displayed;
    int desync_last_frame;
    u8 desync_write_idx;
    DesyncLocal desync_local[DESYNC_ENTRIES];
    /* The recovery values of the latest frame both games agreed on (ODB_DESYNC_RECOVERY_*). */
    unsigned int recovery_timer;
    u8 recovery_stocks[4];
    u16 recovery_percent[4];
    StartMeleeData* start_data;   /* the match's game info (0x80480530), for the game report */
    /* Slippi 3.7.0, indexed by frame & (RNG_HISTORY_COUNT - 1): the ports holding B on each frame
     * (ODB_B_HELD_HISTORY) and the bits sync_rng_proc mixes into that frame's seed
     * (ODB_RNG_SEED_BITS_HISTORY). Kept through rollbacks, like the rest of this block. */
    u8 b_held_history[RNG_HISTORY_COUNT];
    u8 rng_seed_bits_history[RNG_HISTORY_COUNT];
    u8 is_leaving;            /* ODB_IS_LEAVING: the local player left a Party match (InitPause) */
    unsigned int tx_checksum; /* the checksum field of the last pad sent (Slippi reuses its buffer) */
    int direct_renew;         /* depth of pad renews the rollback logic called itself */
    int resim;                /* re-simulating after a rollback load */
    /* The pads each recent frame used, every port in wire form, and whether every remote pad in
     * it was confirmed. A re-simulated confirmed frame must get the same pads again; the
     * self-test replays them, since the host drops remote pads once a frame is finalized. */
    int used_frame[USED_PADS_COUNT];
    u8 used_confirmed[USED_PADS_COUNT];
    u8 used_pads[USED_PADS_COUNT][ONLINE_PORTS][PAD_SIZE];
    int selftest_rollback;    /* the running rollback is the self-test's */
    unsigned int resim_input_mismatches;
    unsigned int forced_renews, gated_alarms;
    /* Determinism self-test (MELEE_ONLINE_SELFTEST=every:depth, test harness only). */
    int selftest_every, selftest_depth, selftest_ref_frame;
    /* Diagnostics. */
    unsigned int rollbacks, resim_frames, max_depth, loads, captures, skips, advances, skip_run;
    /* Predictions compared with the pad that then arrived, how many were wrong, and the same for
     * the ones that came from an early pad (early_used counts frames predicted from one). */
    unsigned int predictions, mispredictions, early_used, early_right, early_wrong;
    unsigned int early_rollbacks;   /* rollbacks started by an early pad, before the confirmed one */
    /* Frames whose seed took players' RNG bits, and rollbacks a prediction marked for them forced. */
    unsigned int rng_bit_frames, rng_rollbacks;
    unsigned int despawns;    /* remote fighters removed after a disconnect */
    /* Correction size: fighter positions when a rollback loaded, compared with the re-simulated
     * state at the same frame (the visible snap). */
    float corr_pos[ONLINE_PORTS][2];
    u8 corr_valid[ONLINE_PORTS];
    int corr_pending;
    double corr_sum;
    float corr_max;
    unsigned int corr_n, corr_over1, corr_over4;
    /* Per remote: early guesses scored, how many were wrong, and whether its early pads are ignored
     * for the rest of the match (more than 60% wrong after 64; honest play stays under 26%). */
    unsigned int early_scored[REMOTES_MAX], early_missed[REMOTES_MAX];
    u8 early_distrusted[REMOTES_MAX];
    /* How long rollbacks take, trigger to done, in microseconds: the longest, the total, and how
     * many ran past 8 ms and past a whole frame (16.7 ms, a frame the player could see drop). */
    unsigned long long rollback_start_ticks;
    unsigned int rollback_us_max, rollback_over_8ms, rollback_over_frame;
    unsigned long long rollback_us_total;
    unsigned int test_inputs;
    unsigned int wait_retrace;
    int loop_entered;         /* the scene's engine loop has reached its pad wait */
    int engine_started;       /* the engine body of frame 0 has begun */
    u8 match_state[RESPONSE_CAPACITY];
    int console_setup, console_active, console_injecting;
    unsigned int console_epoch;
} MuOnlineState;

static MuOnlineState mu_online;

/* The match's ports and remote players: 4 and 3, or 6 and 5 in a Triples match. */
static int online_ports(void)
{
    return mu_online.ports == ONLINE_PORTS && !mu_online.console_active ? ONLINE_PORTS : 4;
}
static int online_remotes(void)
{
    return online_ports() - 1;
}
/* A remote's part of the reply: Slippi's layout for remotes 1 to 3, the Triples block after it for
 * remotes 4 and 5. */
static const u8* rx_part(int count, unsigned int base, unsigned int stride, unsigned int tri)
{
    if (count < REMOTES) {
        return mu_online.rxb + base + stride * (unsigned int) count;
    }
    return mu_online.rxb + RXB_TRI + TRI_ENTRY * (unsigned int) (count - REMOTES) + tri;
}
#define RX_DESYNC(c) rx_part((c), RXB_OPNT_DESYNC, 8, TRI_DESYNC)
#define RX_FRAME_NUM(c) rx_part((c), RXB_OPNT_FRAME_NUMS, 4, TRI_FRAME_NUM)
#define RX_INPUTS(c) rx_part((c), RXB_OPNT_INPUTS, PAD_SIZE * RXB_INPUTS_COUNT, TRI_INPUTS)
#define RX_DESPAWN(c) rx_part((c), RXB_DESPAWN_FRAMES, 4, TRI_DESPAWN)
#define RX_PREDICTED(c) rx_part((c), RXB_OPNT_PREDICTED_INPUTS, PAD_SIZE * RXB_INPUTS_COUNT, TRI_PREDICTED)

int mu_online_ports(void)
{
    return mu_online.active ? online_ports() : 4;
}

/* These operations are the version-26 host ABI, private to console sessions. */
enum { CONSOLE_QUERY, CONSOLE_MATCH_BEGIN, CONSOLE_FORWARD, CONSOLE_INPUTS,
       CONSOLE_REPLAY, CONSOLE_MATCH_END, CONSOLE_FAIL,
       CONSOLE_BOUNDARY, CONSOLE_BODY_END, CONSOLE_CONFIRM, CONSOLE_COMMIT, CONSOLE_SERVICE,
       CONSOLE_VALIDATED };
int mu_console_abi(int operation, unsigned int body, u8* local, u8* rxb,
                   unsigned int* epoch, int* local_port);
static void console_frame_begin(void);
static int console_frame_end(void);
static int console_pad_renew(PADStatus* stat);
static void console_match_exit(void);


/* GPDO_CUR_GAME of Slippi's game prep data: the online games since the player last searched for an
 * opponent, 1 for the first. It outlives each match (arming clears mu_online) and only changes
 * outside one, as Slippi's own counter does. */
static unsigned int online_game_index;

int vsnprintf(char* out, size_t size, const char* fmt, __builtin_va_list args);
/* Any arguments the format names (the early-pad and rollback-time lines pass more than three). */
static void logf_(const char* fmt, ...)
{
    char line[320];
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    vsnprintf(line, sizeof line, fmt, args);
    __builtin_va_end(args);
    mu_online_abi_log(line);
}

static u32 be32(const u8* p) { return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3]; }
static void put32(u8* p, u32 v) { p[0] = (u8) (v >> 24); p[1] = (u8) (v >> 16); p[2] = (u8) (v >> 8); p[3] = (u8) v; }

int mu_online_active(void)
{
    return mu_online.active;
}

int mu_online_resim_active(void)
{
    return mu_online.resim;
}

/* ODB_IS_DISCONNECTED, for InitPause's client pause (gmvs.c). */
int mu_online_disconnected(void)
{
    return mu_online.disconnected;
}

/* ODB_IS_LEAVING (Slippi 3.7.0): set by InitPause when the local player LRA-Starts out of a Party
 * match, sent with every pad from then on (the host then disconnects from everyone), and read by
 * StartEngineLoop and VSSceneDecide. Kept through rollbacks, so it is set once. It outlives the match
 * until the next one is armed, as the console's ODB does until InitOnlinePlay replaces it. */
int mu_online_is_leaving(void)
{
    return mu_online.is_leaving;
}

void mu_online_leave(void)
{
    mu_online.is_leaving = 1;
}

/* Slippi Dolphin always runs its Required code sections (General Codes, Recording, Online), so an
 * online match plays with every gameplay code in them, whatever this game was booted with. */
unsigned int mu_online_codes(void)
{
    if (!mu_online.active && !mu_online.pending
#ifndef MU_NO_SLIPPI
        && !mu_tourney_playing()
#endif
    ) {
        return 0;
    }
    return MU_RC_INIT_STAGE_DATA | MU_RC_INIT_PLAYER_DATA | MU_RC_OFFSCREEN_DAMAGE | MU_RC_DEAD_UP_FALL |
           MU_RC_FD_BG_SEED | MU_RC_PS_ZERO_BUFFER | MU_RC_PS_IS_VALID | MU_RC_PS_FROZEN_CHECK |
           MU_RC_PS_FILE_LOAD | MU_RC_WHISPY_FIX | MU_RC_NANA_DETERMINISM | MU_RC_PS_MONITOR |
           MU_RC_PREVENT_WOBBLING | MU_RC_CASTLE_WIND_RESET | MU_RC_KONGO_LOG_RESET;
}

/* The patches' short-circuit: an online match is running and the scene is not being left. */
static int hooks_on(void)
{
    return (mu_online.active || mu_online.console_active) && mu_gm_engine_state()->unk_C == 0;
}

static int global_frame(void)
{
    return (int) (mu_online.console_active ? mu_gm_engine_state()->unk_0 : mu_gm_engine_state()->unk_8);
}

/* The whole bookkeeping block, for the snapshot exclusion list (mu_online_abi.c). */
void* mu_online_state_address(void)
{
    return &mu_online;
}

unsigned int mu_online_state_size(void)
{
    return (unsigned int) sizeof mu_online;
}

/* ---- pads on the wire: Slippi's 12-byte report, the console PADStatus in big-endian order ---- */

static void pad_to_wire(u8* out, const PADStatus* pad)
{
    out[0] = (u8) (pad->button >> 8);
    out[1] = (u8) pad->button;
    out[2] = (u8) pad->stickX;
    out[3] = (u8) pad->stickY;
    out[4] = (u8) pad->substickX;
    out[5] = (u8) pad->substickY;
    out[6] = pad->triggerLeft;
    out[7] = pad->triggerRight;
    out[8] = pad->analogA;
    out[9] = pad->analogB;
    out[10] = (u8) pad->err;
    out[11] = 0;
}

static void wire_to_pad(PADStatus* pad, const u8* in)
{
    pad->button = (u16) (in[0] << 8 | in[1]);
    pad->stickX = (s8) in[2];
    pad->stickY = (s8) in[3];
    pad->substickX = (s8) in[4];
    pad->substickY = (s8) in[5];
    pad->triggerLeft = in[6];
    pad->triggerRight = in[7];
    pad->analogA = in[8];
    pad->analogB = in[9];
    pad->err = (s8) in[10];
}

/* ---- test inputs: a deterministic pad per online frame, so runs with and without network
 * jitter feed the game identical inputs and must reach identical states ---- */

static int test_inputs_enabled(void)
{
    return mu_online.test && mu_online.test_inputs != 0;
}

static void test_pad(u8* out, int frame, int port)
{
    /* Hold each choice for a few frames, as a person would. */
    u32 x = (u32) (frame / 6) * 2654435761u ^ (u32) (port + 1) * 40503u ^ mu_online.test_inputs;
    static const u16 buttons[] = {0, 0, 0, 0x0100, 0x0200, 0x0400, 0x0040, 0x0100, 0x0020, 0};
    PADStatus pad;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    memset(&pad, 0, sizeof pad);
    pad.stickX = (s8) ((int) ((x >> 3) % 161) - 80);
    pad.stickY = (s8) ((int) ((x >> 11) % 161) - 80);
    if ((x >> 20) % 5 == 0) {
        pad.substickX = (s8) ((int) ((x >> 23) % 161) - 80);
    }
    pad.button = buttons[(x >> 27) % 10];
    if (pad.button & 0x0040) {
        pad.triggerLeft = 140;
    }
    pad_to_wire(out, &pad);
}

/* ---- the finalized-state checksum (StartEngineLoop FN_COMPUTE_CHECKSUM) ---- */

static u32 float_bits(float f)
{
    u32 v;
    memcpy(&v, &f, 4);
    return v;
}

static u32 compute_checksum(void)
{
    u32 acc = 0;
    float sum = 0.0F;
    int i, k;
    for (i = 0; i < online_ports(); i++) {
        for (k = 0; k < 2; k++) {
            HSD_GObj* gobj = player_slots[i].player_entity[k];
            Fighter* fp;
            if (gobj == NULL) {
                continue;
            }
            fp = gobj->user_data;
#if MU_SQUAD
            /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
            if (gmSquad_Live()) acc ^= (u32) fp->kind;
#endif
            acc ^= (u32) fp->motion_id;
            acc ^= float_bits(fp->cur_pos.x);
            acc ^= float_bits(fp->cur_pos.y);
            acc ^= float_bits(fp->dmg.x1830_percent);
            acc ^= (u32) fp->x8_spawnNum;
            sum = sum + fp->cur_pos.x;
            sum = sum + fp->cur_pos.y;
            sum = sum + fp->dmg.x1830_percent;
        }
        acc ^= (u8) player_slots[i].stocks;
    }
    return (((acc >> 16) ^ (acc & 0xFFFF)) << 16) | ((u32) (s32) sum & 0xFFFF);
}

/* ---- host commands ---- */

static int command_into(unsigned int cmd, const u8* payload, unsigned int size, u8* response,
                        unsigned int capacity, unsigned int* response_size)
{
    unsigned int got = 0;
    int result = mu_online_abi_command(cmd, payload, size, response, capacity, &got);
    if (response_size != NULL) {
        *response_size = got;
    }
    if (result != 0) {
        logf_("online: host refused command %02X (%d)", (int) cmd, result, 0);
    }
    return result;
}

static int command(unsigned int cmd, const u8* payload, unsigned int size, u8* response,
                   unsigned int* response_size)
{
    return command_into(cmd, payload, size, response, RESPONSE_CAPACITY, response_size);
}

/* A Pro AI seat this machine plays (an Arena room's bot, a Direct game's added Pro AI, a Friends seat): the
 * model drives the seat's fighter here (shim/mu_practice_cpu.c mu_proai_seat_pad) and its controller goes
 * out as this machine's partner pad. Asked once per match; kept out of snapshots (a rollback never
 * changes which seat is whose). */
int mu_proai_seat_pad(int port, int pro, PADStatus* out);
static int proai_port = -1, proai_pro = 0;
static int match_20xx;   /* this match's setup has 20XX CPUs (owner 11-24): mu_20xx_ai_enabled online */
int mu_online_match_20xx(void) { return mu_online.active && match_20xx; }
static u8 proai_reply[RESPONSE_CAPACITY];
static void proai_seat_ask(void)
{
    unsigned int got = 0;
    /* The Friends Doubles home's partner tab on Pro AI: the room seated Player 2 on this machine, and the
     * model plays that seat (the host answers with its port). 0xFF: no Pro AI partner here. */
    const int model = mu_lobby_partner_pro_model();
    const u8 friends_pro = model >= 0 && model < 0x80 ? (u8) model : 0xFF;
    proai_port = -1;
    match_20xx = 0;
    if (command_into(CMD_PROAI_SEAT, &friends_pro, 1, proai_reply, sizeof proai_reply, &got) == 0 && got >= 4 && proai_reply[3]) {
        match_20xx = 1;
        logf_("online: this match's CPUs play the 20XX AI");
    }
    if (got >= 3 &&
        proai_reply[0] == 1 && proai_reply[1] < 4)
    {
        proai_port = proai_reply[1];
        proai_pro = proai_reply[2];
        logf_("online: Pro AI seat: port %d, pro %d (played on this machine)", proai_port, proai_pro);
    }
}
MU_EXCLUSIONS(proai_seat, MU_EXCLUDE(proai_port), MU_EXCLUDE(proai_pro), MU_EXCLUDE(proai_reply), MU_EXCLUDE(match_20xx))
/* The Pro AI seat this machine plays in the running network match, -1 none (shim/mu_controls.c: a
 * seat the model plays never takes a person's control options). */
int mu_online_proai_port(void)
{
    return mu_online.active ? proai_port : -1;
}

static int capture_state(int frame)
{
    u8 payload[32];
    memset(payload, 0, sizeof payload);
    put32(payload, (u32) frame);
    int result;
    result = command(CMD_CAPTURE_SAVESTATE, payload, sizeof payload, mu_online.match_state, NULL);
    mu_online.captures++;
    return result == 0;
}

static void correction_capture(void)
{
    int p;
    for (p = 0; p < online_ports(); p++) {
        HSD_GObj* gobj = player_slots[p].player_entity[0];
        Fighter* fp = gobj != NULL ? gobj->user_data : NULL;
        mu_online.corr_valid[p] = (u8) (fp != NULL);
        if (fp != NULL) {
            mu_online.corr_pos[p][0] = fp->cur_pos.x;
            mu_online.corr_pos[p][1] = fp->cur_pos.y;
        }
    }
    mu_online.corr_pending = 1;
}
static void correction_measure(void)
{
    int p;
    float worst = 0.0f;
    if (!mu_online.corr_pending) {
        return;
    }
    mu_online.corr_pending = 0;
    for (p = 0; p < online_ports(); p++) {
        HSD_GObj* gobj = player_slots[p].player_entity[0];
        Fighter* fp = gobj != NULL ? gobj->user_data : NULL;
        float dx, dy, d;
        if (fp == NULL || !mu_online.corr_valid[p]) {
            continue;
        }
        dx = fp->cur_pos.x - mu_online.corr_pos[p][0];
        dy = fp->cur_pos.y - mu_online.corr_pos[p][1];
        d = sqrtf(dx * dx + dy * dy);
        if (d > worst) {
            worst = d;
        }
    }
    mu_online.corr_n++;
    mu_online.corr_sum += worst;
    if (worst > mu_online.corr_max) {
        mu_online.corr_max = worst;
    }
    if (worst > 1.0f) {
        mu_online.corr_over1++;
    }
    if (worst > 4.0f) {
        mu_online.corr_over4++;
    }
}

static int load_state(int frame)
{
    u8 payload[32];
    memset(payload, 0, sizeof payload);   /* no preserve blocks: the bookkeeping is excluded */
    put32(payload, (u32) frame);
    int result;
    mu_alarms_hold();   /* timers are hardware: they keep their schedule through a load */
    result = command(CMD_LOAD_SAVESTATE, payload, sizeof payload, mu_online.match_state, NULL);
    mu_alarms_release();
    mu_online.loads++;
    return result == 0;
}

/* ---- self-test attribution: what lives at each range the re-simulation changed ---- */

static int inside(uintptr_t a, const void* base, uintptr_t size)
{
    return base != NULL && a >= (uintptr_t) base && a < (uintptr_t) base + size;
}

static const char* jobj_field(uintptr_t off)
{
    if (off < __builtin_offsetof(HSD_JObj, next)) return "obj";
    if (off < __builtin_offsetof(HSD_JObj, flags)) return "links";
    if (off < __builtin_offsetof(HSD_JObj, u)) return "flags";
    if (off < __builtin_offsetof(HSD_JObj, rotate)) return "u";
    if (off < __builtin_offsetof(HSD_JObj, scale)) return "rotate";
    if (off < __builtin_offsetof(HSD_JObj, translate)) return "scale";
    if (off < __builtin_offsetof(HSD_JObj, mtx)) return "translate";
    if (off < __builtin_offsetof(HSD_JObj, scl)) return "mtx";
    if (off < __builtin_offsetof(HSD_JObj, envelopemtx)) return "scl";
    if (off < __builtin_offsetof(HSD_JObj, aobj)) return "envelopemtx";
    return "tail";
}

/* Depth-first over a joint tree in the order the game numbers a model's parts. */
static int find_in_jobj_tree(HSD_JObj* jobj, uintptr_t a, int* index, int depth, char* out, unsigned int cap,
                             const char* owner)
{
    for (; jobj != NULL; jobj = jobj->next) {
        int self = (*index)++;
        if (inside(a, jobj, sizeof *jobj)) {
            snprintf(out, cap, "%s joint %d %s+%X", owner, self, jobj_field(a - (uintptr_t) jobj),
                     (int) (a - (uintptr_t) jobj));
            return 1;
        }
        if (inside(a, jobj->aobj, sizeof(HSD_AObj))) {
            snprintf(out, cap, "%s joint %d aobj+%X", owner, self, (int) (a - (uintptr_t) jobj->aobj));
            return 1;
        }
        if (!(jobj->flags & JOBJ_INSTANCE) && jobj->child != NULL && depth < 96 &&
            find_in_jobj_tree(jobj->child, a, index, depth + 1, out, cap, owner))
        {
            return 1;
        }
    }
    return 0;
}

static void describe_address(uintptr_t a, char* out, unsigned int cap)
{
    int p, pass;
    for (pass = 0; pass < 2; pass++) {
        for (p = 0; p < HSD_GObjLibInitData.p_link_max; p++) {
            HSD_GObj* gobj;
            for (gobj = HSD_GObjPLinkHead[p]; gobj != NULL; gobj = gobj->next) {
                char owner[48];
                int index = 0;
                unsigned int user_size = p == HSD_GOBJ_PLINK_FIGHTER ? sizeof(Fighter)
                                         : p == HSD_GOBJ_PLINK_ITEM  ? sizeof(Item)
                                                                     : 0;
                snprintf(owner, sizeof owner, "link %d class %d", p, (int) gobj->classifier);
                if (pass == 0) {
                    if (inside(a, gobj, sizeof *gobj)) {
                        snprintf(out, cap, "%s gobj+%X", owner, (int) (a - (uintptr_t) gobj));
                        return;
                    }
                    if (user_size != 0 && inside(a, gobj->user_data, user_size)) {
                        snprintf(out, cap, "%s %s+%X", owner, p == HSD_GOBJ_PLINK_FIGHTER ? "fighter" : "item",
                                 (int) (a - (uintptr_t) gobj->user_data));
                        return;
                    }
                    if (gobj->obj_kind == HSD_GObj_JObjKind &&
                        find_in_jobj_tree(gobj->hsd_obj, a, &index, 0, out, cap, owner))
                    {
                        return;
                    }
                } else {
                    /* Unknown sizes: a nearby guess, marked with '~'. */
                    if (user_size == 0 && inside(a, gobj->user_data, 0x200)) {
                        snprintf(out, cap, "~%s user+%X", owner, (int) (a - (uintptr_t) gobj->user_data));
                        return;
                    }
                    if (gobj->obj_kind != HSD_GObj_JObjKind && gobj->obj_kind != HSD_GOBJ_OBJ_NONE &&
                        inside(a, gobj->hsd_obj, 0x200))
                    {
                        snprintf(out, cap, "~%s obj kind %d+%X", owner, (int) gobj->obj_kind,
                                 (int) (a - (uintptr_t) gobj->hsd_obj));
                        return;
                    }
                }
            }
        }
    }
    snprintf(out, cap, "?");
}

static void selftest_attribute(const u8* reply, unsigned int size)
{
    unsigned int count, i, shown = 0;
    if (size < 4) {
        return;
    }
    count = be32(reply);
    for (i = 0; i < count && 4 + 8 * (i + 1) <= size && shown < 96; i++) {
        uintptr_t a = be32(reply + 4 + 8 * i);
        unsigned int length = be32(reply + 8 + 8 * i);
        char what[96], line[160];
        describe_address(a, what, sizeof what);
        snprintf(line, sizeof line, "online selftest:   %08X +%u = %s", (unsigned int) a, length, what);
        mu_online_abi_log(line);
        shown++;
    }
}

/* Diagnostic (MELEE_TRACE_RENEW=1): each pad renew with the retrace it ran in. */
static int trace_renew(void)
{
    static int on = -1;
    if (on < 0) {
        char* getenv(const char* name);
        const char* v = getenv("MELEE_TRACE_RENEW");
        on = v != NULL && v[0] == '1';
    }
    return on;
}

/* Diagnostic (MELEE_TRACE_P2P_PADS=1): the pads every port got on each frame, and the values the
 * frame's checksum is made of. Two games' logs of one match must be the same line for line on every
 * frame both ran with confirmed pads ("conf 1"); the first line that differs names the frame, the
 * port and the value. */
static int trace_pads(void)
{
    static int on = -1;
    if (on < 0) {
        char* getenv(const char* name);
        const char* v = getenv("MELEE_TRACE_P2P_PADS");
        on = v != NULL && v[0] == '1';
    }
    return on;
}

/* Tests only (MELEE_ONLINE_EARLY_CHECKSUMS=1, and only in a harness match, mu_online.test): compare
 * the opponent's checksums from frame 1 instead of from the first frame after the freeze, so a pair
 * run shows the two games agree through the entry animation too. Players never get this: a match
 * from the menus has mu_online.test 0 and keeps Slippi's rule. Frame 0 is never compared: the first
 * pads carry finalized frame 0 with checksum 0 (nothing is finalized yet) and the ring starts zeroed,
 * so a frame-0 entry is the "nothing yet" value, not a state. */
static int early_checksums(void)
{
    static int on = -1;
    if (on < 0) {
        char* getenv(const char* name);
        const char* v = getenv("MELEE_ONLINE_EARLY_CHECKSUMS");
        on = v != NULL && v[0] == '1';
    }
    return on && mu_online.test;
}

static void trace_pads_line(const PADStatus* stat, int frame, const char* kind, int confirmed)
{
    char line[260];
    int p, at;
    if (!trace_pads()) {
        return;
    }
    at = snprintf(line, sizeof line, "pads f %d %s conf %d local %d:", frame, kind, confirmed,
                  (int) mu_online.local_index);
    for (p = 0; p < online_ports(); p++) {
        u8 w[PAD_SIZE];
        int k;
        pad_to_wire(w, &stat[p]);
        line[at++] = ' ';
        for (k = 0; k < PAD_SIZE; k++) {
            at += snprintf(line + at, sizeof line - (unsigned int) at, "%02X", w[k]);
        }
    }
    mu_online_abi_log(line);
}

static void trace_state_line(int frame, u32 checksum)
{
    char line[360];
    int i, at;
    if (!trace_pads()) {
        return;
    }
    at = snprintf(line, sizeof line, "state f %d sum %08X seed %08X:", frame, (unsigned int) checksum,
                  (unsigned int) *HSD_RandSeedPtr);
    for (i = 0; i < online_ports(); i++) {
        HSD_GObj* gobj = player_slots[i].player_entity[0];
        Fighter* fp;
        if (gobj == NULL) {
            continue;
        }
        fp = gobj->user_data;
        at += snprintf(line + at, sizeof line - (unsigned int) at, " p%d m %d x %08X y %08X d %08X s %d", i,
                       (int) fp->motion_id, (unsigned int) float_bits(fp->cur_pos.x),
                       (unsigned int) float_bits(fp->cur_pos.y), (unsigned int) float_bits(fp->dmg.x1830_percent),
                       (int) (u8) player_slots[i].stocks);
    }
    mu_online_abi_log(line);
}

static void selftest_command(unsigned int cmd, int frame)
{
    u8 payload[4];
    unsigned int got = 0;
    put32(payload, (u32) frame);
    command(cmd, payload, sizeof payload, mu_online.match_state, &got);
    if (cmd == CMD_SELFTEST_COMPARE) {
        selftest_attribute(mu_online.match_state, got);
    }
}

/* ---- RenewInputs_Prefunction called by the rollback logic itself ---- */

static int trace_renew(void);

static void direct_renew(void)
{
    if (trace_renew()) {
        logf_("direct renew (rollback %d advance %d force %d)", mu_online.rollback_active,
              mu_online.frame_advance, mu_online.force_pad_renew);
    }
    mu_online.direct_renew++;
    fn_800195FC();
    mu_online.direct_renew--;
}

/* PreventPadAlarmDuringRollback (80019608), at the top of fn_800195FC. Nonzero: skip the renew. */
int mu_online_pad_alarm_gate(void)
{
    if (!hooks_on()) {
        return 0;
    }
    if (mu_online.console_active) return 1; /* body hook owns console queue injection */
    mu_online.frame_advance = 0;
    if (mu_online.direct_renew != 0) {
        return 0;   /* the rollback logic asked for these pads */
    }
    if (trace_renew()) {
        unsigned long long mu_ticks_since_start(void);
        logf_("pad alarm (rollback %d) retrace %d tbms %d", mu_online.rollback_active, (int) mu_online_abi_retrace_count(),
              (int) (mu_ticks_since_start() / 40500));
    }
    if (mu_online.rollback_active) {
        /* The pad alarm fired during a rollback: renew at the earliest safe time instead. */
        mu_online.force_pad_renew = 1;
        mu_online.gated_alarms++;
        return 1;
    }
    return 0;
}

/* ForceInputRefetchOnAdvance (80019614), at the end of fn_800195FC. */
void mu_online_after_pad_renew(void)
{
    if (hooks_on() && mu_online.frame_advance) {
        direct_renew();
    }
}

/* SkipNewInputFetchOnRollback (80376A20): nonzero skips PADRead during a rollback. */
int mu_online_skip_pad_read(void)
{
    if (mu_online.console_active) return mu_online.console_injecting;
    return hooks_on() && mu_online.rollback_active && !mu_online.rollback_should_load;
}

/* ---- TriggerSendInput (80376A28), right after PADRead in HSD_PadRenewRawStatus ----
 * Returns nonzero when the caller must return without queueing this sample. */

/* The early pad to predict `frame` of remote `count` with, when its confirmed pads only reach
 * `latest`: the newest entry for a frame after `latest` and not after `frame` (the host keeps them
 * oldest first), or NULL. A reply without the block, or a malformed one, predicts as before. */
static const u8* early_pad(int count, int frame, int latest)
{
    const u8* rxb = mu_online.rxb;
    unsigned int p = RXB_EARLY;
    int r;
    if (count < 0 || count >= REMOTES || mu_online.early_distrusted[count] || online_ports() != 4) {
        return NULL;   /* a Triples match has no early pads */
    }
    if (mu_online.rxb_size < RXB_EARLY + REMOTES) {
        return NULL;
    }
    for (r = 0; r < REMOTES; r++) {
        unsigned int n, i;
        const u8* found = NULL;
        if (p >= mu_online.rxb_size) {
            return NULL;
        }
        n = rxb[p++];
        if (n > EARLY_MAX || p + n * EARLY_ENTRY > mu_online.rxb_size) {
            return NULL;
        }
        if (r != count) {
            p += n * EARLY_ENTRY;
            continue;
        }
        for (i = 0; i < n; i++) {
            int target = (int) be32(rxb + p + i * EARLY_ENTRY);
            if (target > latest && target <= frame) {
                found = rxb + p + i * EARLY_ENTRY + 4;
            }
        }
        return found;
    }
    return NULL;
}

#include "mu_online_prediction.inc"

static void clamp_stick_at_rest(PADStatus* pad)
{
    if (pad->stickX >= -2 && pad->stickX <= 2 && pad->stickY >= -2 && pad->stickY <= 2) {
        pad->stickX = pad->stickY = 0;
    }
    if (pad->substickX >= -2 && pad->substickX <= 2 && pad->substickY >= -2 && pad->substickY <= 2) {
        pad->substickX = pad->substickY = 0;
    }
}

int mu_online_pad_renew(PADStatus* stat)
{
    int frame, i;
    u8 tx[TXB_SIZE + TXB_PARTNER_PAD_SIZE];
    u8 local[PAD_SIZE];
    unsigned int got = 0;
    PADStatus* source;

    if (mu_online.console_active) return console_pad_renew(stat);
    if (!hooks_on()) {
        return 0;
    }
    frame = mu_online.frame;

    /* The engine body of frame 0 takes a sample from before the match and online pad N goes to the
     * body of frame N (see the top of this file). On a console that sample is always there, left in
     * the queue by the menus. A game that boots straight into the match can reach the engine loop
     * with an empty queue: body 0 then took online pad 1 and that game applied every pad one frame
     * earlier than the other one. So no online pad is fetched before the engine loop waits for
     * pads, and when the loop finds the queue empty body 0 gets a neutral sample of its own. */
    if (!mu_online.engine_started) {
        if (!mu_online.loop_entered) {
            return 1;
        }
        if (HSD_PadGetRawQueueCount() == 0) {
            memset(stat, 0, (unsigned int) online_ports() * sizeof *stat);   /* pads 5 and 6 stay unplugged in a 4-port match */
            trace_pads_line(stat, 0, "first", 1);
            return 0;
        }
    }

    /* During a rollback the pads come from history; nothing new is sent. */
    if (mu_online.rollback_active && !mu_online.rollback_should_load) {
        int idx, p, slot = used_slot(frame);
        int known = mu_online.used_frame[slot] == frame;
        if (frame > mu_online.rollback_end_frame) {
            return 1;
        }
        if (frame == mu_online.selftest_ref_frame) {
            /* Back at the frame the self-test kept: the state must be byte for byte the same. */
            selftest_command(CMD_SELFTEST_COMPARE, frame);
            mu_online.selftest_ref_frame = -1;
        }
        if (trace_renew()) {
            logf_("renew resim frame %d retrace %d engine %d", frame, (int) mu_online_abi_retrace_count(),
                  global_frame());
        }
        memset(stat, 0, (unsigned int) online_ports() * sizeof *stat);   /* pads 5 and 6 stay unplugged in a 4-port match */
        if (mu_online.selftest_rollback && known) {
            /* Self-test: exactly the pads this frame used the first time (kept with their RNG
             * bits, so section 11 gives the frame the same seed bits again). */
            for (p = 0; p < online_ports(); p++) {
                wire_to_pad(&stat[p], mu_online.used_pads[slot][p]);
            }
            prepare_rng_bits(stat, frame, 0);
        } else {
            int confirmed;
            unsigned int predicted_players;
            idx = mu_online.local_inputs_idx - (mu_online.rollback_end_frame - frame + 1);
            if (idx < 0) {
                idx += LOCAL_INPUTS_COUNT;
            }
            wire_to_pad(&stat[mu_online.local_index], mu_online.local_inputs[idx]);
            confirmed = load_opponent_inputs(stat, frame, 1, &predicted_players);
            if (known && mu_online.used_confirmed[slot]) {
                /* Every pad of this frame was confirmed the first time: they must not change. */
                u8 wire[PAD_SIZE];
                for (p = 0; p < online_ports(); p++) {
                    pad_to_wire(wire, &stat[p]);
                    if (memcmp(wire, mu_online.used_pads[slot][p], PAD_SIZE) != 0) {
                        if (mu_online.resim_input_mismatches++ < 8) {
                            logf_("online: re-simulated frame %d port %d got other pads than it confirmed (rollback from %d)",
                                  frame, p, mu_online.stable_savestate_frame);
                        }
                        break;
                    }
                }
            }
            record_used_pads(stat, frame, confirmed);   /* with the RNG bits, as sent */
            prepare_rng_bits(stat, frame, predicted_players);
            trace_pads_line(stat, frame, "resim", confirmed);
        }
        mu_online.frame = frame + 1;
        mu_online.resim_frames++;
        return 0;
    }

    /* Section 1: every port is zero until shortly before the match unfreezes, so both players'
     * recordings agree on finalized frames. */
    if (frame < START_SYNC_FRAME - mu_online.delay) {
        memset(stat, 0, (unsigned int) online_ports() * sizeof *stat);   /* pads 5 and 6 stay unplugged in a 4-port match */
    }
    source = &stat[mu_online.input_source];
    if (test_inputs_enabled() && frame >= START_SYNC_FRAME - mu_online.delay) {
        test_pad(local, frame, mu_online.local_index);
        wire_to_pad(source, local);
    }
    /* Section 2: resting sticks read as exactly zero so noise does not cause rollbacks. */
    clamp_stick_at_rest(source);
    /* Section 3: a stale report repeats the previous one, as the game treats it locally. */
    if (source->err == -3) {
        wire_to_pad(source, mu_online.last_local);
    }
    pad_to_wire(mu_online.last_local, source);

    if (trace_renew()) {
        logf_("renew normal frame %d retrace %d engine %d", frame, (int) mu_online_abi_retrace_count(),
              global_frame());
    }
    /* Section 4: send this frame's pad. */
    put32(tx + TXB_FRAME, (u32) frame);
    put32(tx + TXB_FINALIZED_FRAME, (u32) mu_online.stable_finalized);
    /* The finalized frame's checksum; when it is not in the ring the previous value goes out
     * again, as the console's reused transfer buffer does. A zero would read as a desync. */
    for (i = 0; i < DESYNC_ENTRIES; i++) {
        if (mu_online.desync_local[i].frame == mu_online.stable_finalized) {
            mu_online.tx_checksum = mu_online.desync_local[i].checksum;
            break;
        }
    }
    put32(tx + TXB_FINALIZED_FRAME_CHECKSUM, mu_online.tx_checksum);
    tx[TXB_DELAY] = mu_online.delay;
    /* Slippi 3.7.0: the controller port the local inputs come from (the host watches it for input
     * suggestions) and whether the local player chose to leave (the host then disconnects). */
    tx[TXB_INPUT_SOURCE_INDEX] = mu_online.input_source;
    tx[TXB_IS_LEAVING] = mu_online.is_leaving;
    memcpy(tx + TXB_PAD, mu_online.last_local, PAD_SIZE);
    /* Slippi 3.7.0: the random bits the last reply gave go out with this pad. They reach the delay
     * buffer with it (section 8) and section 11 takes them out again before the game sees them. */
    tx[TXB_PAD + PAD_RNG_BITS] = mu_online.rxb[RXB_NEXT_PAD_RNG_BITS];
    /* A local partner's pad follows Slippi's payload: its inputs and an RNG bits byte that is always
     * zero (one machine's random bits go out once, with the main pad). */
    memset(tx + TXB_SIZE, 0, TXB_PARTNER_PAD_SIZE);
    if (proai_port >= 0) {
        /* This machine's Pro AI seat: the model's controller is the partner pad (no input from it: a
         * still pad, connected, so the seat stays in the match). */
        PADStatus ai;
        if (!mu_proai_seat_pad(proai_port, proai_pro, &ai)) {
            memset(&ai, 0, sizeof ai);
            ai.err = 0;
        }
        pad_to_wire(mu_online.last_partner, &ai);
        memcpy(tx + TXB_SIZE, mu_online.last_partner, PAD_RNG_BITS);
    } else {
        const int partner_source = mu_lobby_partner_source();
        if (partner_source >= 0 && partner_source < 4) {
            PADStatus* second = &stat[partner_source];
            clamp_stick_at_rest(second);
            if (second->err == -3) wire_to_pad(second, mu_online.last_partner);
            pad_to_wire(mu_online.last_partner, second);
            memcpy(tx + TXB_SIZE, mu_online.last_partner, PAD_RNG_BITS);
        }
    }
    /* The second pad is sampled only on a new frame. Its delayed confirmed queue comes back as
     * a virtual remote port: the existing remote_inputs history saves and replays it on rollback.
     * Early input/ring samples remain main-port only; this pad never needs an early prediction. */
    /* The host asks for its full reply capacity; the answer lands in the command buffer and the
     * part this code reads (Slippi's reply and the early inputs after it) is copied out. */
    mu_online.rxb_size = 0;
    if (command(CMD_ONLINE_INPUTS, tx, sizeof tx, mu_online.match_state, &got) == 0 && got >= RXB_SIZE) {
        if (got > sizeof mu_online.rxb) {
            got = sizeof mu_online.rxb;
        }
        memcpy(mu_online.rxb, mu_online.match_state, got);
    }
    if (got < RXB_SIZE) {
        if (got > 0) {
            mu_online.rxb[RXB_RESULT] = mu_online.match_state[0];
        }
        /* No answer, or the host's one-byte "disconnected" answer, means no match. This used to
         * return here with no pad sample, on every retrace: the engine then never ran again, so the
         * frame that shows DISCONNECTED and ends the game was never reached and the match stood
         * frozen until the player quit. The frame runs with the last pads the buffer holds. */
        if (!mu_online.disconnected) {
            logf_("online: host reply %d bytes (first byte %d) at frame %d, treating as disconnect", (int) got,
                  got > 0 ? (int) mu_online.rxb[RXB_RESULT] : -1, (int) mu_online.frame);
        }
        mu_online.disconnected = 1;
        mu_online.rxb[RXB_RESULT] = RESP_DISCONNECTED;
    } else {
        mu_online.rxb_size = got;
    }

    /* Section 5: skip (both games wait for each other), disconnect, or advance. */
    mu_online.frame_advance = 0;
    switch (mu_online.rxb[RXB_RESULT]) {
    case RESP_SKIP:
        if (!mu_online.game_over) {
            /* The start and the end of each run of skipped frames, so a report of a stalled match
             * shows how long this game waited (the host's own lines say why). */
            if (mu_online.skip_run++ == 0) {
                logf_("online: skip starts at frame %d", (int) mu_online.frame, 0, 0);
            }
            mu_online.skips++;
            return 1;
        }
        break;
    case RESP_DISCONNECTED:
        mu_online.disconnected = 1;
        break;
    case RESP_ADVANCE:
        mu_online.frame_advance = 1;
        mu_online.advances++;
        break;
    default:
        break;
    }

    if (mu_online.skip_run > 0) {
        logf_("online: skip ends at frame %d after %d retraces", (int) mu_online.frame, (int) mu_online.skip_run, 0);
        mu_online.skip_run = 0;
    }
    /* Section 6: the local player's pad is the one from `delay` frames ago. */
    wire_to_pad(&stat[mu_online.local_index], mu_online.delay_buffer[mu_online.delay_index]);
    /* Section 7: keep it for rollbacks. */
    memcpy(mu_online.local_inputs[mu_online.local_inputs_idx], mu_online.delay_buffer[mu_online.delay_index],
           PAD_SIZE);
    mu_online.local_inputs_idx = (u8) ((mu_online.local_inputs_idx + 1) % LOCAL_INPUTS_COUNT);
    /* Section 8: this frame's pad goes into the delay buffer. */
    memcpy(mu_online.delay_buffer[mu_online.delay_index], tx + TXB_PAD, PAD_SIZE);
    mu_online.delay_index = (u8) ((mu_online.delay_index + 1) % mu_online.delay);

    /* Section 9: rollback decision. */
    if (check_predictions(frame)) {
        return 1;
    }
    /* Self-test: roll back `depth` frames with the inputs already confirmed, so the re-simulated
     * state can be compared byte for byte with the state the game has now. */
    if (mu_online.selftest_every && frame % mu_online.selftest_every == 0 &&
        frame >= START_SYNC_FRAME + 20 && !mu_online.game_over && !mu_online.savestate_predicting &&
        (int) be32(mu_online.rxb + RXB_SMALLEST_LATEST) >= frame)
    {
        static int fighters_logged;
        if (!fighters_logged) {
            int p;
            fighters_logged = 1;
            for (p = 0; p < 4; p++) {
                HSD_GObj* gobj = player_slots[p].player_entity[0];
                if (gobj != NULL) {
                    logf_("online selftest: port %d fighter data at %08X (size %X)", p,
                          (int) (uintptr_t) gobj->user_data, (int) sizeof(Fighter));
                }
            }
        }
        selftest_command(CMD_SELFTEST_KEEP, frame);
        mu_online.selftest_ref_frame = frame;
        mu_online.selftest_rollback = 1;
        mu_online.rollback_active = 1;
        mu_online.rollback_should_load = 1;
        mu_online.rollback_end_frame = frame;
        mu_online.savestate_frame = frame - mu_online.selftest_depth;
        mu_online.frame = frame + 1;
        mu_online.rollbacks++;
        return 1;
    }
    /* Section 10: remote pads for this frame, predicted where they have not arrived. */
    {
        unsigned int predicted_players;
        int confirmed = load_opponent_inputs(stat, frame, 1, &predicted_players);
        record_used_pads(stat, frame, confirmed);   /* with the RNG bits, as sent */
        /* Section 11: this frame's RNG bits. */
        prepare_rng_bits(stat, frame, predicted_players);
        trace_pads_line(stat, frame, "run", confirmed);
    }
    mu_online.frame = frame + 1;
    return 0;
}

/* ---- ForceEngineOnRollback (801A4DB4), the pad wait of gm_801A4D34 ----
 * Nonzero: run the engine now (with *count samples). */
int mu_online_engine_gate(int* count)
{
    int mu_console_engine_gate(int* count);
    /* Match bodies own their stable rollback fields at frame_begin, before TE. */
    if (mu_online.console_active) return mu_console_engine_gate(count);
    if (mu_console_engine_gate(count)) {
        return 1;
    }
    if (!hooks_on()) {
        int mu_redo_gate(void);
        if (*count == 0 && mu_redo_gate()) {   /* tick re-run (shim/mu_redo.c), offline only */
            *count = 1;
            return 1;
        }
        return *count != 0;
    }
    mu_online.loop_entered = 1;
    if (*count <= 0 && !mu_online.rollback_active) {
        if (mu_online.force_pad_renew) {
            mu_online.force_pad_renew = 0;
            mu_online.forced_renews++;
            direct_renew();
        }
        return 0;
    }
    if (*count <= 0) {
        *count = 1;   /* one iteration drives the whole rollback */
    }
    mu_online.stable_rollback_active = mu_online.rollback_active;
    mu_online.stable_rollback_end_frame = mu_online.rollback_end_frame;
    mu_online.stable_should_load = mu_online.rollback_should_load;
    mu_online.stable_savestate_frame = mu_online.savestate_frame;
    return 1;
}

/* ---- StartEngineLoop (801A4DE4), top of each engine body ---- */

/* StartEngineLoop's HUD notices (drawn once, on the in-game text the online HUD set up). */
static void hud_message(float x, float y, float size, u32 rgba, const char* text)
{
    HSD_Text* t = mu_slippi_hud_text();
    GXColor color;
    int id;
    if (t == NULL) {
        return;
    }
    color.r = (u8) (rgba >> 24);
    color.g = (u8) (rgba >> 16);
    color.b = (u8) (rgba >> 8);
    color.a = (u8) rgba;
    id = HSD_SisLib_803A6B98(t, x, y, text);
    HSD_SisLib_803A7548(t, id, size, size);
    HSD_SisLib_803A74F0(t, id, &color);
}

static void end_game(void)
{
    mu_gmvs_request_online_end((int) mu_online.remote_index);
}

/* StartEngineLoop's desync recovery values for a checksum entry: the seconds left on the timer and
 * each port's percent and stocks (PlayerBlock_LoadDamage, PlayerBlock_LoadStocksLeft). Read only. */
static void store_recovery(DesyncLocal* entry)
{
    int i;
    entry->recovery_timer = gmVs_GetSceneState()->timer_seconds;
    for (i = 0; i < 4; i++) {
        entry->recovery_percent[i] = (u16) Player_GetDamage(i);
        entry->recovery_stocks[i] = (u8) Player_GetStocks(i);
    }
}

/* SinglesDetermineWinner (the online major's GPDO_FN_COMPUTE_RANKED_WINNER, set in every mode):
 * ports 1 and 2 only. A timeout goes to the ledge grab limit, then stocks, then percent; a
 * finished game to whoever has stocks left. -1 for a tie and for any other end (LRA-Start). */
static int singles_winner(int end_method)
{
    if (end_method == END_TIME) {
        int lg1 = (int) pl_80040AF0(0), lg2 = (int) pl_80040AF0(1);
        int s1, s2, d1, d2;
        if (!(lg1 > LGL_LIMIT && lg2 > LGL_LIMIT)) {   /* both over: the limit is ignored */
            if (lg1 > LGL_LIMIT) {
                return 1;
            }
            if (lg2 > LGL_LIMIT) {
                return 0;
            }
        }
        s1 = Player_GetStocks(0);
        s2 = Player_GetStocks(1);
        if (s1 != s2) {
            return s1 > s2 ? 0 : 1;
        }
        d1 = Player_GetDamage(0);
        d2 = Player_GetDamage(1);
        if (d1 != d2) {
            return d1 < d2 ? 0 : 1;
        }
        return -1;
    }
    if (end_method == END_GAME) {
        if (Player_GetStocks(0) != 0) {
            return 0;
        }
        if (Player_GetStocks(1) != 0) {
            return 1;
        }
    }
    return -1;
}

/* The tournament uses the exact existing singles winner, without changing online reports. */
int mu_online_singles_winner(int end_method) { return singles_winner(end_method); }

/* InitOnlinePlay's FN_HandleGameCompleted, called once the game can no longer be rolled back: the
 * game report (CMD_REPORT_GAME). Slippi runs CreateMatchEndData first only for the winner callback,
 * which reads nothing from it but the end method, so that is read from the scene state directly
 * and the match end data is left alone. Everything here is read only. */
static void report_game(void)
{
    u8 b[RGB_SIZE];
    VsSceneState* st = gmVs_GetSceneState();
    int end_method = st->match_result;
    int winner, i;

    memset(b, 0, sizeof b);
    b[RGB_ONLINE_MODE] = (u8) mu_online.mode;
    put32(b + RGB_FRAME_LENGTH, gm_801A4BA8());
    put32(b + RGB_GAME_INDEX, online_game_index);
#ifndef MU_NO_SLIPPI
    /* asm Online/Core/InitOnlinePlay.asm:320-400. */
    put32(b + RGB_TIEBREAKER_INDEX, mu_online.mode == ONLINE_MODE_RANKED ? mu_ranked_set()->tiebreak_num : 0);
#else
    put32(b + RGB_TIEBREAKER_INDEX, 0);
#endif
    winner = singles_winner(end_method);
#ifndef MU_NO_SLIPPI
    /* VSSceneDecide calls SinglesDetermineWinner independently of the report's
     * special -2/-3 winner codes (asm main.asm:628-655). Desync exits replay;
     * only a disconnect aborts the set. Preserve that result before encoding. */
    if (mu_online.mode == ONLINE_MODE_RANKED)
        mu_ranked_setup_game_result(winner, end_method, mu_online.disconnect_displayed);
#endif
    if (mu_online.disconnect_displayed) {
        winner = -3;
    } else if (mu_online.desync_displayed) {
        winner = -2;
    }
    b[RGB_WINNER_IDX] = (u8) winner;
    b[RGB_GAME_END_METHOD] = (u8) end_method;
    b[RGB_LRAS_INITIATOR] = (u8) (end_method == END_LRAS ? st->pauser : -1);
    put32(b + RGB_SYNCED_TIMER, mu_online.recovery_timer);
    for (i = 0; i < 4; i++) {
        u8* p = b + RGB_P1_RGPB + RGPB_SIZE * i;
        const StaticPlayer* sp = &player_slots[i];
        p[0] = (u8) sp->pkind;                                /* static block +0x8 */
        p[1] = (u8) sp->stocks;                               /* +0x8E */
        put32(p + 2, float_bits(sp->stale_moves.xC6C));       /* +0xC6C+188: damage done */
        p[6] = mu_online.recovery_stocks[i];
        p[7] = (u8) (mu_online.recovery_percent[i] >> 8);
        p[8] = (u8) mu_online.recovery_percent[i];
    }
    if (mu_online.start_data != NULL) {
        mu_replay_game_info_block(b + RGB_GAME_INFO_BLOCK, mu_online.start_data);
    }
    /* Triples: the six placements first (the report's winner reads ports 1 and 2 only), for the Friends
     * lobby's result. */
    if (online_ports() == ONLINE_PORTS) {
        extern void mu_replay_triples_game_end(int end_method);
        mu_replay_triples_game_end(end_method);
    }
    command(CMD_REPORT_GAME, b, sizeof b, mu_online.match_state, NULL);
    logf_("online: game report sent: game %d, winner %d, end method %d", (int) online_game_index, winner,
          end_method);
}

void mu_online_frame_begin(void)
{
    struct gm_80479D58_t* engine;
    int frame, i;
    u8* rxb;
    DesyncLocal* written;
    DesyncLocal* confirmed;

    if (mu_online.console_setup || mu_online.console_active) {
        console_frame_begin();
        return;
    }
    if (!hooks_on()) {
        return;
    }
    mu_online.engine_started = 1;
    mu_online_audio_frame_begin();
    engine = mu_gm_engine_state();
    frame = (int) engine->unk_8;
    rxb = mu_online.rxb;

    if (!mu_online.disconnect_displayed && mu_online.disconnected && !mu_online.game_over) {
        mu_online.disconnect_displayed = 1;
        logf_("online: disconnected at frame %d%s", frame, mu_online.is_leaving ? " (the local player left)" : "", 0);
        /* No message when the local player chose to leave (Slippi 3.7.0). */
        if (!mu_online.is_leaving) {
            hud_message(9.0F, -162.0F, 0.7F, 0xFF0000FFu, "DISCONNECTED");
        }
        end_game();
        /* Someone who chose to leave is not kept around as long (Slippi 3.7.0). */
        if (mu_online.is_leaving) {
            mu_gmvs_online_end_time(LEAVING_GAME_END_FRAMES);
        }
    }

    if (mu_online.stable_rollback_active) {
        if (mu_online.stable_should_load) {
            int ss = mu_online.stable_savestate_frame;
            if (frame < ss) {
                goto rollback_inputs_done;
            }
            if (frame > ss) {
                correction_capture();
                load_state(ss);
                mu_online.resim = 1;
                mu_online_abi_resim_phase(1);
            }
            {
                /* Rewind the raw pad queue: the loaded state predates the samples in it. */
                PadLibData* p = &HSD_PadLibData;
                int queued = lb_80019894();
                int w = p->qwrite - queued;
                if (w < 0) {
                    w += p->qnum;
                }
                p->qwrite = (u8) w;
                p->qcount = 0;
            }
            frame = global_frame();
            mu_online.frame = mu_online.savestate_frame;
            mu_online.savestate_predicting = 0;
            memset(mu_online.player_predicting, 0, sizeof mu_online.player_predicting);
            mu_online.rollback_should_load = 0;
            mu_online.stable_should_load = 0;
        }
        direct_renew();   /* the next pads of the rollback */
    }
rollback_inputs_done:

    /* The stable finalized frame never runs ahead of the frame being processed. */
    {
        int v = frame > mu_online.finalized ? mu_online.finalized : frame;
        if (v > mu_online.stable_finalized) {
            mu_online.stable_finalized = v;
        }
    }

    /* Checksum for this frame (overwritten when the frame is re-simulated). */
    {
        int offset = frame - (mu_online.desync_last_frame + 1);
        int idx = mu_online.desync_write_idx;
        if (offset >= 0) {
            mu_online.desync_write_idx = (u8) ((mu_online.desync_write_idx + 1) % DESYNC_ENTRIES);
            mu_online.desync_last_frame = frame;
        }
        idx = (idx + offset) % DESYNC_ENTRIES;
        if (idx < 0) {
            idx += DESYNC_ENTRIES;
        }
        mu_online.desync_local[idx].frame = frame;
        mu_online.desync_local[idx].checksum = compute_checksum();
        trace_state_line(frame, mu_online.desync_local[idx].checksum);
        written = &mu_online.desync_local[idx];
        store_recovery(written);
    }

    /* Compare the remote checksums of finalized frames with ours. The recovery values follow the
     * first of our entries a remote checksum matched; on frame 0 they are this frame's own, so a game
     * that never hears from the opponent still recovers to something that is not all zeroes. */
    confirmed = frame == 0 ? written : NULL;
    if (frame != 0 && !mu_online.desync_displayed) {
        int remote_count = rxb[RXB_OPNT_COUNT];
        /* The first frame compared: after the freeze (Slippi's rule), or frame 1 in a test. */
        const int first_compared = early_checksums() ? 1 : UNFREEZE_FRAME + 1;
        if (frame == 1 && first_compared == 1) {
            logf_("online: checksums compared from frame 1 (MELEE_ONLINE_EARLY_CHECKSUMS)");
        }
        for (i = 0; i < remote_count && i < online_remotes(); i++) {
            int cf = (int) be32(RX_DESYNC(i));
            u32 remote = be32(RX_DESYNC(i) + 4);
            int k;
            if (cf > mu_online.stable_finalized || cf < first_compared) {
                continue;
            }
            for (k = 0; k < DESYNC_ENTRIES; k++) {
                u32 local;
                int diff;
                if (mu_online.desync_local[k].frame != cf) {
                    continue;
                }
                if (confirmed == NULL) {
                    confirmed = &mu_online.desync_local[k];   /* Slippi's comparison always keeps the first */
                }
                local = mu_online.desync_local[k].checksum;
                diff = (int) (s16) (local & 0xFFFF) - (int) (s16) (remote & 0xFFFF);
                if (diff < -1 || diff > 1) {
                    mu_online.desync_displayed = 1;
                    logf_("online: DESYNC at frame %d (ours %08X, theirs %08X)", cf, (int) local,
                          (int) remote);
                    hud_message(9.0F, -140.0F, 0.5F, 0xFFB800FFu, "DESYNC DETECTED");
                    end_game();
                    goto desync_done;
                } else if (!mu_online.desync_risk_displayed && (local >> 16) != (remote >> 16)) {
                    mu_online.desync_risk_displayed = 1;
                    logf_("online: desync risk at frame %d (ours %08X, theirs %08X)", cf, (int) local,
                          (int) remote);
                    hud_message(228.0F, 194.0F, 0.38F, 0xFFB800FFu, "Desync Risk");
                }
                break;
            }
        }
    }
    /* Not desynced (a hard desync skips this): keep the agreed values for the game report. */
    if (confirmed != NULL) {
        mu_online.recovery_timer = confirmed->recovery_timer;
        memcpy(mu_online.recovery_stocks, confirmed->recovery_stocks, sizeof mu_online.recovery_stocks);
        memcpy(mu_online.recovery_percent, confirmed->recovery_percent, sizeof mu_online.recovery_percent);
    }

desync_done:
    /* Keep this frame while any remote input in it is predicted (the self-test keeps every frame). */
    if ((mu_online.savestate_predicting && frame > mu_online.stable_finalized) || mu_online.selftest_every) {
        capture_state(frame);
    }

    /* Despawn disconnected remote fighters (Slippi 3.7.0). The reply gives, per remote index, the
     * frame that player's fighter despawns on (0: it does not). It is compared with the frame being
     * processed rather than taken as an order, since the reply is not refreshed during a rollback
     * and these frames can be replayed. Remote index to port as section 10 maps it. The removal
     * itself only acts once (fn_8016EF98 does nothing without a fighter). */
    {
        int remote, port = 0;
        for (remote = 0; remote < online_remotes(); remote++, port++) {
            int despawn_frame;
            if (port == mu_online.local_index) {
                port++;
            }
            despawn_frame = (int) be32(RX_DESPAWN(remote));
            if (despawn_frame == 0 || frame < despawn_frame) {
                continue;
            }
            /* In Party mode someone already eliminated stays, so they still show on the results
             * screen. They have no fighter, and without teams no stock to come back with. */
            if (mu_online.mode == ONLINE_MODE_PARTY && Player_GetStocks(port) == 0) {
                continue;
            }
            if (fn_8016EF98(port)) {
                mu_online.despawns++;
                logf_("online: despawned port %d (remote %d) at frame %d", port, remote, frame);
            }
        }
    }

    /* The match is over once it has stayed over past the rollback window. */
    if (!mu_online.game_over) {
        if (gmVs_GetSceneState()->match_result == 0) {
            mu_online.game_end_frame = 0;
        } else {
            if (mu_online.game_end_frame == 0) {
                mu_online.game_end_frame = frame;
            }
            if (frame - mu_online.game_end_frame > ROLLBACK_MAX) {
                mu_online.game_over = 1;
                logf_("online: match over at frame %d", frame, 0, 0);
                /* HANDLE_GAME_CONFIRMED_OVER: the game end handler InitOnlinePlay installed. */
                report_game();
            }
        }
    }
}

/* ---- LoopEngineForRollback (801A5014), end of each engine body ---- */

/* What the render pass would have done between two re-simulated frames that gameplay depends on:
 * Slippi's ExecCameraTasks (the camera values behind tag and offscreen positions, and each awake
 * fighter's offscreen state), plus the fighter draw's bone matrix setup, which gameplay reads raw
 * on the next frame (ftDrawCommon_80080E18 refreshes them under the same condition). Without it a
 * re-simulated frame reads the previous frame's matrices and the rollback lands one rounding step
 * away from the state the game reaches without one. */
static void camera_tasks(void)
{
    HSD_GObj* gobj;
    Camera_8002A4AC(Camera_80030A50());
    HSD_CObjSetCurrent(Camera_80030A50()->hsd_obj);
    for (gobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; gobj != NULL; gobj = gobj->next) {
        Fighter* fp = gobj->user_data;
        if (!fp->is_sleeping && ftLib_80086A8C(gobj)) {
            mu_refresh_part_matrices(fp);
        }
    }
}

#include "mu_console_rollback.inc"

#if MU_SQUAD
/* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
 * Read-only observation AFTER a body, including every resimulation. The reply's
 * minimum remote latest input is the same frontier check_predictions uses.
 * Do not use command(): its shared response buffer is synchronized game memory. */
static void squad_digest_observe(int ending)
{
    u8 payload[9];
    if (!mu_online.test || !gmSquad_OnlineChosen() || !mu_online.active) return;
    payload[0] = ending ? MU_SQUAD_OP_DIGEST_END : MU_SQUAD_OP_DIGEST_FRAME;
    put32(payload + 1, (u32) (global_frame() - 1));
    put32(payload + 5, mu_online.disconnected ? (u32) -1 : be32(mu_online.rxb + RXB_SMALLEST_LATEST));
    mu_online_abi_command(MU_SQUAD_COMMAND, payload, sizeof payload, NULL, 0, NULL);
}
#else
#line 1409 /* Preserve ee1088f OFF debug declaration locations. */
#endif

int mu_online_frame_end(void)
{
    int current;
    if (mu_online.console_active) return console_frame_end();
    if (!hooks_on()) {
        int mu_redo_tracking(void);
        if (mu_redo_tracking()) {
            mu_online_audio_frame_end();   /* tick re-run (shim/mu_redo.c): this body's sounds */
        }
        return ENGINE_CONTINUE;
    }
    mu_online_audio_frame_end();
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
    squad_digest_observe(0);
#else
#line 1418 /* Preserve ee1088f OFF debug declaration locations. */
#endif
    current = global_frame() - 1;
    if (current > mu_online.latest_frame) {
        mu_online.latest_frame = current;
    }
    if (!mu_online.stable_rollback_active) {
        return ENGINE_CONTINUE;
    }
    if (current < mu_online.stable_rollback_end_frame) {
        camera_tasks();
        return ENGINE_RESIM;
    }
    mu_online.rollback_active = 0;
    mu_online.stable_rollback_active = 0;
    mu_online.selftest_rollback = 0;
    if (mu_online.resim) {
        mu_online.resim = 0;
        mu_online_abi_resim_phase(0);
        correction_measure();
    }
    mu_online_audio_rollback_end();   /* after resim is off, or deferred music would defer again */
    if (mu_online.rollback_start_ticks) {
        unsigned long long mu_ticks_since_start(void);
        unsigned int us = (unsigned int) ((mu_ticks_since_start() - mu_online.rollback_start_ticks) / 40.5);
        mu_online.rollback_start_ticks = 0;
        mu_online.rollback_us_total += us;
        if (us > mu_online.rollback_us_max) mu_online.rollback_us_max = us;
        if (us > 8000) mu_online.rollback_over_8ms++;
        if (us > 16683) mu_online.rollback_over_frame++;
    }
    if (trace_renew()) {
        unsigned long long mu_ticks_since_start(void);
        logf_("rollback done retrace %d tbms %d", (int) mu_online_abi_retrace_count(), (int) (mu_ticks_since_start() / 40500), 0);
    }
    return ENGINE_ROLLBACK_DONE;
}

/* ---- the match (InitOnlinePlay, 8016E748) ---- */

/* Slippi's SyncRNG: every frame the seed is the frame number (halves swapped) plus the match's
 * offset, so both games draw the same numbers whatever happened before.
 * Slippi 3.7.0 then hashes it (murmur3 finalizer): Melee's RNG turns a seed that steps by a fixed
 * amount each frame into rolls that also step by a fixed amount, and this makes each frame's rolls
 * unrelated to the last's. Last, the frame's RNG bits from section 11 (zero on most frames) go into
 * the top byte, after the hash: the top bits of each roll then land evenly spaced for the 256
 * values. The global frame here is the frame section 11 stored them under. */
static void sync_rng_proc(HSD_GObj* gobj)
{
    u32 f = (u32) global_frame();
    u32 seed = ((f << 16) | (f >> 16)) + mu_online.rng_offset;
    (void) gobj;
    seed ^= seed >> 16;
    seed *= 0x85EBCA6Bu;
    seed ^= seed >> 13;
    seed *= 0xC2B2AE35u;
    seed ^= seed >> 16;
    seed ^= (u32) mu_online.rng_seed_bits_history[f & (RNG_HISTORY_COUNT - 1)] << 24;
    *HSD_RandSeedPtr = seed;
}

static int fetch_match_state(unsigned int* size)
{
    return command(CMD_GET_MATCH_STATE, NULL, 0, mu_online.match_state, size);
}

/* The host's answer to "is a match requested" (online_test_match): 0 none, 1 the diagnostic
 * harness, 2 a lobby or peer-to-peer match without menus, 3 a peer-to-peer session that uses the
 * online menus. Kinds 1 and 2 boot straight into the match scene; kind 3 boots through the main
 * menu into the online character select (mu_slippi_menu.c) and never takes the paths below.
 *
 * p2p_menus and p2p_port are written once, by the first question to the host. That question is the
 * boot mode's (mu_replay_boot_mode, gm_801A4510), long before any match, and nothing writes them
 * again: every rollback snapshot holds the values the game already has, so a load changes nothing.
 * They are outside mu_online on purpose (arming a match clears that block). */
static int p2p_menus = -1;   /* -1 not asked yet, else whether the kind is 3 */
static u8 p2p_port;          /* the pad port the host named for the local player */

#if MU_SQUAD
/* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
 * Nonzero only while the host answers the launch question. Its diagnostic harness prepares the
 * squad lineup from inside that answer (h_online_test_match), and nothing else may. */
static int launch_query;
int mu_online_launch_query(void)
{
    return launch_query;
}
#endif

static int launch_kind(int* mode, int* input_port)
{
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
    int kind;
    launch_query = 1;
    kind = mu_online_abi_test_match(mode, input_port);
    launch_query = 0;
#else
    int kind = mu_online_abi_test_match(mode, input_port);
#endif
    if (p2p_menus < 0) {
        p2p_menus = kind == 3;
        p2p_port = kind == 3 ? (u8) (*input_port & 3) : 0;
    }
    return kind;
}

/* Whether this run is an online test match (the harness boots straight into the match scene). */
int mu_online_is_test_run(void)
{
    int mode = 0, input_port = 0;
    int kind = launch_kind(&mode, &input_port);
    return kind == 1 || kind == 2 ? kind : 0;
}

/* Whether this run is a peer-to-peer session played through the online menus (kind 3). */
int mu_online_p2p_menus(void)
{
    if (p2p_menus < 0) {
        int mode = 0, input_port = 0;
        launch_kind(&mode, &input_port);
    }
    return p2p_menus;
}

/* The pad port of the local player in such a session (0 in any other run). */
int mu_online_p2p_port(void)
{
    return mu_online_p2p_menus() ? p2p_port : 0;
}

/* The test harness boots straight into the match scene; this waits there until both games have
 * negotiated the match, the way Slippi's online character screen waits. */
int mu_online_test_wait(void)
{
    int mode = 0, input_port = 0;
    int kind;
    unsigned int last = 0xFFFFFFFFu, size = 0;
    kind = launch_kind(&mode, &input_port);
    if (kind != 1 && kind != 2) {
        return 0;   /* no request, or a session with menus (the character select waits instead) */
    }
    memset(&mu_online, 0, sizeof mu_online);
    mu_online.test = kind == 1;
    mu_online.mode = mode;
    mu_online.input_source = (u8) (input_port & 3);
    {
        char* getenv(const char* name);
        const char* seed = getenv("MELEE_ONLINE_TEST_INPUTS");
        if (mu_online.test && seed != NULL && seed[0] != '\0') {
            unsigned int v = 0;
            while (*seed >= '0' && *seed <= '9') {
                v = v * 10 + (unsigned int) (*seed++ - '0');
            }
            mu_online.test_inputs = v ? v : 1;
        }
    }
    mu_online.selftest_ref_frame = -1;
    {
        char* getenv(const char* name);
        const char* spec = getenv("MELEE_ONLINE_SELFTEST");   /* every:depth, e.g. 60:5 */
        int every = 0, depth = 0;
        while (spec != NULL && *spec >= '0' && *spec <= '9') {
            every = every * 10 + (*spec++ - '0');
        }
        if (spec != NULL && *spec == ':') {
            spec++;
            while (*spec >= '0' && *spec <= '9') {
                depth = depth * 10 + (*spec++ - '0');
            }
        }
        if (mu_online.test && every > 0 && depth >= 1 && depth < ROLLBACK_MAX) {
            mu_online.selftest_every = every;
            mu_online.selftest_depth = depth;
            logf_("online selftest: rolling back %d frames every %d frames", depth, every, 0);
        }
    }
    logf_("online test: waiting for the match (mode %d, input port %d)", mode, input_port, 0);
    for (;;) {
        unsigned int now = mu_online_abi_retrace_count();
        if (now != last) {
            last = now;
            if (fetch_match_state(&size) != 0 || size < MSRB_MIN_SIZE) {
                return 0;
            }
            if (mu_online.match_state[MSRB_CONNECTION_STATE] == MM_ERROR_ENCOUNTERED) {
                char line[260];
                snprintf(line, sizeof line, "online test: matchmaking failed: %.120s",
                         (const char*) mu_online.match_state + MSRB_ERROR_MSG);
                mu_online_abi_log(line);
                return 0;
            }
            if (mu_online.match_state[MSRB_CONNECTION_STATE] == MM_CONNECTION_SUCCESS &&
                mu_online.match_state[MSRB_LOCAL_READY] && mu_online.match_state[MSRB_REMOTE_READY])
            {
                mu_online.pending = 1;
                mu_online.wait_retrace = now;
                logf_("online test: match ready at retrace %d", (int) now, 0, 0);
                return 1;
            }
        }
        HOST_POLL();
    }
}

/* The negotiated match's game info block (console layout), for the scene's preloads. */
const unsigned char* mu_online_pending_game_info(void)
{
    return mu_online.pending ? mu_online.match_state + MSRB_GAME_INFO_BLOCK : NULL;
}

int mu_online_pending(void)
{
    return mu_online.pending;
}

#if MU_SQUAD
/* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
 * The menu-free diagnostic harness (launch kind 1), never a user-approved lobby match. */
int mu_online_harness(void)
{
    return mu_online.test;
}
#endif

void mu_online_start_melee(StartMeleeData* data)
{
    unsigned int size = 0;
    HSD_GObj* gobj;
    int i, delay;

    if (!mu_online.pending) {
        return;
    }
    mu_online.pending = 0;
    mu_online_rules_clear();
    /* asm InitOnlinePlay.asm:38-50: Ranked indices belong to the set. */
#ifndef MU_NO_SLIPPI
    if (mu_online.mode == ONLINE_MODE_RANKED) online_game_index = mu_ranked_set()->cur_game;
    else
#endif
        online_game_index++;
    /* FN_LoadMatchState: the match as the host negotiated it. */
    if (fetch_match_state(&size) != 0 || size < MSRB_MIN_SIZE) {
        mu_online_abi_log("online: no match state at match start; playing offline");
        return;
    }
    /* A 3.6.4-sized reply (the peer-to-peer host's) has no ranked info or bot flags: the recording
     * gets zeroes there, not what an earlier reply left in this buffer. */
    if (size < MSRB_SIZE) {
        memset(mu_online.match_state + size, 0, MSRB_SIZE - size);
    }
    mu_online.frame = 1;
    for (i = 0; i < USED_PADS_COUNT; i++) {
        mu_online.used_frame[i] = -1;
    }
    /* Triples: the host marks a match of 5 or 6 players after Slippi's match state. */
    mu_online.ports = (u8) (size > MSRB_TRIPLES && mu_online.match_state[MSRB_TRIPLES] == ONLINE_PORTS ? ONLINE_PORTS : 4);
    mu_online.local_index = (u8) (mu_online.match_state[MSRB_LOCAL_PLAYER_INDEX] % online_ports());
    mu_online.remote_index = (u8) (mu_online.match_state[MSRB_REMOTE_PLAYER_INDEX] % online_ports());
    mu_online.rng_offset = be32(mu_online.match_state + MSRB_RNG_OFFSET);
    *HSD_RandSeedPtr = mu_online.rng_offset;
#ifndef MU_NO_SLIPPI
    /* asm InitOnlinePlay.asm:148-189: ties are one stock / three minutes.
     * Exit/desync replay retains the host's recovery values. */
    if (mu_online.mode == ONLINE_MODE_RANKED && mu_ranked_set()->tiebreak_num &&
        mu_ranked_set()->last_end_mode != END_LRAS) {
        u8* info = mu_online.match_state + MSRB_GAME_INFO_BLOCK;
        put32(info + 0x10, 180);
        for (i = 0; i < 4; ++i) info[0x62 + 0x24*i] = 1;
    }
#endif
    mu_replay_apply_game_info(data, mu_online.match_state + MSRB_GAME_INFO_BLOCK);
    /* Triples: the scene counted its players before this match's game info was in; count again. */
    mu_triples_match_enter(data);
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
    /* The harness skips gmVsMelee_EnterVs in debug VS. Run its production squad entry
     * after importing match info and before fighters or Game Start are created. */
    if (mu_online.test && gm_GetCurrentGameMode() == GM_DEBUG_VS && gmSquad_OnlineChosen())
        gmSquad_OnEnterVs(data);
    if (gmSquad_OnlineReady()) gmSquad_ApplyLiveStart(data);
#endif
    /* Teams: the costume follows the team. The match state carries the colour each player picked,
     * and when all four picked the same team the host deals the teams out itself, so the picked
     * colour is the wrong team's for two of them (all four played in red). The console's online code
     * sets the costume from the team here; the menus already do (mu_slippi_menu.c splash_prep). */
    if (data->rules.is_teams) {
        for (i = 0; i < online_ports(); i++) {
            data->players[i].color = mu_slippi_team_costume(data->players[i].team + 1, data->players[i].ckind);
        }
    }
    mu_online.start_data = data;
    delay = mu_online.match_state[MSRB_DELAY_FRAMES];
    if (delay < MIN_DELAY) {
        delay = MIN_DELAY;
    }
    if (delay > MAX_DELAY) {
        delay = MAX_DELAY;
    }
    mu_online.delay = (u8) delay;
    mu_online.desync_last_frame = 0;
    /* No transformation from a held A: clear every port's copied buttons. */
    for (i = 0; i < online_ports(); i++) {
        HSD_PadCopyStatus[i].button = 0;
    }
    gobj = GObj_Create(4, 7, 0);
    HSD_GObj_SetupProc(gobj, sync_rng_proc, 0);
    /* Per-match rules the online codes read: mode, the frozen Stadium choice (the match state's
     * alt stage mode byte; 3.7.0 appends ranked info and bot flags after it), teams, the local port. */
    mu_online_rules_set(mu_online.mode, mu_online.match_state[MSRB_ALT_STAGE_MODE], data->rules.is_teams,
                        mu_online.local_index);
    mu_online.active = 1;
    proai_seat_ask();
    mu_online_audio_match_start(mu_online.local_index, mu_online.input_source);
    logf_("online: match starts, local port %d, delay %d, rng offset %08X", mu_online.local_index,
          mu_online.delay, (int) mu_online.rng_offset);
    if (online_ports() == ONLINE_PORTS) {
        logf_("online: Triples match, pads for %d ports", online_ports(), 0, 0);
    }
    /* SendGameInfo (8016E74C, right after InitOnlinePlay): every online match is recorded. */
    /* Triples matches are not recorded yet: Slippi's replay format holds four ports. */
    if (online_ports() != ONLINE_PORTS) {
        mu_replay_online_start(data, mu_online.match_state);
    } else {
        logf_("online: Triples match, not recorded (replays hold four players)", 0, 0, 0);
    }
    /* HANDLE_RANKED_MATCH_START: a Ranked game that is not a tiebreak tells the server it starts
     * (message 20 is game 1). Sent last, since the
     * reply buffer is the match state the lines above still read. */
    if (mu_online.mode == ONLINE_MODE_RANKED
#ifndef MU_NO_SLIPPI
        && !mu_ranked_set()->tiebreak_num
#endif
    ) {
        u8 status = (u8) (online_game_index + 19);
        command(CMD_REPORT_MATCH_STATUS, &status, 1, mu_online.match_state, NULL);
    }
}

/* FN_TX_FIND_MATCH (HandleInputsOnCSS): a new search starts the game index over. */
void mu_online_reset_game_index(void)
{
    online_game_index = 0;
}

/* What Slippi's recording reads from the online state (FlushFrameBuffer, SendGameEnd). */
void mu_online_record_state(int* stable_finalized, int* game_over, int* disconnected)
{
    *stable_finalized = mu_online.stable_finalized;
    *game_over = mu_online.game_over;
    *disconnected = mu_online.disconnected;
}

void mu_online_match_exit(void)
{
    if (!mu_online.active) {
        return;
    }
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
    squad_digest_observe(1);
#else
#line 1745 /* Preserve ee1088f OFF debug declaration locations. */
#endif
    mu_online_audio_match_exit();
    mu_online_rules_clear();
    logf_("online: match exit: %d rollbacks (deepest %d frames), %d re-simulated frames",
          (int) mu_online.rollbacks, (int) mu_online.max_depth, (int) mu_online.resim_frames);
    logf_("online: %d loads, %d captures, %d skips", (int) mu_online.loads, (int) mu_online.captures,
          (int) mu_online.skips);
    logf_("online: %d predictions compared, %d wrong; %d frames predicted from early pads",
          (int) mu_online.predictions, (int) mu_online.mispredictions, (int) mu_online.early_used);
    logf_("online: rollback time: longest %.2f ms, average %.2f ms, %d over 8 ms, %d over a frame",
          mu_online.rollback_us_max / 1000.0,
          mu_online.rollbacks ? (double) mu_online.rollback_us_total / mu_online.rollbacks / 1000.0 : 0.0,
          (int) mu_online.rollback_over_8ms, (int) mu_online.rollback_over_frame);
    logf_("online: early pad predictions right %d, wrong %d; %d rollbacks started by early pads",
          (int) mu_online.early_right, (int) mu_online.early_wrong, (int) mu_online.early_rollbacks);
    logf_("online: correction size: %d corrections, mean %.2f units, largest %.2f, %d over 1 unit, %d over 4",
          (int) mu_online.corr_n, mu_online.corr_n ? mu_online.corr_sum / mu_online.corr_n : 0.0,
          (double) mu_online.corr_max, (int) mu_online.corr_over1, (int) mu_online.corr_over4);
    logf_("online: %d re-simulated frames got other pads than they confirmed",
          (int) mu_online.resim_input_mismatches, 0, 0);
    logf_("online: %d pad alarms gated during rollbacks, %d forced renews", (int) mu_online.gated_alarms,
          (int) mu_online.forced_renews, 0);
    logf_("online: %d frames seeded with RNG bits, %d rollbacks for RNG bits, %d despawns",
          (int) mu_online.rng_bit_frames, (int) mu_online.rng_rollbacks, (int) mu_online.despawns);
    if (mu_online.resim) {
        mu_online.resim = 0;
        mu_online_abi_resim_phase(0);
    }
    mu_online.active = 0;
}

/* The scene loop can end on a re-simulated frame: a late remote input (a pause quit, the last hit)
 * rolls back and the match ends inside the re-run. The scene then waits for its last frame to be
 * shown before the exit handlers run, but while the host is in a re-simulation it skips retrace
 * waits, so that wait never ended: the game froze going into the next game. The rollback ends
 * here, as it would have on its last frame. */
void mu_online_scene_loop_exit(void)
{
    if (mu_online.console_active || mu_online.console_setup) {
        console_match_exit();
        return;
    }
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
     * The terminal body bypasses frame_end; record it before scene teardown. */
    squad_digest_observe(0);
#else
#line 1784 /* Preserve ee1088f OFF debug declaration locations. */
#endif
    if (!mu_online.resim) {
        return;
    }
    logf_("online: scene ended during a rollback at frame %d", (int) global_frame(), 0, 0);
    mu_online.rollback_active = 0;
    mu_online.stable_rollback_active = 0;
    mu_online.selftest_rollback = 0;
    mu_online.resim = 0;
    mu_online_abi_resim_phase(0);
    mu_online_audio_rollback_end();
}

/* The online menus' VS prep (Slippi's InitOnlinePlay runs in that scene): the next match scene is
 * an online match of this mode, read from this pad port. Starts clean, as the test harness does. */
void mu_online_arm(int mode, int input_port)
{
    memset(&mu_online, 0, sizeof mu_online);
    mu_online.mode = mode;
    mu_online.input_source = (u8) (input_port & 3);
    mu_online.selftest_ref_frame = -1;
    mu_online.pending = 1;
}
