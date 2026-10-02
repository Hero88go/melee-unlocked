/* Slippi replay playback, the game side: the native form of Slippi's playback codes
 * (Playback/Core and the Common codes they use). Each function is called from the decomp site the
 * matching code patches, under MU_NATIVE; run-source/slp-playback-20260925/design.md maps every
 * write. mu_replay_abi.c moves the host's data across the boundary.
 *
 * What the playback also needs from the replay's own Gecko list (NeutralSpawn, UCF 0.84, the online
 * gameplay codes) is carried by the host's MU_REPLAY_CODE_* mask; each native code checks it.
 */
#include <string.h>
#include <melee/cm/camera.h>
#include <melee/cm/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/types.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/gm_1601.h>
#include <melee/it/forward.h>
#include <melee/it/types.h>
#include <melee/lb/lblanguage.h>
#include <melee/mn/mnname.h>
#include <melee/gr/stage.h>
#include <melee/gm/types.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/types.h>
#include <melee/mn/types.h>
#include <melee/mp/mpcoll.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/random.h>

int mu_replay_abi_active(void);
unsigned int mu_replay_abi_codes(void);
const unsigned char* mu_replay_abi_game_info(unsigned int* seed);
int mu_replay_abi_resync(void);
int mu_replay_abi_ps_frozen_toggle(void);
int mu_replay_abi_fetch(int frame);
int mu_replay_abi_last_result(void);
int mu_replay_abi_frame_seed(unsigned int* seed);
int mu_replay_abi_character(int port, int follower, float* f, unsigned int* buttons,
                            unsigned int* action_state, unsigned int* seed, unsigned char* raw);
int mu_replay_abi_stock_steal(int frame, int port);
void mu_replay_abi_event(unsigned int command, const unsigned char* payload, unsigned int size);
void mu_replay_abi_finished(void);
void mu_replay_abi_log(const char* text);
int snprintf(char* buffer, __SIZE_TYPE__ size, const char* format, ...);

enum { REPLAY_CONTINUE = 1, REPLAY_TERMINATE = 2 };
#define FIRST_FRAME (-123)

/* Slippi's frameIndex (r13 - 0x49AC): -123 on the match scene's first frame. */
static int frame_index = FIRST_FRAME;
static int finished;
static int emitted_frame = FIRST_FRAME - 1;
static int recording_active;
/* The native recording (Slippi's Recording codes): recording_online when the match is online.
 * frame_has_events is Slippi's frame buffer being non-empty (FlushFrameBuffer only then writes a
 * bookend). lcancel_status is the byte ExtendPlayerBlock adds to each fighter (fp+0x25FF), kept
 * per port and follower. All of it is game state for rollbacks, as on the console. */
static int recording_online;
static int frame_has_events;
static u8 lcancel_status[4][2];
static void (*chained_frame_end)(void);
static void emit_frame_start(void);
static void emit_frame_bookend(void);
static void emit_game_start(const StartMeleeData* data, const u8* msrb);
static void start_recording(StartMeleeData* data, const u8* msrb);
char* getenv(const char* name);

int mu_replay_on(void)
{
    return mu_replay_abi_active();
}

/* A replay's gameplay code: on when the replay carries it, and in every online match (Slippi
 * Dolphin always runs the sections these codes come from). */
int mu_replay_code(unsigned int code)
{
    return (mu_replay_abi_codes() & code) != 0 || (mu_online_codes() & code) != 0;
}

/* A code the native game runs as part of the General Codes: during playback it runs only when the
 * replay carries it too, as Legacy's playback build applies it only from the replay's own list. */
int mu_replay_allows(unsigned int code)
{
    return !mu_replay_abi_active() || (mu_replay_abi_codes() & code) != 0;
}

int mu_replay_frame_index(void)
{
    return frame_index;
}

static void logf_(const char* fmt, int a, int b)
{
    char line[160];
    snprintf(line, sizeof line, fmt, a, b);
    mu_replay_abi_log(line);
}

/* ---- boot (Boot to Playback Scene, 801a45a0) ---- */

/* gm_801A4510: the first game mode. Slippi boots into major 0x0E (GM_DEBUG_VS). An online test
 * match (the harness, no menus) boots the same way. */
int mu_online_is_test_run(void);
int mu_online_test_wait(void);
const unsigned char* mu_online_pending_game_info(void);

void mu_replay_boot_mode(u8* mode)
{
    if (mu_replay_on() || mu_online_is_test_run()) {
        *mode = GM_DEBUG_VS;
    }
}

/* ---- the waiting scene's preload (SceneThink_Playback, 801a6348) ----
 * Legacy runs this in the results state (id 3) before the match state; natively the match state is
 * entered first and this runs right before its own preload (gm_801A4014). It only loads files and
 * sounds. */
static u32 be32(const u8* p) { return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3]; }
static u16 be16(const u8* p) { return (u16) (p[0] << 8 | p[1]); }
static float bef(const u8* p) { u32 v = be32(p); float f; memcpy(&f, &v, 4); return f; }

