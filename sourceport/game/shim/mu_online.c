/* Native Slippi online play.
 *
 * The console game gets online play from Slippi's patches: it exchanges pads with the host's Slippi
 * device every frame, saves and loads its state around rollbacks, and re-simulates the frames a
 * late remote input changed, all inside the engine loop (gm_801A4D34). This file is the native
 * equivalent, called from the same sites; each function names the patch it stands for. Nothing
 * here runs unless an online match is active, so offline play and replay playback are unchanged.
 *
 * Frame numbers: the online pad frame N (the number sent with each pad) is consumed by the engine
 * body whose unpaused-frame counter (gm_80479D58.unk_8) is N at its start; savestates and
 * checksums are keyed by that counter. Measured on the console code: 4746 of 4746 samples.
 *
 * All of this bookkeeping is excluded from snapshots: a rollback load restores the game, never the
 * record of which frames were predicted or confirmed.
 */
#include <string.h>
#include <dolphin/pad.h>
#include <melee/cm/camera.h>
#include <melee/ft/forward.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/types.h>
#include <melee/gm/gmvs.h>
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
void mu_alarms_hold(void);
void mu_alarms_release(void);
void mu_replay_game_info_block(unsigned char* out, const StartMeleeData* data);
u32 gm_801A4BA8(void);    /* MenuController_LoadTimer1: engine bodies run this match */
u32 pl_80040AF0(int slot);   /* PlayerBlock_GetCliffhangerStat: ledge grabs */
extern StaticPlayer player_slots[];

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
    CMD_SELFTEST_KEEP = 0xF0,     /* native test only: keep the state as a reference */
    CMD_SELFTEST_COMPARE = 0xF1,   /* native test only: compare the state with the reference */
    RESPONSE_CAPACITY = 4096,
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
    MSRB_SIZE = 962,
    MM_CONNECTION_SUCCESS = 4,
    MM_ERROR_ENCOUNTERED = 5,
    /* The inputs response (Slippi's RXB), byte offsets. */
    RXB_RESULT = 0,
    RXB_OPNT_COUNT = 1,
    RXB_OPNT_DESYNC = 2,
    RXB_OPNT_FRAME_NUMS = RXB_OPNT_DESYNC + 8 * REMOTES,
    RXB_SMALLEST_LATEST = RXB_OPNT_FRAME_NUMS + 4 * REMOTES,
    RXB_OPNT_INPUTS = RXB_SMALLEST_LATEST + 4,
    RXB_SHOULD_DESPAWN = RXB_OPNT_INPUTS + PAD_SIZE * RXB_INPUTS_COUNT * REMOTES,
    RXB_SIZE = RXB_SHOULD_DESPAWN + REMOTES,
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
    LGL_LIMIT = 45,   /* ledge grabs past this lose a timeout */
    END_TIME = 1,
    END_GAME = 2,
    END_LRAS = 7,
};

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
    u8 remote_index;
    u8 input_source;          /* the physical port the local pad is read from */
    u8 delay;
    int frame;                /* next online pad frame to fetch */
    unsigned int rng_offset;
    u8 last_local[PAD_SIZE];
    u8 delay_index;
    u8 delay_buffer[MAX_DELAY][PAD_SIZE];
    u8 rxb[RXB_SIZE];
    int rollback_active, rollback_should_load, rollback_end_frame;
    u8 local_inputs_idx;
    u8 local_inputs[LOCAL_INPUTS_COUNT][PAD_SIZE];
    u8 predicted_read[REMOTES], predicted_write[REMOTES];
    u8 predicted[REMOTES][PREDICTED_INPUTS_COUNT][PAD_SIZE];
    int savestate_predicting, savestate_frame;
    int player_savestate_frame[REMOTES];
    u8 player_predicting[REMOTES];
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
    unsigned int tx_checksum; /* the checksum field of the last pad sent (Slippi reuses its buffer) */
    int direct_renew;         /* depth of pad renews the rollback logic called itself */
    int resim;                /* re-simulating after a rollback load */
    /* The pads each recent frame used, every port in wire form, and whether every remote pad in
     * it was confirmed. A re-simulated confirmed frame must get the same pads again; the
     * self-test replays them, since the host drops remote pads once a frame is finalized. */
    int used_frame[USED_PADS_COUNT];
    u8 used_confirmed[USED_PADS_COUNT];
    u8 used_pads[USED_PADS_COUNT][4][PAD_SIZE];
    int selftest_rollback;    /* the running rollback is the self-test's */
    unsigned int resim_input_mismatches;
    unsigned int forced_renews, gated_alarms;
    /* Determinism self-test (MELEE_ONLINE_SELFTEST=every:depth, test harness only). */
    int selftest_every, selftest_depth, selftest_ref_frame;
    /* Diagnostics. */
    unsigned int rollbacks, resim_frames, max_depth, loads, captures, skips, advances, skip_run;
    unsigned int test_inputs;
    unsigned int wait_retrace;
    int loop_entered;         /* the scene's engine loop has reached its pad wait */
    int engine_started;       /* the engine body of frame 0 has begun */
    u8 match_state[RESPONSE_CAPACITY];
} MuOnlineState;

