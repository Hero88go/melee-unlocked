/* Rollback-safe audio and rumble for native online play.
 *
 * Audio lives outside rollback snapshots, as on the console: a load leaves every voice playing.
 * So a re-executed frame must not start its sounds a second time, and the sounds the replaced
 * timeline started that the new one does not must be stopped. This file keeps, per engine frame
 * (gm_80479D58.unk_8), the sounds the last execution of that frame started ("stable") and the
 * ones the running execution starts ("pending"): a re-executed start adopts the matching stable
 * voice and its instance id, and the frame end stops every stable voice nobody adopted. It is
 * the native form of the online code's PreventDuplicateSounds, AssignSoundInstanceId, the sound
 * half of LoopEngineForRollback, NoDestroyVoice (generalized), StartSong, Stop and HandleRumble.
 *
 * Nothing here runs unless an online match is active, so offline play and replay playback are
 * unchanged. All of this state is excluded from snapshots (mu_online_audio_state_address).
 */
#include <string.h>
#include <melee/gm/types.h>
#include <sysdolphin/baselib/axdriver.h>
#include <sysdolphin/baselib/rumble.h>
#include <mu_native.h>

struct gm_80479D58_t* mu_gm_engine_state(void);
void mu_online_abi_log(const char* text);
int snprintf(char* buffer, __SIZE_TYPE__ size, const char* format, ...);
char* getenv(const char* name);
extern HSD_RumbleData HSD_Rumble_804C22E0[4];

enum {
    SFX_RING = 16,             /* > the deepest rollback (7 frames): a slot never aliases a live frame */
    SFX_PER_FRAME = 32,        /* four fighters and items can start more than 16 sounds in a frame */
    SFX_STOP_TRIGGER = 540000, /* "end what plays on this track": never deduplicated */
    VOICES_EVERY = 60,
};

typedef struct SfxEntry {
    int sound_id;
    int track;
    int instance;   /* -1 while the start has not got a voice */
} SfxEntry;

typedef struct SfxFrame {
    int frame;                          /* the engine frame this slot holds, -1 when empty */
    u8 stable_count, pending_count;
    u32 consumed;                       /* bit i: stable[i] adopted by the running execution */
    SfxEntry stable[SFX_PER_FRAME];     /* previous execution of the frame: the live voices */
    SfxEntry pending[SFX_PER_FRAME];    /* running execution */
} SfxFrame;

typedef struct MuOnlineAudio {
    int latest;                         /* newest frame whose body ended, -1 at match start */
    int in_body;                        /* between mu_online_audio_frame_begin and _end */
    SfxFrame f[SFX_RING];
    unsigned int started, adopted, stopped, bypassed, overflow, beyond_ring, failed, gaps;
    /* A disc stream start or stop requested inside a re-simulation, done at the rollback end. */
    int music_op;
    char music_path[64];
    u8 music_volume;
    int music_track;
    unsigned int music_deferred;
    /* Rumble of the local player's port, sent to the physical port its pad is read from. */
    int local_index, input_source;
    int motor_want, motor_on;
} MuOnlineAudio;

static MuOnlineAudio audio;
static int trace = -1;

static int tracing(void)
{
    if (trace < 0) {
        const char* text = getenv("MELEE_TRACE_ONLINE_SFX");
        trace = text != NULL && text[0] != '\0' && text[0] != '0';
    }
    return trace;
}

static void logf_(const char* fmt, int a, int b, int c, int d, int e, int f)
{
    char line[200];
    snprintf(line, sizeof line, fmt, a, b, c, d, e, f);
    mu_online_abi_log(line);
}

static int engine_frame(void)
{
    return (int) mu_gm_engine_state()->unk_8;
}

/* The patches' short-circuit (online match, scene not being left), plus: inside an engine body.
 * A sound an event handler starts between bodies belongs to no frame a re-simulation repeats. */
static int sfx_hooks_on(void)
{
    return mu_online_active() && mu_gm_engine_state()->unk_C == 0 && audio.in_body;
}