void mu_replay_prepare_scene(void)
{
    unsigned int seed;
    const u8* info = mu_replay_abi_game_info(&seed);
    PreloadedGameModeState* scene;
    u64 mask = 0;
    int i;

    if (info == NULL && mu_online_test_wait()) {
        info = mu_online_pending_game_info();
    }
    if (info == NULL) {
        return;
    }
    scene = lbDvd_GetPreloadCacheScene();
    for (i = 0; i < 4; i++) {
        const u8* p = info + 0x60 + 0x24 * i;
        scene->game_cache.entries[i].char_id = (s8) p[0];
#ifdef MU_AKANEIA_FIGHTERS
        {
            const int ckind = mu_ak_ckind_from_mex((s8) p[0]);
            scene->game_cache.entries[i].char_id = ckind >= 0 ? (s8) ckind : ChKind_None;
        }
#endif
        scene->game_cache.entries[i].color = p[3];
    }
    lbDvd_80018254();
    lbDvd_80018C2C(199);
    lbDvd_80017700(4);
    lbAudioAx_80026F2C(28);
    for (i = 0; i < 6; i++) {
        const u8* p = info + 0x60 + 0x24 * i;
        if ((s8) p[0] != 33) {   /* FN_GetFighterNum: 33 is "no fighter" */
            int ckind = (s8) p[0];
#ifdef MU_AKANEIA_FIGHTERS
            ckind = mu_ak_ckind_from_mex(ckind);
            if (ckind < 0) continue;
#endif
            mask |= lbAudioAx_80026E84((CharacterKind) ckind);
        }
    }
    mask |= lbAudioAx_80026EBC((StKind) be16(info + 0xE));
    lbAudioAx_8002702C(4, mask);
    lbAudioAx_80027168();
    lbAudioAx_80024F6C();
}

/* ---- RestoreGameInfo (8016e748, fn_8016E730 after db_Setup) ---- */

#define BIT(b, n) ((g[b] >> (7 - (n))) & 1)

static void convert_rules(StartMeleeRules* r, const u8* g)
{
    r->match_kind = g[0] >> 5;
    r->x0_3 = (g[0] >> 2) & 7;
    r->timer_enabled = BIT(0, 6);
    r->timer_counts_up = BIT(0, 7);
    r->x1_0 = BIT(1, 0); r->x1_1 = BIT(1, 1); r->x1_2 = BIT(1, 2); r->x1_3 = BIT(1, 3);
    r->x1_4 = BIT(1, 4); r->x1_5 = BIT(1, 5); r->timer_shows_hours = BIT(1, 6);
    r->friendly_fire = BIT(1, 7);
    r->is_stock = BIT(2, 0); r->x2_1 = BIT(2, 1); r->x2_2 = BIT(2, 2); r->single_button = BIT(2, 3);
    r->disable_pausing = BIT(2, 4); r->x2_5 = BIT(2, 5); r->x2_6 = BIT(2, 6); r->x2_7 = BIT(2, 7);
    r->x3_0 = BIT(3, 0); r->x3_1 = BIT(3, 1); r->x3_2 = BIT(3, 2); r->x3_3 = BIT(3, 3);
    r->x3_4 = BIT(3, 4); r->x3_5 = BIT(3, 5); r->x3_6 = BIT(3, 6); r->x3_7 = BIT(3, 7);
    r->x4_0 = BIT(4, 0); r->is_vs = BIT(4, 1); r->x4_2 = BIT(4, 2); r->x4_3 = BIT(4, 3);
    r->x4_4 = BIT(4, 4); r->x4_5 = BIT(4, 5); r->x4_6 = BIT(4, 6); r->x4_7 = BIT(4, 7);
    r->x5_0 = BIT(5, 0); r->x5_1 = BIT(5, 1); r->x5_2 = BIT(5, 2); r->x5_3 = BIT(5, 3);
    r->x5_4 = BIT(5, 4); r->x5_5 = BIT(5, 5); r->x5_6 = BIT(5, 6); r->x5_7 = BIT(5, 7);
    r->x6 = g[6];
    r->x7 = g[7];
    r->is_teams = g[8];
    r->x9 = g[9];
    r->xA = g[0xA];
    r->item_freq = (s8) g[0xB];
    r->sd_penalty = (s8) g[0xC];
    r->xD = g[0xD];
    r->stkind = be16(g + 0xE);
    r->time_limit = be32(g + 0x10);
    r->x14 = g[0x14];
    r->x18 = be32(g + 0x18);
    r->x1C_pad[0] = be32(g + 0x1C);
    r->x20 = (u64) be32(g + 0x20) << 32 | be32(g + 0x24);
    r->x28 = (int) be32(g + 0x28);
    r->x2C = bef(g + 0x2C);
    r->x30 = bef(g + 0x30);
    r->game_speed = bef(g + 0x34);
    /* The nine pointers (0x38..0x5B): RestoreGameInfo zeroes 0x40..0x5B and keeps the two pause
     * callbacks as recorded. A recorded console address cannot be called natively; say so. */
    if (be32(g + 0x38) != 0 || be32(g + 0x3C) != 0) {
        logf_("replay: recorded pause callbacks %08X %08X not restored", (int) be32(g + 0x38),
              (int) be32(g + 0x3C));
    }
    r->on_unpause_override = NULL;
    r->on_pause_override = NULL;
    r->check_for_pauser_override = NULL;
    r->on_match_start = NULL;
    r->on_frame_start = NULL;
    r->on_frame_end = NULL;
    r->on_match_end = NULL;
    r->x54 = NULL;
    r->x58 = NULL;
}

