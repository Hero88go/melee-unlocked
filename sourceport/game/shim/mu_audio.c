/* Audio hardware: the audio interface (AI) that plays the mixed buffer, the DSP that mixes it, and
 * audio RAM (ARAM) with its DMA queue (ARQ). The game's AX library runs natively and does what it
 * did on the console; the host plays the buffers and runs the mixer when the command list is mailed,
 * the same way the recompiled build does. */
#include <dolphin/ai.h>
#include <dolphin/ar.h>
#include <dolphin/dsp.h>
#include <dolphin/dvd.h>

#include "mu_shim.h"

unsigned int mu_game_options(void);
int32_t mu_slippi_command(uint8_t command, const uint8_t* payload, uint32_t payload_size,
                          uint8_t* response, uint32_t response_capacity, uint32_t* response_size);

void* mu_native_alloc(u32 size) { return mu_host->native_alloc(size); }
void mu_native_free(void* ptr) { mu_host->native_free(ptr); }

/* ---- AI ---- */
static AIDCallback mu_ai_callback;

void AIInit(u8* stack) { (void) stack; }
AIDCallback AIRegisterDMACallback(AIDCallback callback)
{
    AIDCallback previous = mu_ai_callback;
    mu_ai_callback = callback;
    return previous;
}
void AIInitDMA(u32 start_addr, u32 length) { mu_host->ai_init_dma(mu_mem_ptr(start_addr), length); }
void AIStartDMA(void) { mu_host->ai_start_dma(1); }
void AIStopDMA(void) { mu_host->ai_start_dma(0); }
void AISetDSPSampleRate(u32 rate) { mu_host->ai_set_sample_rate(rate); }
/* Streamed disc audio: Melee has none. */
void AISetStreamVolLeft(u8 volume) { (void) volume; }
void AISetStreamVolRight(u8 volume) { (void) volume; }

static void mu_ai_event(void* a, intptr_t b)
{
    (void) a; (void) b;
    if (mu_ai_callback)
        mu_ai_callback();
}

/* The host played the buffer the game set up: that is the AI interrupt. */
void mu_ai_dma_done(void) { mu_post(mu_ai_event, 0, 0); }

/* ---- DSP ----
 * The mixer finishes each command list before the mail call returns, so the task callbacks that
 * reported the DSP's progress run straight away. */
void DSPInit(void) {}
BOOL DSPCheckInit(void) { return 1; }
void DSPReset(void) {}
void DSPHalt(void) {}
u32 DSPCheckMailToDSP(void) { return mu_host->dsp_mail_pending(); }
u32 DSPGetDMAStatus(void) { return 0; }
void DSPSendMailToDSP(u32 mail) { mu_host->dsp_mail(mail); }

DSPTaskInfo* DSPAddTask(DSPTaskInfo* task)
{
    task->state = 1;   /* DSP_TASK_STATE_RUN */
    if (task->init_cb)
        task->init_cb(task);
    return task;
}

DSPTaskInfo* DSPAssertTask(DSPTaskInfo* task)
{
    if (task->res_cb)
        task->res_cb(task);
    return task;
}

/* ---- ARAM ----
 * The console's allocator: a stack of blocks starting past the 16 KB the OS keeps, with the length
 * of each block pushed onto an array the game supplies. */
static u32* mu_ar_lengths;
static u32 mu_ar_top;

u32 ARInit(u32* stack_index_addr, u32 num_entries)
{
    (void) num_entries;
    mu_ar_lengths = stack_index_addr;
    mu_ar_top = 0x4000;
    return mu_ar_top;
}

u32 ARAlloc(u32 length)
{
    u32 addr = mu_ar_top;
    *mu_ar_lengths++ = length;
    mu_ar_top += length;
    return addr;
}

u32 ARFree(u32* length)
{
    u32 freed = *--mu_ar_lengths;
    if (length)
        *length = freed;
    mu_ar_top -= freed;
    return mu_ar_top;
}

u32 ARGetSize(void) { return mu_host->aram_size(); }

/* ---- ARQ ----
 * Requests copy at once; the callback follows through the event queue as the DMA interrupt did. */
void ARQInit(void) {}

static void mu_arq_event(void* a, intptr_t b)
{
    struct ARQRequest* request = (struct ARQRequest*) a;
    (void) b;
    if (request->callback)
        request->callback(request);
}