/* What the ring holds for a frame that can no longer be rolled back, sorted, for comparing runs. */
static void trace_final(const SfxFrame* s)
{
    char line[400];
    int order[SFX_PER_FRAME];
    int i, j, n = 0, len;
    if (!tracing() || s->frame < 0) {
        return;
    }
    for (i = 0; i < s->stable_count; i++) {
        order[i] = i;
    }
    for (i = 1; i < s->stable_count; i++) {
        int k = order[i];
        for (j = i; j > 0; j--) {
            const SfxEntry* a = &s->stable[order[j - 1]];
            const SfxEntry* b = &s->stable[k];
            if (a->sound_id < b->sound_id || (a->sound_id == b->sound_id && a->track <= b->track)) {
                break;
            }
            order[j] = order[j - 1];
        }
        order[j] = k;
    }
    len = snprintf(line, sizeof line, "[online-sfx-final] frame=%d n=%d ids=", s->frame, s->stable_count);
    for (i = 0; i < s->stable_count && len < (int) sizeof line - 24; i++, n++) {
        const SfxEntry* e = &s->stable[order[i]];
        len += snprintf(line + len, sizeof line - len, "%s%d/%d", n ? "," : "", e->sound_id, e->track);
    }
    mu_online_abi_log(line);
}

/* The slot of a frame; a slot still tagged with an older frame is taken over. */
static SfxFrame* sfx_slot(int frame)
{
    SfxFrame* s = &audio.f[frame & (SFX_RING - 1)];
    if (s->frame != frame) {
        if (frame <= audio.latest) {
            audio.beyond_ring++;   /* re-executed, but its record was already reused */
        }
        trace_final(s);
        s->frame = frame;
        s->stable_count = 0;
        s->pending_count = 0;
        s->consumed = 0;
    }
    return s;
}

static int sfx_push(SfxFrame* s, int sound_id, int track, int instance)
{
    SfxEntry* e = &s->pending[s->pending_count];
    e->sound_id = sound_id;
    e->track = track;
    e->instance = instance;
    return s->pending_count++;
}

static void trace_start(int frame, int sound_id, int track, const char* result, int instance)
{
    if (tracing()) {
        char line[200];
        snprintf(line, sizeof line, "[online-sfx] frame=%d latest=%d resim=%d id=%d track=%d result=%s inst=%d",
                 frame, audio.latest, mu_online_resim_active(), sound_id, track, result, instance);
        mu_online_abi_log(line);
    }
}

void mu_online_audio_match_start(int local_index, int input_source)
{
    int i;
    memset(&audio, 0, sizeof audio);
    audio.latest = -1;
    for (i = 0; i < SFX_RING; i++) {
        audio.f[i].frame = -1;
    }
    audio.local_index = local_index & 3;
    audio.input_source = input_source & 3;
}

void mu_online_audio_frame_begin(void)
{
    if (mu_online_active() && mu_gm_engine_state()->unk_C == 0) {
        audio.in_body = 1;
    }
}

/* PreventDuplicateSounds (8038D0B0): a re-executed frame gets the voice its previous execution
 * started, still playing because audio is outside the snapshot. Matched on (sound, track), each
 * stable voice adopted once. Otherwise the start is recorded and gets a ticket for its id. */