static void convert_player(PlayerInitData* p, const u8* g)
{
    p->ckind = (s8) g[0];
#ifdef MU_AKANEIA_FIGHTERS
    if ((s8) g[0] != ChKind_None) {
        const int ckind = mu_ak_ckind_from_mex((s8) g[0]);
        p->ckind = ckind >= 0 ? (s8) ckind : ChKind_None;
    }
#endif
    p->slot_type = g[1];
    p->stocks = (s8) g[2];
    p->color = g[3];
    p->slot = g[4];
    p->spawn_pos = (s8) g[5];
    p->spawn_dir = (s8) g[6];
    p->sub_color = g[7];
    p->handicap = (s8) g[8];
    p->team = g[9];
    p->nametag = g[0xA];
    p->xB = g[0xB];
    p->rumble_enabled = BIT(0xC, 0); p->xC_b1 = BIT(0xC, 1); p->vs_metal = BIT(0xC, 2);
    p->xC_b3 = BIT(0xC, 3); p->vs_invisible = BIT(0xC, 4); p->xC_b5 = BIT(0xC, 5);
    p->xC_b6 = BIT(0xC, 6); p->xC_b7 = BIT(0xC, 7);
    p->xD_b0 = BIT(0xD, 0); p->xD_b1 = BIT(0xD, 1); p->xD_b2 = BIT(0xD, 2); p->xD_b3 = BIT(0xD, 3);
    p->xD_b4 = BIT(0xD, 4); p->xD_b5 = BIT(0xD, 5); p->xD_b6 = BIT(0xD, 6); p->xD_b7 = BIT(0xD, 7);
    p->cpu_kind = g[0xE];
    p->cpu_level = g[0xF];
    p->damage = be16(g + 0x10);
    p->damage1 = be16(g + 0x12);
    p->hp = be16(g + 0x14);
    p->attack_ratio = bef(g + 0x18);
    p->defense_ratio = bef(g + 0x1C);
    p->model_scale = bef(g + 0x20);
    /* Nametags are display only; the recorded name is not in this card's nametag table. */
    if (p->slot_type == 0 && p->nametag != 0x78) {
        p->nametag = 0x78;
    }
}

/* A Game Start game info block (console layout) into the match about to start. Also used by
 * online play, where the host negotiated the block. */
void mu_replay_apply_game_info(StartMeleeData* data, const unsigned char* info)
{
    int i;
    convert_rules(&data->rules, info);
    for (i = 0; i < 6; i++) {
        convert_player(&data->players[i], info + 0x60 + 0x24 * i);
    }
}

static void restore_rng_proc(HSD_GObj* gobj)
{
    unsigned int seed;
    (void) gobj;
    if (mu_replay_on() && mu_replay_abi_frame_seed(&seed)) {
        *HSD_RandSeedPtr = seed;
    }
    if (mu_replay_on() && mu_replay_abi_last_result() == REPLAY_CONTINUE) {
        emit_frame_start();
    }
}

void mu_replay_start_melee(StartMeleeData* data)
{
    unsigned int seed;
    const u8* info = mu_replay_abi_game_info(&seed);
    HSD_GObj* gobj;
    int i;

    recording_active = 0;
    recording_online = 0;
    if (info == NULL) {
        const char* record = getenv("MELEE_SOURCE_RECORD");
        /* An online match is recorded once InitOnlinePlay has set it up (mu_replay_online_start). */
        if (mu_online_pending() || !record || record[0] != '1' || record[1] != '\0' ||
            (gm_GetCurrentGameMode() != GM_VS && gm_GetCurrentGameMode() != GM_DEBUG_VS)) return;
        finished = 0;
        emitted_frame = FIRST_FRAME - 1;
        start_recording(data, NULL);
        return;
    } else {
        *HSD_RandSeedPtr = seed;
        convert_rules(&data->rules, info);
        for (i = 0; i < 6; i++) {
            convert_player(&data->players[i], info + 0x60 + 0x24 * i);
        }
    }
    frame_index = FIRST_FRAME;
    finished = 0;
    emitted_frame = FIRST_FRAME - 1;
    /* RestoreInitialRNG: GObj class 4, p_link 7, priority 0; process s_link 0. */
    gobj = GObj_Create(4, 7, 0);
    HSD_GObj_SetupProc(gobj, restore_rng_proc, 0);
    logf_("replay: match starts, stage %d, seed %08X", data->rules.stkind, (int) *HSD_RandSeedPtr);
}

/* ---- IncrementFrameIndex (8016d294) and FetchGameFrame (8016d298), fn_8016CFE0 ---- */
void mu_replay_scene_think(int match_result)
{
    if (!mu_replay_on() && !recording_active) {
        return;
    }
    if (gm_801A4BA8() == 0) {
        frame_index = FIRST_FRAME;
    } else {
        frame_index++;
    }
    if (match_result == 0 && mu_replay_on()) {
        if (mu_replay_abi_fetch(frame_index) == REPLAY_TERMINATE) {
            logf_("replay: the recording ends at frame %d (fetch %d terminates)", frame_index - 1,
                  frame_index);
        }
    }
}

/* RestoreLRAStart (8016d304): nonzero when the fetch said the game ended here (LRA-Start). */
int mu_replay_terminated(void)
{
    return mu_replay_on() && mu_replay_abi_last_result() == REPLAY_TERMINATE;
}

/* RestoreStockSteal (8016b9c0): replaces the Start test of a dead teams player. */
int mu_replay_stock_steal(int pad_port)
{
    return mu_replay_abi_stock_steal(frame_index, pad_port);
}

/* ---- the recording of the played-back game (Slippi pre-frame and post-frame events) ---- */

static void put32(u8* b, int off, u32 v)
{
    b[off] = (u8) (v >> 24);
    b[off + 1] = (u8) (v >> 16);
    b[off + 2] = (u8) (v >> 8);
    b[off + 3] = (u8) v;
}
static void putf(u8* b, int off, float f) { u32 v; memcpy(&v, &f, 4); put32(b, off, v); }
static void put16(u8* b, int off, u16 v) { b[off] = (u8) (v >> 8); b[off + 1] = (u8) v; }

#include "mu_record_start.inc"

static void emit_frame_bookend(void)
{
    u8 b[8];
    if (emitted_frame < FIRST_FRAME) return;
    put32(b, 0, (u32) emitted_frame);
    put32(b, 4, (u32) emitted_frame); /* playback frames are already final */
    mu_replay_abi_event(0x3C, b, sizeof b);
    emitted_frame = FIRST_FRAME - 1;
}

