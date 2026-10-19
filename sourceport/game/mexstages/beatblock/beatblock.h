#ifndef MU_BEATBLOCK_H
#define MU_BEATBLOCK_H
#include "../common/mu_gr_common.h"
/* A named snapshot of the console streaming fields GrBeat reads. */
typedef struct MuGrBgmClock {
    int fallback, voice_active, voice_paused;
    float voice_rate;
    int voice_loop, voice_current;
    int stream_index, stream_loading_index;
    int chunk_start, chunk_next;
} MuGrBgmClock;
/* provided by common/mu_gr_mex_services.c */
int mu_gr_mex_bgm_clock(MuGrBgmClock* out);
typedef struct BeatPulse { int active; float start, period; } BeatPulse;
extern BeatPulse beat_pulses[4];
extern int beat_fallback;
void beat_sync_create(void);
#endif