int mu_online_sfx_begin(int sound_id, int track, int* adopted_id)
{
    SfxFrame* s;
    int frame, i;
    if (!sfx_hooks_on()) {
        return MU_SFX_NO_TICKET;
    }
    frame = engine_frame();
    if (sound_id == SFX_STOP_TRIGGER) {
        audio.bypassed++;
        trace_start(frame, sound_id, track, "bypass", -1);
        return MU_SFX_NO_TICKET;
    }
    s = sfx_slot(frame);
    if (frame <= audio.latest) {
        for (i = 0; i < s->stable_count; i++) {
            SfxEntry* e = &s->stable[i];
            if (!(s->consumed & (1u << i)) && e->sound_id == sound_id && e->track == track) {
                s->consumed |= 1u << i;
                if (s->pending_count < SFX_PER_FRAME) {
                    sfx_push(s, e->sound_id, e->track, e->instance);
                } else {
                    audio.overflow++;   /* adopted now, but a later rollback will not see it */
                }
                audio.adopted++;
                trace_start(frame, sound_id, track, "adopted", e->instance);
                *adopted_id = e->instance;
                return MU_SFX_ADOPTED;
            }
        }
    }
    if (s->pending_count == SFX_PER_FRAME) {
        audio.overflow++;
        trace_start(frame, sound_id, track, "overflow", -1);
        return MU_SFX_NO_TICKET;
    }
    return (frame << 8) | sfx_push(s, sound_id, track, -1);
}

/* AssignSoundInstanceId (8038D224): the id the start got, for a later execution to adopt. */
void mu_online_sfx_started(int ticket, int instance)
{
    SfxFrame* s;
    SfxEntry* e;
    if (ticket < 0) {
        return;
    }
    s = &audio.f[(ticket >> 8) & (SFX_RING - 1)];
    if (s->frame != (ticket >> 8) || (ticket & 0xFF) >= s->pending_count) {
        return;
    }
    e = &s->pending[ticket & 0xFF];
    e->instance = instance;
    audio.started++;
    trace_start(s->frame, e->sound_id, e->track, "new", instance);
}

/* NoDestroyVoice, generalized: during a re-executed frame F, a voice the replaced timeline started
 * on F or later is not part of the new timeline yet. A key-off by track or of every voice leaves
 * it to the frame reconciliation (adopted on its own frame, or stopped when that frame ends). */
int mu_online_sfx_protected(int instance)
{
    int frame, g, i;
    if (!sfx_hooks_on()) {
        return 0;
    }
    frame = engine_frame();
    for (g = frame; g <= audio.latest; g++) {
        const SfxFrame* s = &audio.f[g & (SFX_RING - 1)];
        if (s->frame != g) {
            continue;
        }
        for (i = 0; i < s->stable_count; i++) {
            if (s->stable[i].instance == instance && !(s->consumed & (1u << i))) {
                return 1;
            }
        }
    }
    return 0;
}

/* LoopEngineForRollback, sound half (801A5014): when a re-executed frame ends, stop what the
 * replaced timeline started on it and this execution did not; then this execution becomes the
 * record a later rollback over the frame compares with. */
void mu_online_audio_frame_end(void)
{
    SfxFrame* s;
    int current, i, n = 0;
    if (!mu_online_active() || mu_gm_engine_state()->unk_C != 0) {
        return;
    }
    audio.in_body = 0;
    current = engine_frame() - 1;
    if (current > audio.latest + 1 && audio.latest >= 0) {
        audio.gaps++;   /* one unk_8 step per body is assumed; a skipped number shifts the ring */
    }
    s = sfx_slot(current);
    if (current <= audio.latest) {
        for (i = 0; i < s->stable_count; i++) {
            if (!(s->consumed & (1u << i))) {
                const SfxEntry* e = &s->stable[i];
                HSD_AudioSFXKeyOff(e->instance);   /* an id whose voice ended is ignored */
                audio.stopped++;
                if (tracing()) {
                    logf_("[online-sfx-stop] frame=%d id=%d track=%d inst=%d", current, e->sound_id,
                          e->track, e->instance, 0, 0);
                }
            }
        }
    }
    for (i = 0; i < s->pending_count; i++) {
        if (s->pending[i].instance >= 0) {
            s->stable[n++] = s->pending[i];
        } else {
            /* No voice (bad track or channel, none free): a later execution retries it. */
            audio.failed++;
            trace_start(current, s->pending[i].sound_id, s->pending[i].track, "failed", -1);
        }
    }
    s->stable_count = (u8) n;
    s->pending_count = 0;
    s->consumed = 0;
    if (current > audio.latest) {
        audio.latest = current;
        if (tracing() && current % VOICES_EVERY == 0) {
            logf_("[online-sfx-voices] frame=%d active=%d", current, AXDriver_8038E5DC(), 0, 0, 0, 0);
        }
    }
}