static void emit_frame_start(void)
{
    u8 b[12];
    if (emitted_frame == frame_index) return;
    emit_frame_bookend();
    put32(b, 0, (u32) frame_index);
    put32(b, 4, *HSD_RandSeedPtr);
    put32(b, 8, mu_gm_engine_state()->unk_8);   /* the unpaused frame counter, 80479D60 */
    mu_replay_abi_event(0x3A, b, sizeof b);
    emitted_frame = frame_index;
}

static int is_follower(const Fighter* fp)
{
    return fp->is_sub_fighter && fp->kind == Ft_Kind_Nana;
}

static void emit_pre_frame(Fighter* fp)
{
    u8 b[0x43];
    frame_has_events = 1;
    int qread = HSD_PadLibData.qread != 0 ? HSD_PadLibData.qread - 1 : 4;
    const PADStatus* raw = &HSD_PadLibData.queue[qread].stat[fp->player_idx & 3];
    const HSD_PadStatus* master = &HSD_PadMasterStatus[fp->player_idx & 3];

    memset(b, 0, sizeof b);
    put32(b, 0x1, (u32) frame_index);
    b[0x5] = fp->player_idx;
    b[0x6] = (u8) is_follower(fp);
    put32(b, 0x7, *HSD_RandSeedPtr);
    put16(b, 0xB, (u16) fp->motion_id);
    putf(b, 0xD, fp->cur_pos.x);
    putf(b, 0x11, fp->cur_pos.y);
    putf(b, 0x15, fp->facing_dir);
    putf(b, 0x19, fp->input.lstick[0].x);
    putf(b, 0x1D, fp->input.lstick[0].y);
    putf(b, 0x21, fp->input.cstick[0].x);
    putf(b, 0x25, fp->input.cstick[0].y);
    putf(b, 0x29, fp->input.triggers[0]);
    put32(b, 0x2D, fp->input.held_buttons[0]);
    put16(b, 0x31, (u16) master->button);
    putf(b, 0x33, master->nml_analogL);
    putf(b, 0x37, master->nml_analogR);
    b[0x3B] = (u8) raw->stickX;
    putf(b, 0x3C, fp->dmg.x1830_percent);
    b[0x40] = (u8) raw->stickY;
    b[0x41] = (u8) raw->substickX;
    b[0x42] = (u8) raw->substickY;
    mu_replay_abi_event(0x37, b + 1, sizeof b - 1);
}

#define FLAG(v, n) ((u8) ((v) ? 0x80 >> (n) : 0))

/* Diagnostic, off unless MELEE_DUMP_FIGHTERS=<first>:<last> (replay frame numbers) and
 * MELEE_DUMP_FIGHTERS_OUT=<file>: the raw native Fighter at the post-frame point, one record per
 * fighter per frame ([s32 frame][u8 port][u8 follower][u16 size][bytes]). The Legacy host writes the
 * console-layout Fighter at the same point; tools/fieldmap/field_diff.py compares them field by field. */
char* getenv(const char* name);
/* The CRT's stdio under other names: the decomp declares its own FILE. */
void* mu_crt_fopen(const char* path, const char* mode) __asm__("fopen");
__SIZE_TYPE__ mu_crt_fwrite(const void* p, __SIZE_TYPE__ size, __SIZE_TYPE__ n, void* f) __asm__("fwrite");
int mu_crt_fflush(void* f) __asm__("fflush");
#define fopen mu_crt_fopen
#define fwrite mu_crt_fwrite
#define fflush mu_crt_fflush
static void dump_fighter(Fighter* fp)
{
    static int init, first, last;
    static void* out;
    if (!init) {
        const char* range = getenv("MELEE_DUMP_FIGHTERS");
        const char* path = getenv("MELEE_DUMP_FIGHTERS_OUT");
        init = 1;
        if (range && path) {
            int sign = 1, v = 0, which = 0;
            first = last = 0;
            for (const char* p = range;; p++) {
                if (*p == '-') {
                    sign = -1;
                } else if (*p >= '0' && *p <= '9') {
                    v = v * 10 + (*p - '0');
                } else {
                    if (which == 0) first = sign * v; else last = sign * v;
                    if (*p != ':') break;
                    which = 1; sign = 1; v = 0;
                }
            }
            out = fopen(path, "wb");
        }
    }
    if (!out || frame_index < first || frame_index > last) {
        return;
    }
    u8 head[8];
    s32 f = frame_index;
    u16 size = (u16) sizeof(Fighter);
    memcpy(head, &f, 4);
    head[4] = fp->player_idx;
    head[5] = (u8) is_follower(fp);
    memcpy(head + 6, &size, 2);
    fwrite(head, 1, 8, out);
    fwrite(fp, 1, sizeof(Fighter), out);
    /* Bone record (port | 0x80): per part rotate, scale, translate, world matrix, 22 floats. */
    if (fp->parts != NULL) {
        u32 n = DP(ftPartsTable[fp->kind])->parts_num;
        float bones[96 * 22];
        if (n > 96) n = 96;
        for (u32 i = 0; i < n; i++) {
            HSD_JObj* j = fp->parts[i].joint;
            float* o = bones + i * 22;
            if (j == NULL) { memset(o, 0, 22 * sizeof(float)); continue; }
            memcpy(o, &j->rotate, 16);
            memcpy(o + 4, &j->scale, 12);
            memcpy(o + 7, &j->translate, 12);
            memcpy(o + 10, j->mtx, 48);
        }
        size = (u16) (n * 22 * sizeof(float));
        head[4] = (u8) (fp->player_idx | 0x80);
        memcpy(head + 6, &size, 2);
        fwrite(head, 1, 8, out);
        fwrite(bones, 1, size, out);
    }
    fflush(out);
}

