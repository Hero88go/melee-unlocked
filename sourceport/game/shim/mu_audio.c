/* Audio hardware: the audio interface (AI) that plays the mixed buffer, the DSP that mixes it, and
 * audio RAM (ARAM) with its DMA queue (ARQ). The game's AX library runs natively and does what it
 * did on the console; the host plays the buffers and runs the mixer when the command list is mailed,
 * the same way the recompiled build does. */
#include <dolphin/ai.h>
#include <dolphin/ar.h>
#include <dolphin/dsp.h>

#include "mu_shim.h"

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

void ARQPostRequest(struct ARQRequest* request, u32 owner, u32 type, u32 priority, u32 source, u32 dest, u32 length,
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