/* StartSong (8038E910) and Stop (800236EC): no disc stream starts or stops inside a
 * re-simulation (disc reads and the stream start's busy-wait need a running machine). The last
 * request wins (a switch is a stop then a start) and is done when the rollback ends. */
int mu_online_music_defer(int op, const char* path, int volume, int track)
{
    if (!mu_online_active() || !mu_online_resim_active() || mu_gm_engine_state()->unk_C != 0) {
        return 0;
    }
    audio.music_op = op;
    audio.music_path[0] = '\0';
    if (op == MU_MUSIC_START && path != NULL) {
        strncpy(audio.music_path, path, sizeof audio.music_path - 1);
        audio.music_path[sizeof audio.music_path - 1] = '\0';
    }
    audio.music_volume = (u8) volume;
    audio.music_track = track;
    audio.music_deferred++;
    return 1;
}

static void music_flush(void)
{
    int op = audio.music_op;
    audio.music_op = 0;
    if (op == MU_MUSIC_START) {
        HSD_AudioPStreamStartChParam(audio.music_path, audio.music_volume, audio.music_track);
    } else if (op == MU_MUSIC_STOP) {
        AXDriverStop();
    }
}

/* HandleRumble (8034DED8): only the local player's in-game port rumbles, on the physical port its
 * pad is read from. Commands of a re-simulation are dropped; the rollback end reconciles. */
int mu_online_motor_port(int chan, unsigned int command)
{
    if (!mu_online_active()) {
        return chan;
    }
    if (chan != audio.local_index) {
        return -1;
    }
    audio.motor_want = command == 1;
    if (tracing()) {
        logf_("[online-motor] frame=%d want=%d on=%d resim=%d", engine_frame(), audio.motor_want,
              audio.motor_on, mu_online_resim_active(), 0, 0);
    }
    if (mu_online_resim_active()) {
        return -1;
    }
    audio.motor_on = audio.motor_want;
    return audio.input_source;
}

/* The motor follows the rolled-back and re-simulated game state, even when the new timeline
 * issued no rumble edge of its own. */
static void motor_reconcile(void)
{
    int on = HSD_Rumble_804C22E0[audio.local_index].last_status == 2;
    if (on != audio.motor_on) {
        audio.motor_on = on;
        mu_pad_motor(audio.input_source, on);
    }
}

void mu_online_audio_rollback_end(void)
{
    if (!mu_online_active()) {
        return;
    }
    if (audio.music_op != 0) {
        music_flush();
    }
    motor_reconcile();
}

void mu_online_audio_match_exit(void)
{
    int i;
    if (!mu_online_active()) {
        return;
    }
    audio.in_body = 0;
    /* The exit path stops the music itself; a request deferred by an unfinished rollback is moot. */
    audio.music_op = 0;
    if (audio.motor_on) {
        audio.motor_on = 0;
        mu_pad_motor(audio.input_source, 0);
    }
    if (tracing()) {
        for (i = 0; i < SFX_RING; i++) {
            trace_final(&audio.f[i]);
        }
    }
    logf_("online-sfx: %d started, %d adopted, %d stopped, %d bypassed, %d overflow, %d failed",
          (int) audio.started, (int) audio.adopted, (int) audio.stopped, (int) audio.bypassed,
          (int) audio.overflow, (int) audio.failed);
    logf_("online-sfx: %d beyond ring, %d frame gaps, %d deferred music requests", (int) audio.beyond_ring,
          (int) audio.gaps, (int) audio.music_deferred, 0, 0, 0);
}

/* The whole block, for the snapshot exclusion list. */
void* mu_online_audio_state_address(void)
{
    return &audio;
}

unsigned int mu_online_audio_state_size(void)
{
    return (unsigned int) sizeof audio;
}