/* The console's draw pass sets up every skinning bone's world matrix each frame
 * (ftPartsSetupEnvelopeMtx), and gameplay code then reads jobj->mtx of those bones raw
 * (ftdynamics.c, ftCo_AirCatch.c, ftCo_0D95.c ...). The native renderer does not run that
 * setup for bones it does not draw, so refresh the part joints here, at the end of the
 * fighter's draw, where the console's refresh happens (ftDrawCommon_80080E18). */
void mu_refresh_part_matrices(Fighter* fp)
{
    if (fp->parts == NULL || fp->kind >= 33) {
        return;
    }
    u32 n = DP(ftPartsTable[fp->kind])->parts_num;
    for (u32 i = 0; i < n; i++) {
        HSD_JObj* j = fp->parts[i].joint;
        if (j != NULL) {
            HSD_JObjSetupMatrix(j);
        }
    }
}

void mu_replay_post_frame(Fighter* fp)
{
    u8 b[0x55];
    u32 misc;
    u8 flags;

    if (mu_replay_on() && !fp->is_sleeping) {
        dump_fighter(fp);
    }
    if ((!recording_active && (!mu_replay_on() || mu_replay_abi_last_result() != REPLAY_CONTINUE)) || fp->is_sleeping) {
        return;
    }
    frame_has_events = 1;
    memset(b, 0, sizeof b);
    put32(b, 0x1, (u32) frame_index);
    b[0x5] = fp->player_idx;
    b[0x6] = (u8) is_follower(fp);
    b[0x7] = (u8) fp->kind;
#ifdef MU_AKANEIA_FIGHTERS
    if (mu_mex_active()) {
        const int kind = mu_ak_mex_internal(fp->kind);
        if (kind >= 0) b[0x7] = (u8) kind;
    }
#endif
    put16(b, 0x8, (u16) fp->motion_id);
    putf(b, 0xA, fp->cur_pos.x);
    putf(b, 0xE, fp->cur_pos.y);
    putf(b, 0x12, fp->facing_dir);
    putf(b, 0x16, fp->dmg.x1830_percent);
    putf(b, 0x1A, fp->shield_health);
    b[0x1E] = (u8) fp->x208C;
    b[0x1F] = (u8) fp->x2090;
    b[0x20] = (u8) fp->dmg.x18c4_source_ply;
    b[0x21] = (u8) Player_GetStocks(fp->player_idx);
    putf(b, 0x22, fp->cur_anim_frame);
    b[0x26] = FLAG(fp->allow_interrupt, 0) | FLAG(fp->x2218_b1, 1) | FLAG(fp->x2218_b2, 2) |
              FLAG(fp->reflecting, 3) | FLAG(fp->x2218_b4, 4) | FLAG(fp->x2218_b5, 5) |
              FLAG(fp->x2218_b6, 6) | FLAG(fp->x2218_b7, 7);
    b[0x27] = FLAG(fp->x221A_b0, 0) | FLAG(fp->x221A_b1, 1) | FLAG(fp->allow_sdi, 2) |
              FLAG(fp->x221A_b3, 3) | FLAG(fp->fall_fast, 4) | FLAG(fp->x221A_b5, 5) |
              FLAG(fp->x221A_b6, 6) | FLAG(fp->x221A_b7, 7);
    flags = FLAG(fp->x221B_b0, 0) | FLAG(fp->x221B_b1, 1) | FLAG(fp->x221B_b2, 2) |
            FLAG(fp->x221B_b3, 3) | FLAG(fp->x221B_b4, 4) | FLAG(fp->x221B_b5, 5) |
            FLAG(fp->x221B_b6, 6) | FLAG(fp->x221B_b7, 7);
    b[0x28] = flags;
    b[0x29] = FLAG(fp->x221C_b0, 0) | FLAG(fp->x221C_b1, 1) | FLAG(fp->x221C_b2, 2) |
              FLAG(fp->x221C_b3, 3) | FLAG(fp->x221C_b4, 4) | FLAG(fp->x221C_b5, 5) |
              FLAG(fp->x221C_b6, 6) | FLAG((fp->x221C_u16_y >> 2) & 1, 7);
    b[0x2A] = FLAG(fp->x221F_b0, 0) | FLAG(fp->x221F_b1, 1) | FLAG(fp->x221F_b2, 2) |
              FLAG(fp->is_sleeping, 3) | FLAG(fp->is_sub_fighter, 4) | FLAG(fp->x221F_b5, 5) |
              FLAG(fp->x221F_b6, 6) | FLAG(fp->x221F_b7, 7);
    memcpy(&misc, &fp->mv, 4);
    put32(b, 0x2B, misc);
    b[0x2F] = (u8) fp->ground_or_air;
    put16(b, 0x30, (u16) fp->coll_data.floor.index);
    b[0x32] = (u8) (fp->co_attrs.max_jumps - fp->x1968_jumpsUsed);
    b[0x33] = lcancel_status[fp->player_idx & 3][is_follower(fp)];
    b[0x34] = (u8) (fp->x1988 != 0 ? fp->x1988 : fp->x198C);
    putf(b, 0x35, fp->self_vel.x);
    putf(b, 0x39, fp->self_vel.y);
    putf(b, 0x3D, fp->x8c_kb_vel.x);
    putf(b, 0x41, fp->x8c_kb_vel.y);
    putf(b, 0x45, fp->gr_vel);
    putf(b, 0x49, fp->dmg.x195c_hitlag_frames);
    put32(b, 0x4D, (u32) fp->anim_id);
    put16(b, 0x51, fp->dmg.x18ec_instancehitby);
    put16(b, 0x53, fp->x2074.x2088);
    mu_replay_abi_event(0x38, b + 1, sizeof b - 1);
}