static MuOnlineState mu_online;

/* GPDO_CUR_GAME of Slippi's game prep data: the online games since the player last searched for an
 * opponent, 1 for the first. It outlives each match (arming clears mu_online) and only changes
 * outside one, as Slippi's own counter does. */
static unsigned int online_game_index;

static void logf_(const char* fmt, int a, int b, int c)
{
    char line[200];
    snprintf(line, sizeof line, fmt, a, b, c);
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

/* Slippi Dolphin always runs its Required code sections (General Codes, Recording, Online), so an
 * online match plays with every gameplay code in them, whatever this game was booted with. */
unsigned int mu_online_codes(void)
{
    if (!mu_online.active && !mu_online.pending) {
        return 0;
    }
    return MU_RC_INIT_STAGE_DATA | MU_RC_INIT_PLAYER_DATA | MU_RC_OFFSCREEN_DAMAGE | MU_RC_DEAD_UP_FALL |
           MU_RC_FD_BG_SEED | MU_RC_PS_ZERO_BUFFER | MU_RC_PS_IS_VALID | MU_RC_PS_FROZEN_CHECK |
           MU_RC_PS_FILE_LOAD | MU_RC_WHISPY_FIX | MU_RC_NANA_DETERMINISM | MU_RC_PS_MONITOR |
           MU_RC_PREVENT_WOBBLING;
}

/* The patches' short-circuit: an online match is running and the scene is not being left. */
static int hooks_on(void)
{
    return mu_online.active && mu_gm_engine_state()->unk_C == 0;
}

static int global_frame(void)
{
    return (int) mu_gm_engine_state()->unk_8;
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
    for (i = 0; i < 4; i++) {
        for (k = 0; k < 2; k++) {
            HSD_GObj* gobj = player_slots[i].player_entity[k];
            Fighter* fp;
            if (gobj == NULL) {
                continue;
            }
            fp = gobj->user_data;
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

static int command(unsigned int cmd, const u8* payload, unsigned int size, u8* response,
                   unsigned int* response_size)
{
    unsigned int got = 0;
    int result = mu_online_abi_command(cmd, payload, size, response, RESPONSE_CAPACITY, &got);
    if (response_size != NULL) {
        *response_size = got;
    }
    if (result != 0) {
        logf_("online: host refused command %02X (%d)", (int) cmd, result, 0);
    }
    return result;
}

static void capture_state(int frame)
{
    u8 payload[32];
    memset(payload, 0, sizeof payload);
    put32(payload, (u32) frame);
    command(CMD_CAPTURE_SAVESTATE, payload, sizeof payload, mu_online.match_state, NULL);
    mu_online.captures++;
}

static void load_state(int frame)
{
    u8 payload[32];
    memset(payload, 0, sizeof payload);   /* no preserve blocks: the bookkeeping is excluded */
    put32(payload, (u32) frame);
    mu_alarms_hold();   /* timers are hardware: they keep their schedule through a load */
    command(CMD_LOAD_SAVESTATE, payload, sizeof payload, mu_online.match_state, NULL);
    mu_alarms_release();
    mu_online.loads++;
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

static void trace_pads_line(const PADStatus* stat, int frame, const char* kind, int confirmed)
{
    char line[200];
    int p, at;
    if (!trace_pads()) {
        return;
    }
    at = snprintf(line, sizeof line, "pads f %d %s conf %d local %d:", frame, kind, confirmed,
                  (int) mu_online.local_index);
    for (p = 0; p < 4; p++) {
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
    char line[240];
    int i, at;
    if (!trace_pads()) {
        return;
    }
    at = snprintf(line, sizeof line, "state f %d sum %08X seed %08X:", frame, (unsigned int) checksum,
                  (unsigned int) *HSD_RandSeedPtr);
    for (i = 0; i < 4; i++) {
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
    return hooks_on() && mu_online.rollback_active && !mu_online.rollback_should_load;
}

/* ---- TriggerSendInput (80376A28), right after PADRead in HSD_PadRenewRawStatus ----
 * Returns nonzero when the caller must return without queueing this sample. */

static int load_opponent_inputs(PADStatus* stat, int frame, int predicting_allowed)
{
    int count, remote_port = 0, have_all = 1;
    u8* rxb = mu_online.rxb;
    for (count = 0; count < REMOTES; count++, remote_port++) {
        int latest, index = 0;
        if (remote_port == mu_online.local_index) {
            remote_port++;
        }
        latest = (int) be32(rxb + RXB_OPNT_FRAME_NUMS + 4 * count);
        if (latest - frame >= 0) {
            index = latest - frame;
        } else if (predicting_allowed && frame >= START_SYNC_FRAME - mu_online.delay &&
                   !mu_online.game_over)
        {
            /* Predicting: keep what we used for comparison once the real input arrives. */
            u8 w = mu_online.predicted_write[count];
            have_all = 0;
            memcpy(mu_online.predicted[count][w], rxb + RXB_OPNT_INPUTS + count * RXB_INPUTS_COUNT * PAD_SIZE,
                   PAD_SIZE);
            mu_online.predicted_write[count] = (u8) ((w + 1) % PREDICTED_INPUTS_COUNT);
            if (!mu_online.player_predicting[count]) {
                mu_online.player_savestate_frame[count] = frame;
                mu_online.player_predicting[count] = 1;
                mu_online.predicted_read[count] = w;
                if (!mu_online.savestate_predicting) {
                    mu_online.savestate_frame = frame;
                    mu_online.savestate_predicting = 1;
                }
            }
            index = 0;
        } else if (!predicting_allowed) {
            index = 0;
        }
        if (remote_port < 4) {
            wire_to_pad(&stat[remote_port],
                        rxb + RXB_OPNT_INPUTS + count * RXB_INPUTS_COUNT * PAD_SIZE + index * PAD_SIZE);
        }
    }
    if (have_all) {
        mu_online.finalized = frame;
    }
    return have_all;
}

static int used_slot(int frame)
{
    int slot = frame % USED_PADS_COUNT;
    return slot < 0 ? slot + USED_PADS_COUNT : slot;
}

static void record_used_pads(const PADStatus* stat, int frame, int confirmed)
{
    int slot = used_slot(frame), p;
    mu_online.used_frame[slot] = frame;
    mu_online.used_confirmed[slot] = (u8) confirmed;
    for (p = 0; p < 4; p++) {
        pad_to_wire(mu_online.used_pads[slot][p], &stat[p]);
    }
}

/* Section 9: compare earlier predictions with inputs that have now arrived. Returns 1 when a
 * rollback was triggered. */
static int check_predictions(int frame)
{
    u8* rxb = mu_online.rxb;
    int rollback_required = mu_online.rollback_active;
    int count, savestate, values_set = 0;

    if (!mu_online.savestate_predicting) {
        mu_online.player_predicting[0] = mu_online.player_predicting[1] =
            mu_online.player_predicting[2] = 0;
        return 0;
    }
    for (count = 0; count < REMOTES; count++) {
        while (mu_online.player_predicting[count]) {
            int latest = (int) be32(rxb + RXB_OPNT_FRAME_NUMS + 4 * count);
            int ss = mu_online.player_savestate_frame[count];
            int offset = latest - ss;
            const u8* actual;
            const u8* predicted;
            int t, differ = 0;
            if (offset < 0) {
                break;   /* the inputs for the savestate frame have not arrived yet */
            }
            if (ss <= mu_online.stable_finalized) {
                if (latest > ss) {
                    goto inputs_match;   /* already finalized: advance without comparing */
                }
                break;
            }
            actual = rxb + RXB_OPNT_INPUTS + count * RXB_INPUTS_COUNT * PAD_SIZE + offset * PAD_SIZE;
            predicted = mu_online.predicted[count][mu_online.predicted_read[count]];
            if ((actual[0] & 0x1F) != (predicted[0] & 0x1F) ||
                (actual[1] & 0x7F) != (predicted[1] & 0x7F) ||
                memcmp(actual + 2, predicted + 2, 4) != 0)
            {
                differ = 1;
            }
            for (t = 6; t < 8 && !differ; t++) {
                if (actual[t] <= 42 && predicted[t] <= 42) {
                    continue;   /* both in the trigger dead zone */
                }
                if (actual[t] != predicted[t]) {
                    differ = 1;
                }
            }
            if (differ) {
                rollback_required = 1;
                break;
            }
        inputs_match:
            mu_online.player_savestate_frame[count] = ss + 1;
            mu_online.predicted_read[count] =
                (u8) ((mu_online.predicted_read[count] + 1) % PREDICTED_INPUTS_COUNT);
            if (mu_online.predicted_read[count] == mu_online.predicted_write[count]) {
                break;   /* caught up with the prediction */
            }
        }
    }
    /* The savestate frame is the lowest frame any remote is still predicted from. */
    savestate = mu_online.stable_finalized;
    for (count = 0; count < REMOTES; count++) {
        if (mu_online.player_predicting[count] != 1) {
            continue;
        }
        if (!values_set || mu_online.player_savestate_frame[count] < savestate) {
            savestate = mu_online.player_savestate_frame[count];
        }
        values_set = 1;
    }
    mu_online.savestate_frame = savestate;
    mu_online.finalized = savestate;
    if (savestate > (int) be32(rxb + RXB_SMALLEST_LATEST)) {
        mu_online.finalized = (int) be32(rxb + RXB_SMALLEST_LATEST);
    }
    if (rollback_required) {
        if (trace_renew()) {
            unsigned long long mu_ticks_since_start(void);
            logf_("rollback trigger frame %d retrace %d tbms %d", frame,
                  (int) mu_online_abi_retrace_count(), (int) (mu_ticks_since_start() / 40500));
        }
        mu_online.selftest_rollback = 0;
        mu_online.rollback_active = 1;
        mu_online.rollback_should_load = 1;
        mu_online.rollback_end_frame = frame;
        mu_online.frame = frame + 1;
        mu_online.rollbacks++;
        if ((unsigned int) (frame - savestate) > mu_online.max_depth) {
            mu_online.max_depth = (unsigned int) (frame - savestate);
        }
        return 1;
    }
    for (count = 0; count < REMOTES; count++) {
        if (mu_online.player_predicting[count] == 1 &&
            mu_online.predicted_read[count] == mu_online.predicted_write[count])
        {
            mu_online.player_predicting[count] = 0;
        }
    }
    for (count = 0; count < REMOTES; count++) {
        if (mu_online.player_predicting[count] == 1) {
            return 0;
        }
    }
    mu_online.savestate_predicting = 0;
    return 0;
}

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
    u8 tx[25];
    u8 local[PAD_SIZE];
    unsigned int got = 0;
    PADStatus* source;

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
            memset(stat, 0, 4 * sizeof *stat);
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
        memset(stat, 0, 4 * sizeof *stat);
        if (mu_online.selftest_rollback && known) {
            /* Self-test: exactly the pads this frame used the first time. */
            for (p = 0; p < 4; p++) {
                wire_to_pad(&stat[p], mu_online.used_pads[slot][p]);
            }
        } else {
            int confirmed;
            idx = mu_online.local_inputs_idx - (mu_online.rollback_end_frame - frame + 1);
            if (idx < 0) {
                idx += LOCAL_INPUTS_COUNT;
            }
            wire_to_pad(&stat[mu_online.local_index], mu_online.local_inputs[idx]);
            confirmed = load_opponent_inputs(stat, frame, 1);
            if (known && mu_online.used_confirmed[slot]) {
                /* Every pad of this frame was confirmed the first time: they must not change. */
                u8 wire[PAD_SIZE];
                for (p = 0; p < 4; p++) {
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
            record_used_pads(stat, frame, confirmed);
            trace_pads_line(stat, frame, "resim", confirmed);
        }
        mu_online.frame = frame + 1;
        mu_online.resim_frames++;
        return 0;
    }

    /* Section 1: every port is zero until shortly before the match unfreezes, so both players'
     * recordings agree on finalized frames. */
    if (frame < START_SYNC_FRAME - mu_online.delay) {
        memset(stat, 0, 4 * sizeof *stat);
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
    put32(tx, (u32) frame);
    put32(tx + 4, (u32) mu_online.stable_finalized);
    /* The finalized frame's checksum; when it is not in the ring the previous value goes out
     * again, as the console's reused transfer buffer does. A zero would read as a desync. */
    for (i = 0; i < DESYNC_ENTRIES; i++) {
        if (mu_online.desync_local[i].frame == mu_online.stable_finalized) {
            mu_online.tx_checksum = mu_online.desync_local[i].checksum;
            break;
        }
    }
    put32(tx + 8, mu_online.tx_checksum);
    tx[12] = mu_online.delay;
    memcpy(tx + 13, mu_online.last_local, PAD_SIZE);
    if (command(CMD_ONLINE_INPUTS, tx, sizeof tx, mu_online.rxb, &got) != 0 || got < RXB_SIZE) {
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
    memcpy(mu_online.delay_buffer[mu_online.delay_index], tx + 13, PAD_SIZE);
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
        int confirmed = load_opponent_inputs(stat, frame, 1);
        record_used_pads(stat, frame, confirmed);
        trace_pads_line(stat, frame, "run", confirmed);
    }
    mu_online.frame = frame + 1;
    return 0;
}

/* ---- ForceEngineOnRollback (801A4DB4), the pad wait of gm_801A4D34 ----
 * Nonzero: run the engine now (with *count samples). */
int mu_online_engine_gate(int* count)
{
    if (!hooks_on()) {
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
    /* GPDO_TIEBREAK_GAME_NUM: only Ranked's set logic (the GameSetup state, not ported) sets it. */
    put32(b + RGB_TIEBREAKER_INDEX, 0);
    winner = singles_winner(end_method);
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
        logf_("online: disconnected at frame %d", frame, 0, 0);
        hud_message(9.0F, -162.0F, 0.7F, 0xFF0000FFu, "DISCONNECTED");
        end_game();
    }

    if (mu_online.stable_rollback_active) {
        if (mu_online.stable_should_load) {
            int ss = mu_online.stable_savestate_frame;
            if (frame < ss) {
                goto rollback_inputs_done;
            }
            if (frame > ss) {
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
            mu_online.player_predicting[0] = mu_online.player_predicting[1] =
                mu_online.player_predicting[2] = 0;
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
        for (i = 0; i < remote_count && i < REMOTES; i++) {
            int cf = (int) be32(rxb + RXB_OPNT_DESYNC + 8 * i);
            u32 remote = be32(rxb + RXB_OPNT_DESYNC + 8 * i + 4);
            int k;
            if (cf > mu_online.stable_finalized || cf <= UNFREEZE_FRAME) {
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

int mu_online_frame_end(void)
{
    int current;
    if (!hooks_on()) {
        return ENGINE_CONTINUE;
    }
    mu_online_audio_frame_end();
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
    }
    mu_online_audio_rollback_end();   /* after resim is off, or deferred music would defer again */
    if (trace_renew()) {
        unsigned long long mu_ticks_since_start(void);
        logf_("rollback done retrace %d tbms %d", (int) mu_online_abi_retrace_count(), (int) (mu_ticks_since_start() / 40500), 0);
    }
    return ENGINE_ROLLBACK_DONE;
}

/* ---- the match (InitOnlinePlay, 8016E748) ---- */

/* Slippi's SyncRNG: every frame the seed is the frame number (halves swapped) plus the match's
 * offset, so both games draw the same numbers whatever happened before. */
static void sync_rng_proc(HSD_GObj* gobj)
{
    u32 f = (u32) global_frame();
    (void) gobj;
    *HSD_RandSeedPtr = ((f << 16) | (f >> 16)) + mu_online.rng_offset;
}

static int fetch_match_state(unsigned int* size)
{
    return command(CMD_GET_MATCH_STATE, NULL, 0, mu_online.match_state, size);
}

/* Whether this run is an online test match (the harness boots straight into the match scene). */
int mu_online_is_test_run(void)
{
    int mode, input_port;
    return mu_online_abi_test_match(&mode, &input_port);
}

/* The test harness boots straight into the match scene; this waits there until both games have
 * negotiated the match, the way Slippi's online character screen waits. */
int mu_online_test_wait(void)
{
    int mode, input_port;
    int launch_kind;
    unsigned int last = 0xFFFFFFFFu, size = 0;
    launch_kind = mu_online_abi_test_match(&mode, &input_port);
    if (!launch_kind) {
        return 0;
    }
    memset(&mu_online, 0, sizeof mu_online);
    mu_online.test = launch_kind == 1;
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
            if (fetch_match_state(&size) != 0 || size < MSRB_SIZE) {
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
    /* The game index counts up before every game. Slippi leaves Ranked's to its set logic (the
     * GameSetup state), which is not ported, so Ranked counts here too: 1, 2, 3 from its search. */
    online_game_index++;
    /* FN_LoadMatchState: the match as the host negotiated it. */
    if (fetch_match_state(&size) != 0 || size < MSRB_SIZE) {
        mu_online_abi_log("online: no match state at match start; playing offline");
        return;
    }
    mu_online.frame = 1;
    for (i = 0; i < USED_PADS_COUNT; i++) {
        mu_online.used_frame[i] = -1;
    }
    mu_online.local_index = mu_online.match_state[MSRB_LOCAL_PLAYER_INDEX] & 3;
    mu_online.remote_index = mu_online.match_state[MSRB_REMOTE_PLAYER_INDEX] & 3;
    mu_online.rng_offset = be32(mu_online.match_state + MSRB_RNG_OFFSET);
    *HSD_RandSeedPtr = mu_online.rng_offset;
    mu_replay_apply_game_info(data, mu_online.match_state + MSRB_GAME_INFO_BLOCK);
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
    for (i = 0; i < 4; i++) {
        HSD_PadCopyStatus[i].button = 0;
    }
    gobj = GObj_Create(4, 7, 0);
    HSD_GObj_SetupProc(gobj, sync_rng_proc, 0);
    /* Per-match rules the online codes read: mode, the frozen Stadium choice (last match state
     * byte), teams, the local port. */
    mu_online_rules_set(mu_online.mode, mu_online.match_state[MSRB_SIZE - 1], data->rules.is_teams,
                        mu_online.local_index);
    mu_online.active = 1;
    mu_online_audio_match_start(mu_online.local_index, mu_online.input_source);
    logf_("online: match starts, local port %d, delay %d, rng offset %08X", mu_online.local_index,
          mu_online.delay, (int) mu_online.rng_offset);
    /* SendGameInfo (8016E74C, right after InitOnlinePlay): every online match is recorded. */
    mu_replay_online_start(data, mu_online.match_state);
    /* HANDLE_RANKED_MATCH_START: a Ranked game that is not a tiebreak tells the server it starts
     * (message 20 is game 1). No game is a tiebreak here (see report_game). Sent last, since the
     * reply buffer is the match state the lines above still read. */
    if (mu_online.mode == ONLINE_MODE_RANKED) {
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
    mu_online_audio_match_exit();
    mu_online_rules_clear();
    logf_("online: match exit: %d rollbacks (deepest %d frames), %d re-simulated frames",
          (int) mu_online.rollbacks, (int) mu_online.max_depth, (int) mu_online.resim_frames);
    logf_("online: %d loads, %d captures, %d skips", (int) mu_online.loads, (int) mu_online.captures,
          (int) mu_online.skips);
    logf_("online: %d re-simulated frames got other pads than they confirmed",
          (int) mu_online.resim_input_mismatches, 0, 0);
    logf_("online: %d pad alarms gated during rollbacks, %d forced renews", (int) mu_online.gated_alarms,
          (int) mu_online.forced_renews, 0);
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