void ARQPostRequest(struct ARQRequest* request, uintptr_t owner, u32 type, u32 priority, u32 source, u32 dest, u32 length,
                    ARQCallback callback)
{
    request->next = 0;
    request->owner = owner;
    request->type = type;
    request->priority = priority;
    request->source = source;
    request->dest = dest;
    request->length = length;
    request->callback = callback;
    /* type 0: main memory to ARAM; 1: ARAM to main memory. */
    if (type == 0)
        mu_host->aram_dma(1, mu_mem_ptr(source), dest, length);
    else
        mu_host->aram_dma(0, mu_mem_ptr(dest), source, length);
    mu_post(mu_arq_event, request, 0);
}

/* ---- Music through the host jukebox ----
 * The static recomp plays Melee's music the way Slippi does: its codes start every song silent
 * (MuteMusic), keep the silent stream from reading further (PreventMusicAlarm), and hand the song's
 * disc location, the stops and the music group volume to the host jukebox (StartSong, Stop,
 * VolumeChange: EXI commands D6, D7, D8), which mixes the decoded song at the output device. The
 * native game does the same at the same sites, so its music is the same samples at the same level,
 * and neither a simulation hitch nor the output's clock tracking reaches it. --vanilla-game keeps
 * the console's own stream (the parity runs against the vanilla recomp need it). */
enum { MU_JUKEBOX_PLAY = 0xD6, MU_JUKEBOX_STOP = 0xD7, MU_JUKEBOX_VOLUME = 0xD8 };
static u8 mu_jukebox_reply[MU_SLIPPI_RESPONSE_CAPACITY];
static int mu_jukebox_last_volume = -1;

char* getenv(const char* name);

/* MELEE_NO_JUKEBOX (comparison runs): the console's own music stream through AX, with every other
 * code kept, so a dump holds music and sound effects as Dolphin's DSP dump of a replay does. */
int mu_jukebox_music(void)
{
#ifdef MU_NO_SLIPPI
    return 0;   /* no host music player in this build: the game's own stream through AX, always */
#endif
    static int no_jukebox = -1;
    if (no_jukebox < 0) {
        no_jukebox = getenv("MELEE_NO_JUKEBOX") != 0;
    }
    return !no_jukebox && (mu_game_options() & MU_GAME_OPTION_VANILLA) == 0;
}

static void mu_jukebox_send(u8 command, const u8* payload, u32 size)
{
    uint32_t reply_size = 0;
    mu_slippi_command(command, payload, size, mu_jukebox_reply, sizeof mu_jukebox_reply, &reply_size);
}

void mu_jukebox_play(int entrynum)
{
    DVDFileInfo file;
    u8 payload[8];
    if (!mu_jukebox_music() || entrynum < 0 || !DVDFastOpen(entrynum, &file))
        return;
    payload[0] = (u8) (file.startAddr >> 24); payload[1] = (u8) (file.startAddr >> 16);
    payload[2] = (u8) (file.startAddr >> 8);  payload[3] = (u8) file.startAddr;
    payload[4] = (u8) (file.length >> 24);    payload[5] = (u8) (file.length >> 16);
    payload[6] = (u8) (file.length >> 8);     payload[7] = (u8) file.length;
    mu_jukebox_send(MU_JUKEBOX_PLAY, payload, sizeof payload);
    DVDClose(&file);
}

void mu_jukebox_stop(void)
{
    if (mu_jukebox_music())
        mu_jukebox_send(MU_JUKEBOX_STOP, NULL, 0);
}

/* The music group volume as the game stores it (0-254, without the settings Music level, which the
 * jukebox applies itself); sent when it changes, as VolumeChange does. */
void mu_jukebox_volume(unsigned char volume)
{
    if (!mu_jukebox_music() || volume == mu_jukebox_last_volume)
        return;
    mu_jukebox_last_volume = volume;
    mu_jukebox_send(MU_JUKEBOX_VOLUME, &volume, 1);
}

#ifdef MU_NATIVE
/* Rollback snapshot exclusions: host plumbing, not game state. */
MU_EXCLUSIONS(audio,
              MU_EXCLUDE(mu_ai_callback),
              MU_EXCLUDE(mu_jukebox_reply),
              MU_EXCLUDE(mu_jukebox_last_volume))
#endif