/* ---- RestoreGameFrame (8006b0dc, Fighter_procInput before Fighter_procInput_Inner1) ---- */
void mu_replay_input(Fighter* fp)
{
    float f[9];
    unsigned int buttons, action, seed;
    unsigned char raw[4];
    int follower, resync, qread;
    PADStatus* pad;

    if (!mu_replay_on()) {
        if (recording_active && !fp->is_sleeping) emit_pre_frame(fp);
        return;
    }
    if (mu_replay_abi_last_result() != REPLAY_CONTINUE) {
        return;
    }
    follower = is_follower(fp);
    resync = mu_replay_abi_resync();
    if (follower && !resync) {
        emit_pre_frame(fp);
        return;
    }
    mu_replay_abi_character(fp->player_idx, follower, f, &buttons, &action, &seed, raw);
    fp->input.lstick[0].x = f[0];
    fp->input.lstick[0].y = f[1];
    fp->input.cstick[0].x = f[2];
    fp->input.cstick[0].y = f[3];
    fp->input.triggers[0] = f[4];
    fp->input.held_buttons[0] = buttons;
    if (resync) {
        *HSD_RandSeedPtr = seed;
        fp->cur_pos.x = f[5];
        fp->cur_pos.y = f[6];
        fp->facing_dir = f[7];
        fp->motion_id = (FtMotionId) action;
    }
    /* UCF reads the raw stick of the pad queue entry this frame's input came from. */
    qread = HSD_PadLibData.qread - 1;
    if (qread < 0) {
        qread += 5;
    }
    pad = &HSD_PadLibData.queue[qread].stat[fp->pad_port];
    pad->stickX = (s8) raw[0];
    pad->stickY = (s8) raw[1];
    pad->substickX = (s8) raw[2];
    pad->substickY = (s8) raw[3];
    if (resync) {
        u32 bits;
        memcpy(&bits, &f[8], 4);
        if (bits != 0xFFFFFFFFu && f[8] - fp->dmg.x1830_percent != 0.0f) {
            Fighter_TakeDamage_8006CC7C(fp, f[8] - fp->dmg.x1830_percent);
        }
    }
    /* Spawn correction on the first frame. */
    if (frame_index == FIRST_FRAME) {
        CmSubject* box;
        ftPartSetRotX(fp, 0, 0.0f);
        fp->coll_data.cur_pos = fp->cur_pos;
        fp->coll_data.last_pos = fp->cur_pos;
        fp->mv.co.entry.x4 = fp->cur_pos.y;
        fp->coll_data.x38 = mpColl_804D64AC;
        Player_80032828(fp->player_idx, fp->is_sub_fighter, &fp->cur_pos);
        ftCamera_UpdateCameraBox(fp->gobj);
        box = fp->x890_cameraBox;
        box->ext.h.x = box->target_ext.h.x;
        box->ext.h.y = box->target_ext.h.y;
        Camera_8002F3AC();
    }
    emit_pre_frame(fp);
}

/* ---- CleanDynamicGeckos (8016e9e4, gm_Scene_Vs_OnExit) ---- */
static void emit_game_end(int end_method);

void mu_replay_match_exit(void)
{
    if (recording_active) {
        /* The scene ends without the game end having been sent (a reset, a disconnect): end the
         * recording here with what the scene knows. */
        if (!finished) {
            emit_game_end(gmVs_GetSceneState()->match_result);
        }
        recording_active = 0;
        recording_online = 0;
        mu_replay_abi_finished();
        return;
    }
    if (mu_replay_on() && !finished) {
        finished = 1;
        emit_frame_bookend();
        logf_("replay: match scene ends at frame %d (%d)", frame_index, 0);
        mu_replay_abi_finished();
    }
}

/* ---- the replay's online gameplay codes that need game state (the rest sit at their sites) ---- */

/* "BrawlOffscreenDamage" (8006a880): nonzero when the fighter is outside the stage's camera limits,
 * replacing the magnifier-bubble test that decides offscreen damage. */
int mu_offscreen_damage_zone(Fighter* fp)
{
    float x = fp->cur_pos.x, y = fp->cur_pos.y;

    if (gm_GetCurrentGameMode() == GM_HOME_RUN_CONTEST) {
        return 0;   /* the Sandbag takes no offscreen damage */
    }
    if (fp->x221F_b1) {
        return 0;   /* dead */
    }
    if (fp->motion_id == 4 || fp->motion_id == 6) {
        return 0;   /* star KO, screen KO */
    }
    if (x < Stage_GetCamBoundsLeftOffset()) {
        return 1;
    }
    if (x > Stage_GetCamBoundsRightOffset()) {
        return 1;
    }
    if (y > Stage_GetCamBoundsTopOffset()) {
        return 1;
    }
    if (y < Stage_GetCamBoundsBottomOffset()) {
        return 1;
    }
    return 0;
}

/* ---- the recording (Slippi's Recording codes) ---- */

void mu_online_record_state(int* stable_finalized, int* game_over, int* disconnected);

/* SendGameInfo's frame start process (GObj 4/7, priority 0, created after InitOnlinePlay's SyncRNG
 * process so the seed it records is this frame's synced one). */
static void record_frame_start_proc(HSD_GObj* gobj)
{
    u8 b[12];
    (void) gobj;
    if (!recording_active) return;
    put32(b, 0, (u32) frame_index);
    put32(b, 4, *HSD_RandSeedPtr);
    put32(b, 8, mu_gm_engine_state()->unk_8);
    mu_replay_abi_event(0x3A, b, sizeof b);
    frame_has_events = 1;
}

