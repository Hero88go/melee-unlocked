/* GrBeat +7D4/+AE4/+15D4/+1620. The console has a 60 Hz fallback for patched music,
 * and a sample-position path for the original stream. The native jukebox uses the fallback.
 * The snapshot service owns static driver fields; this file owns the block's scheduling. */
#include "beatblock.h"
#include <melee/gm/gmscene.h>
#include <melee/lb/lbaudio_ax.h>
#include <sysdolphin/baselib/axdriver.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/memory.h>

typedef struct BeatSchedule {
    BeatPulse* pulse;
    int start, period, next;
    struct BeatSchedule* link;
} BeatSchedule;
typedef struct BeatSync {
    int frame, initialize;
    BeatSchedule* beats;
    int driver_playing;
    float rate;
    int stream, base, chunk_start, chunk_next, voice_current, voice_loop;
} BeatSync;
BeatPulse beat_pulses[4] = { {0,2.52F,4}, {0,3.02F,4}, {0,3.52F,4}, {0,4.02F,4} };
int beat_fallback;

MU_NO_CONTRACT static void beat_next(BeatSchedule* beat, int offset)
{
    if (beat->start > offset) beat->next = beat->start;
    else {
        float periods = (float) (offset - beat->start) / (float) beat->period;
        int whole = (int) periods;
        float rounded = (float) whole;
        float product;
        if (periods != rounded && periods > 0.0F) rounded = (float) (whole + 1);
        product = (float) beat->period * rounded;
        product += (float) beat->start;
        beat->next = (int) product;
    }
}
static int beat_offset(BeatSync* v, MuGrBgmClock* clock)
{
    int offset = clock->voice_current - v->voice_current;
    offset += v->base - 128;
    /* +1614 frsp followed by caller fctiwz is observable for large offsets. */
    return (int) (float) offset;
}
MU_NO_CONTRACT static void beat_fallback_tick(BeatSync* v)
{
    BeatSchedule* beat;
    if (v->initialize) {
        lbAudioAx_800236DC();
        v->initialize = 0;
        for (beat = v->beats; beat != NULL; beat = beat->link) {
            beat->start = (int) (beat->pulse->start * 60.0F);
            beat->period = (int) (beat->pulse->period * 60.0F);
            beat->next = beat->start != 0 ? beat->start : beat->period;
        }
    }
    for (beat = v->beats; beat != NULL; beat = beat->link) {
        beat->pulse->active = 0;
        if (v->frame >= beat->next) {
            beat->pulse->active = 1;
            beat->next += beat->period;
        }
    }
}
MU_NO_CONTRACT static void beat_sync_think(HSD_GObj* gobj)
{
    BeatSync* v = HSD_GObjGetUserData(gobj);
    BeatSchedule* beat;
    MuGrBgmClock clock;
    int paused = gm_GetDbPauseFlag(1);
    int offset, available = mu_gr_mex_bgm_clock(&clock);
    beat_fallback = clock.fallback;
    if (beat_fallback && !paused) {
        v->frame++;
        beat_fallback_tick(v);
        return;
    }
    /* +C9C deliberately falls into the streaming branch while the fallback is paused. */
    v->frame++;
    if (paused) {
        if (v->driver_playing) { AXDriverPause(); v->driver_playing = 0; }
    } else if (!v->driver_playing) { AXDriverResume(); v->driver_playing = 1; }
    if (!available || clock.voice_paused) return;
    if (v->initialize) {
        if (v->frame <= 0) return;
        v->initialize = 0;
        v->rate = (float) ((double) clock.voice_rate / 0.00003125);
        for (beat = v->beats; beat != NULL; beat = beat->link) {
            float scale = 875.0F / v->rate;
            float value = beat->pulse->start * 1000.0F;
            beat->start = (int) (value / scale);
            value = beat->pulse->period * 1000.0F;
            scale = 875.0F / v->rate;
            beat->period = (int) (value / scale);
        }
        if (clock.stream_index == clock.stream_loading_index) {
            v->base = 128;
            for (beat = v->beats; beat != NULL; beat = beat->link)
                beat->next = beat->start != 0 ? beat->start : beat->period;
            v->voice_current = clock.voice_loop; v->voice_loop = -1;
        } else {
            v->base = -1; v->voice_current = clock.voice_current; v->voice_loop = clock.voice_loop;
        }
        v->stream = clock.stream_index;
        v->chunk_start = clock.chunk_start; v->chunk_next = clock.chunk_next;
    }
    if (v->voice_loop == -1) {
        if (v->voice_current != clock.voice_loop) v->voice_loop = clock.voice_loop;
    }
    if (v->stream != clock.stream_index) {
        int old_base = v->base;
        v->stream = clock.stream_index;
        v->base = v->chunk_start;
        v->chunk_start = clock.chunk_start; v->chunk_next = clock.chunk_next;
        v->voice_current = v->voice_loop; v->voice_loop = clock.voice_loop;
        if (v->base == -1) return;
        if (old_base == -1 || v->base < old_base) {
            offset = beat_offset(v, &clock);
            for (beat = v->beats; beat != NULL; beat = beat->link) beat_next(beat, offset);
        }
    }
    if (v->base == -1) return;
    offset = beat_offset(v, &clock);
    for (beat = v->beats; beat != NULL; beat = beat->link) {
        beat->pulse->active = 0;
        if ((beat->next - 439 <= offset && offset <= beat->next + 439) || beat->next < offset) {
            beat->pulse->active = 1; beat->next += beat->period;
        }
    }
}
void beat_sync_create(void)
{
    HSD_GObj* gobj = GObj_Create(0, 0, 0);
    BeatSync* v = HSD_MemAlloc(sizeof(*v));
    BeatSchedule* head = NULL;
    MuGrBgmClock clock;
    int i;
    for (i = 0; i < 4; i++) {
        BeatSchedule* beat = HSD_MemAlloc(sizeof(*beat));
        beat_pulses[i].active = 0;
        beat->pulse = &beat_pulses[i]; beat->link = head; head = beat;
    }
    v->frame = 0; v->initialize = 1; v->beats = head;
    v->driver_playing = 1; v->rate = -1;
    v->stream = v->base = v->chunk_start = v->chunk_next = v->voice_current = v->voice_loop = -1;
    mu_gr_mex_bgm_clock(&clock); beat_fallback = clock.fallback;
    GObj_InitUserData(gobj, 4, HSD_Free, v);
    HSD_GObj_SetupProc(gobj, beat_sync_think, 1);
}
