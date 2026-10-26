/* Replay takeover, the other players (port/runtime/gx/replay_hud.h): while one port is played live,
 * the others repeat the recording until the live game no longer matches it (their position or
 * motion differs from the recorded one two frames in a row; in Melee, DI alone changes what happens
 * after the first hit). From then on the practice CPU's model plays each of them against the live
 * player with the host's Defense setting (DI, techs, getting out), so a punish can be practised
 * until it drops. "Follow the recording" keeps the recorded inputs throughout.
 *
 * The flags are game state: a retry or a jump back restores them with everything else. */
#include <dolphin/os.h>
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>

#include "mu_native.h"
#include "mu_practice_cpu.h"

int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size);
unsigned int mu_replay_abi_live_ports(void);
int mu_replay_frame_index(void);
int mu_practice_cpu_drive_as(void* fighter, void* opponent, const MuPcConfig* c, MuLabInput* out);

#define CMD_REPLAY_DEFENSE 0xE9   /* source_host.cpp: reply MuPcConfig, or nothing to follow the recording */

static int def_off[4];   /* frames in a row this fighter did not match the recording */
static int def_on[4];    /* it left the recording and defends on its own */
static int def_logs;
/* The host's Defense setting, asked once a frame (host-coupled, outside the game state). */
static MuPcConfig def_config;
static int def_config_frame = -1000000, def_have;

static float absf(float v) { return v < 0.0f ? -v : v; }

static const MuPcConfig* defense_config(void)
{
    const int now = mu_replay_frame_index();
    if (now != def_config_frame) {
        static unsigned char response[4096];   /* the host's minimum reply capacity */
        unsigned int got = 0;
        def_config_frame = now;
        def_have = mu_online_abi_command(CMD_REPLAY_DEFENSE, NULL, 0, response, sizeof response, &got) == 0 &&
                   got == sizeof def_config && response[0] == MU_PC_CONFIG_VERSION;
        if (def_have) __builtin_memcpy(&def_config, response, sizeof def_config);
    }
    return def_have ? &def_config : NULL;
}

/* From mu_replay_input with this frame's recorded values (f[5], f[6] position, `action` motion).
 * Nonzero when it set the fighter's controller for the frame instead of the recording. */
int mu_replay_defend(Fighter* fp, const float* f, unsigned int action)
{
    const unsigned int live = mu_replay_abi_live_ports();
    const int port = fp->player_idx & 3;
    const MuPcConfig* c;
    HSD_GObj* opp_gobj = NULL;
    MuLabInput in;
    int i;
    if (live == 0 || ((live >> port) & 1)) {
        def_off[port] = def_on[port] = 0;
        return 0;
    }
    if (!def_on[port]) {
        const int off = absf(fp->cur_pos.x - f[5]) > 0.5f || absf(fp->cur_pos.y - f[6]) > 0.5f ||
                        (unsigned int) fp->motion_id != action;
        def_off[port] = off ? def_off[port] + 1 : 0;
        if (def_off[port] < 2) return 0;
        def_on[port] = 1;
        if (def_logs < 64) {
            def_logs++;
            OSReport("[replay] P%d left the recording at frame %d\n", port + 1, mu_replay_frame_index());
        }
    }
    c = defense_config();
    if (c == NULL) return 0;   /* Follow the recording */
    for (i = 0; i < 4 && opp_gobj == NULL; i++) {
        if ((live >> i) & 1) opp_gobj = Player_GetEntity(i);
    }
    __builtin_memset(&in, 0, sizeof in);
    if (opp_gobj != NULL && opp_gobj->user_data != NULL) {
        mu_practice_cpu_drive_as(fp, opp_gobj->user_data, c, &in);   /* not driving: hands off the controller */
    }
    fp->input.lstick[0].x = in.lx;
    fp->input.lstick[0].y = in.ly;
    fp->input.cstick[0].x = in.cx;
    fp->input.cstick[0].y = in.cy;
    fp->input.triggers[0] = in.l > in.r ? in.l : in.r;
    fp->input.held_buttons[0] = in.buttons;
    return 1;
}

MU_EXCLUSIONS(replay_defend, MU_EXCLUDE(def_logs), MU_EXCLUDE(def_config), MU_EXCLUDE(def_config_frame),
              MU_EXCLUDE(def_have))