static u32 item_word(const Item* ip, unsigned int offset)
{
    u32 v;
    memcpy(&v, (const u8*) &ip->xDD4_itemVar + offset, 4);
    return v;
}

/* SendGameInfo's item process (GObj 4/7, priority 15): the first 15 items of the item list. */
static void record_items_proc(HSD_GObj* gobj)
{
    HSD_GObj* it;
    int count = 0;
    (void) gobj;
    if (!recording_active) return;
    for (it = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_ITEM]; it != NULL; it = it->next) {
        const Item* ip;
        u8 b[0x2D];
        s8 owner = -1;
        if (++count > 15) break;
        ip = it->user_data;
        memset(b, 0, sizeof b);
        put32(b, 0x1, (u32) frame_index);
        put16(b, 0x5, (u16) ip->kind);
        b[0x7] = (u8) ip->msid;
        putf(b, 0x8, ip->facing_dir);
        putf(b, 0xC, ip->x40_vel.x);
        putf(b, 0x10, ip->x40_vel.y);
        putf(b, 0x14, ip->pos.x);
        putf(b, 0x18, ip->pos.y);
        put16(b, 0x1C, (u16) ip->xC9C);
        putf(b, 0x1E, ip->xD44_lifeTimer);
        put32(b, 0x22, (u32) ip->x1C);
        /* The low byte of the item variables' words 0, 1, 5 and 6 (fp+0xDD7, DDB, DEB, DEF). */
        b[0x26] = (u8) item_word(ip, 0x0);
        b[0x27] = (u8) item_word(ip, 0x4);
        b[0x28] = (u8) item_word(ip, 0x14);
        b[0x29] = (u8) item_word(ip, 0x18);
        /* Owner: byte 0xC of the owner's user data, the port of a fighter. */
        if (ip->owner != NULL && ip->owner->user_data != NULL) {
            if (ip->owner->classifier == HSD_GOBJ_CLASS_FIGHTER) {
                owner = (s8) ((Fighter*) ip->owner->user_data)->player_idx;
            } else if (ip->owner->classifier == HSD_GOBJ_CLASS_ITEM) {
                owner = (s8) ((u32) ((Item*) ip->owner->user_data)->spawn_kind >> 24);
            } else {
                owner = 0;
            }
        }
        b[0x2A] = (u8) owner;
        put16(b, 0x2B, ip->xDA8_short);
        mu_replay_abi_event(0x3B, b + 1, sizeof b - 1);
        frame_has_events = 1;
    }
}

/* FlushFrameBuffer (803219EC, the crowd sound process, the frame's last): the frame bookend, when
 * the frame recorded anything. Online, the latest finalized frame is the stable finalized online
 * frame in replay numbering, never past this frame; offline and once the game is over it is this
 * frame. Native online matches never pause, so Slippi's pause counter is always zero here. */
void mu_replay_flush_frame(void)
{
    u8 b[8];
    int latest = frame_index;
    if (!recording_active || !frame_has_events) return;
    if (recording_online) {
        int stable, game_over, disconnected;
        mu_online_record_state(&stable, &game_over, &disconnected);
        if (!game_over && !disconnected && stable - 123 < frame_index) {
            latest = stable - 123;
        }
    }
    put32(b, 0, (u32) frame_index);
    put32(b, 4, (u32) latest);
    mu_replay_abi_event(0x3C, b, sizeof b);
    frame_has_events = 0;
}

/* SendGameEnd (8016D884): end method, LRA-Start initiator, placements. */
static void emit_game_end(int end_method)
{
    static MatchEnd standings;
    VsSceneState* st = gmVs_GetSceneState();
    u8 b[6];
    int i;
    b[0] = (u8) end_method;
    b[1] = (u8) (end_method == 7 ? st->pauser : -1);
    standings = *gm_8016B774();
    standings.is_teams = gm_GetStartMeleeRules()->is_teams;
    standings.outcome = (u8) end_method;
    gm_80166378(&standings);
    for (i = 0; i < 4; i++) {
        b[2 + i] = standings.player_standings[i].pkind == 3 ? 0xFF
                                                             : standings.player_standings[i].is_small_loser;
    }
    mu_replay_abi_event(0x39, b, sizeof b);
    finished = 1;
}

/* The scene's frame end callback (gm_Scene_Vs_OnFrame's end, where SendGameEnd sits). Online, a
 * finished game is only reported once it can no longer be rolled back. */
static void record_frame_end(void)
{
    int end_method = gmVs_GetSceneState()->match_result;
    if (recording_active && !finished && end_method != 0) {
        int stable, game_over = 1, disconnected;
        if (recording_online) {
            mu_online_record_state(&stable, &game_over, &disconnected);
        }
        if (!recording_online || end_method != 2 || game_over) {
            emit_game_end(end_method);
            mu_replay_abi_finished();
        }
    }
    if (chained_frame_end != NULL) {
        chained_frame_end();
    }
}

static void start_recording(StartMeleeData* data, const u8* msrb)
{
    HSD_GObj* gobj;
    frame_index = FIRST_FRAME - 1;   /* SendGameInfo: -124 until the first scene frame */
    recording_active = 1;
    recording_online = msrb != NULL;
    frame_has_events = 0;
    memset(lcancel_status, 0, sizeof lcancel_status);
    emit_game_start(data, msrb);
    gobj = GObj_Create(4, 7, 0);
    HSD_GObj_SetupProc(gobj, record_frame_start_proc, 0);
    gobj = GObj_Create(4, 7, 0);
    HSD_GObj_SetupProc(gobj, record_items_proc, 15);
    if (data->rules.on_frame_end != record_frame_end) {
        chained_frame_end = data->rules.on_frame_end;
        data->rules.on_frame_end = record_frame_end;
    }
}

/* An online match, right after InitOnlinePlay (mu_online_start_melee): Slippi records every one. */
void mu_replay_online_start(StartMeleeData* data, const unsigned char* msrb)
{
    finished = 0;
    emitted_frame = FIRST_FRAME - 1;
    start_recording(data, msrb);
    logf_("replay: recording the online match, stage %d, seed %08X", data->rules.stkind,
          (int) *HSD_RandSeedPtr);
}

/* ResetLCancelStatus (8006C324, Fighter_procMap) and GetLCancelStatus (8008D698, an aerial's
 * landing): 1 when the landing was L-cancelled, 2 when it was not. */
void mu_replay_lcancel_reset(Fighter* fp)
{
    lcancel_status[fp->player_idx & 3][is_follower(fp)] = 0;
}

void mu_replay_lcancel_landing(Fighter* fp)
{
    lcancel_status[fp->player_idx & 3][is_follower(fp)] = fp->x67F < p_ftCommonData->xE4 ? 1 : 2;
}

/* ---- stage events (Recording/Stages) ----
 * Slippi keeps each code's last sent value inside the code itself, in memory its savestates cover,
 * so a rollback restores it; these statics are in the snapshot the same way. Their first values are
 * the codes' own (0 and 5), and they are reset only where the codes reset them. */
static u32 whispy_last = 0;
static u32 stadium_last = 5;

/* SendDreamlandInfo (80211BF8, grOldPupupu_802113E0's exit): Whispy's blow direction, on change. */
void mu_replay_record_whispy(int direction)
{
    u8 b[6];
    if (!recording_active) return;
    if (frame_index == FIRST_FRAME) whispy_last = 0;
    if (whispy_last == (u32) direction) return;
    whispy_last = (u32) direction;
    put32(b, 1, (u32) frame_index);
    b[5] = (u8) direction;
    mu_replay_abi_event(0x40, b + 1, sizeof b - 1);
    frame_has_events = 1;
}

/* SendFountainInfo (801CC998, the platform height store in grIzumi_801CC358): which side (the
 * sign of the platform's x) and the height, every time the height is set. */
void mu_replay_record_fountain(const HSD_JObj* platform, float height)
{
    u8 b[10];
    u32 x;
    if (!recording_active || frame_index == FIRST_FRAME - 1) return;
    memcpy(&x, &platform->translate.x, 4);
    put32(b, 1, (u32) frame_index);
    b[5] = (u8) (x >> 31);
    putf(b, 6, height);
    mu_replay_abi_event(0x3F, b + 1, sizeof b - 1);
    frame_has_events = 1;
}

/* SendStadiumInfo (801D4FD8, grStadium_801D4548's exit): the transformation state and kind
 * (gp+0xDC and 0xDE as one word), on change. */
void mu_replay_record_stadium(int state, int kind)
{
    u8 b[9];
    const u32 value = (u32) (u16) state << 16 | (u16) kind;
    if (!recording_active) return;
    if (frame_index == -39) stadium_last = 5;
    if (stadium_last == value) return;
    stadium_last = value;
    put32(b, 1, (u32) frame_index);
    put32(b, 5, value);
    mu_replay_abi_event(0x41, b + 1, sizeof b - 1);
    frame_has_events = 1;
}

/* ---- the viewer's controls (host: port/runtime/host/replay_control.h) ----
 * Top of an engine frame (gmscene.c, beside the online and lab hooks), while a replay's match is
 * running. The host is told which replay frame comes next. It holds the call while playback is
 * paused, paces slow motion and fast forward, and keeps a state of the game now and then.
 *
 * When the viewer jumps back the host answers 1, and a second call restores one of those states,
 * here, where a frame starts and no game code is in the middle of anything (as a rollback load
 * does). Nothing of this changes what a frame computes: the frames come from the replay as before. */
#define CMD_REPLAY_GATE 0xFA   /* payload: s32 next replay frame (BE), u8 0 ask / 1 restore now */

int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size);
void mu_alarms_hold(void);
void mu_alarms_release(void);

void mu_replay_frame_begin(void)
{
    static unsigned char response[4096];   /* the host's minimum response capacity */
    unsigned char payload[5];
    unsigned int got = 0;

    if (!mu_replay_on() || finished || mu_replay_abi_last_result() != REPLAY_CONTINUE) {
        return;
    }
    put32(payload, 0, (u32) (frame_index + 1));
    payload[4] = 0;
    if (mu_online_abi_command(CMD_REPLAY_GATE, payload, sizeof payload, response, sizeof response, &got) != 0 ||
        got < 1 || response[0] != 1) {
        return;
    }
    {
        /* On the stack, where the load does not reach. The pad queue belongs to the engine loop
         * this call sits in (it runs one frame per queued sample): it keeps its present counts
         * through the load, or the loop and the queue would disagree about the samples left.
         * Playback takes no input from it. */
        PadLibData pads = HSD_PadLibData;
        mu_alarms_hold();   /* timers are hardware: they keep their schedule through a load */
        payload[4] = 1;
        mu_online_abi_command(CMD_REPLAY_GATE, payload, sizeof payload, response, sizeof response, &got);
        mu_alarms_release();
        HSD_PadLibData = pads;
    }
    /* frame_index is the restored state's now. The last fetched frame is host plumbing, outside the
     * state (mu_replay_abi.c): fetch it again, so what reads it before this frame's own fetch sees
     * what it saw the first time. */
    mu_replay_abi_fetch(frame_index);
}
