/* The operating system the game expects, on a host that is not a GameCube.
 *
 * Most of this layer is smaller than it looks. The game is single-threaded, so "disabling
 * interrupts" is a counter, threads are a formality, and saving a context is nothing. Time comes
 * from the host, which advances the console's timebase deterministically. Alarms are a list the
 * host fires from its own frame loop. What is left is reporting and shutdown. */
#include <dolphin/os.h>

#include "mu_shim.h"

/* ---- interrupts ----
 * The game brackets its own data with these, and while they are off the host must not run any
 * callback the console would have delivered by interrupt. Nothing here is atomic because nothing
 * else runs: the simulation is one thread. */
static int mu_interrupt_level = 1;   /* 1 = enabled, as the game finds the machine */

int mu_interrupts_enabled(void) { return mu_interrupt_level != 0; }

BOOL OSDisableInterrupts(void)
{
    BOOL previous = mu_interrupt_level;
    mu_interrupt_level = 0;
    return previous;
}

BOOL OSRestoreInterrupts(BOOL level)
{
    BOOL previous = mu_interrupt_level;
    mu_interrupt_level = level;
    if (level)
        mu_deliver_pending();
    return previous;
}

BOOL OSEnableInterrupts(void) { return OSRestoreInterrupts(1); }

/* ---- events ----
 * What the console delivered by interrupt (a disc read finishing, a card operation, an audio
 * buffer played) the host reports from inside poll(). Each one is queued here and run once the
 * game has interrupts on, which is when the console would have taken the interrupt. */
#define MU_EVENT_MAX 256
static struct { MuEventFn fn; void* a; intptr_t b; } mu_events[MU_EVENT_MAX];
static int mu_event_head, mu_event_count;
static int mu_delivering;

void mu_post(MuEventFn fn, void* a, intptr_t b)
{
    int slot;
    if (mu_event_count == MU_EVENT_MAX) {
        mu_host->panic(__FILE__, __LINE__, "event queue full");
        return;
    }
    slot = (mu_event_head + mu_event_count) % MU_EVENT_MAX;
    mu_events[slot].fn = fn;
    mu_events[slot].a = a;
    mu_events[slot].b = b;
    mu_event_count++;
}

static void mu_run_events(void)
{
    if (mu_delivering)
        return;
    mu_delivering = 1;
    while (mu_event_count) {
        MuEventFn fn = mu_events[mu_event_head].fn;
        void* a = mu_events[mu_event_head].a;
        intptr_t b = mu_events[mu_event_head].b;
        mu_event_head = (mu_event_head + 1) % MU_EVENT_MAX;
        mu_event_count--;
        /* A handler runs as an interrupt did: with interrupts off. */
        mu_interrupt_level = 0;
        fn(a, b);
        mu_interrupt_level = 1;
    }
    mu_delivering = 0;
}

void mu_deliver_pending(void)
{
    if (mu_interrupt_level)
        mu_run_events();
}

void mu_poll(void)
{
    mu_host->poll();
    mu_deliver_pending();
}

/* Interrupt handlers. The ones that still matter natively are the pixel engine's (draw done and
 * draw sync), raised by the GX library itself once the host has taken the stream (mu_raise_interrupt).
 * The VI retrace goes through mu_vi.c. */
static __OSInterruptHandler mu_handlers[32];

__OSInterruptHandler __OSSetInterruptHandler(__OSInterrupt interrupt, __OSInterruptHandler handler)
{
    __OSInterruptHandler previous = 0;
    if (interrupt >= 0 && interrupt < 32) {
        previous = mu_handlers[interrupt];
        mu_handlers[interrupt] = handler;
    }
    return previous;
}

static void mu_interrupt_event(void* a, intptr_t interrupt)
{
    (void) a;
    if (mu_handlers[interrupt])
        mu_handlers[interrupt]((__OSInterrupt) interrupt, OSGetCurrentContext());
}

void mu_raise_interrupt(int interrupt)
{
    if (interrupt >= 0 && interrupt < 32)
        mu_post(mu_interrupt_event, 0, interrupt);
}

OSInterruptMask __OSUnmaskInterrupts(OSInterruptMask mask) { return mask; }

/* ---- time ----
 * One clock, the console's, owned by the host. Two runs of the same script see the same values. */
OSTime OSGetTime(void) { return (OSTime) (mu_host->boot_time() + mu_host->ticks()); }
OSTick OSGetTick(void) { return (OSTick) mu_host->ticks(); }

static const int mu_days_in_month[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static int mu_leap(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

void OSTicksToCalendarTime(OSTime ticks, OSCalendarTime* td)
{
    long long seconds = (long long) (ticks / (OSTime) MU_TB_HZ);
    long long remainder = (long long) (ticks % (OSTime) MU_TB_HZ);
    long long days;
    int year, month;

    if (remainder < 0) {
        remainder += MU_TB_HZ;
        seconds -= 1;
    }
    td->usec = (int) ((remainder * 1000000) / MU_TB_HZ % 1000);
    td->msec = (int) ((remainder * 1000) / MU_TB_HZ);
    days = seconds / 86400;
    seconds -= days * 86400;
    if (seconds < 0) {
        seconds += 86400;
        days -= 1;
    }
    td->sec = (int) (seconds % 60);
    td->min = (int) ((seconds / 60) % 60);
    td->hour = (int) (seconds / 3600);
    /* The console's epoch is 2000-01-01, which was a Saturday. */
    td->wday = (int) ((days + 6) % 7);
    if (td->wday < 0)
        td->wday += 7;
    year = 2000;
    for (;;) {
        long long length = mu_leap(year) ? 366 : 365;
        if (days < length)
            break;
        days -= length;
        year++;
    }
    td->year = year;
    td->yday = (int) days;
    for (month = 0; month < 12; month++) {
        int length = mu_days_in_month[month] + (month == 1 && mu_leap(year) ? 1 : 0);
        if (days < length)
            break;
        days -= length;
    }
    td->mon = month;
    td->mday = (int) days + 1;
}

/* ---- alarms ----
 * A small sorted list. The host calls mu_fire_alarms from its frame loop with the current
 * timebase, which is where the decrementer exception used to arrive. */
#define MU_MAX_ALARMS 32
static OSAlarm* mu_alarms[MU_MAX_ALARMS];
static int mu_alarm_count;

void OSInitAlarm(void)
{
    mu_alarm_count = 0;
}

void OSCreateAlarm(OSAlarm* alarm)
{
    alarm->handler = 0;
    alarm->tag = 0;
    alarm->fire = 0;
    alarm->prev = 0;
    alarm->next = 0;
    alarm->period = 0;
    alarm->start = 0;
}

static void mu_alarm_remove(OSAlarm* alarm)
{
    int i;
    for (i = 0; i < mu_alarm_count; i++) {
        if (mu_alarms[i] == alarm) {
            mu_alarms[i] = mu_alarms[--mu_alarm_count];
            return;
        }
    }
}

static void mu_alarm_insert(OSAlarm* alarm, OSTime fire, OSTime period, OSAlarmHandler handler)
{
    mu_alarm_remove(alarm);
    if (mu_alarm_count >= MU_MAX_ALARMS) {
        mu_host->panic(__FILE__, __LINE__, "too many alarms");
        return;
    }
    alarm->handler = handler;
    alarm->fire = fire;
    alarm->period = period;
    alarm->start = fire;
    mu_alarms[mu_alarm_count++] = alarm;
}

void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler)
{
    mu_alarm_insert(alarm, (OSTime) mu_host->ticks() + tick, 0, handler);
}

void OSSetAbsAlarm(struct OSAlarm* alarm, long long time, OSAlarmHandler handler)
{
    mu_alarm_insert(alarm, (OSTime) (time - (long long) mu_host->boot_time()), 0, handler);
}

void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period, OSAlarmHandler handler)
{
    mu_alarm_insert(alarm, start, period, handler);
}

void OSCancelAlarm(OSAlarm* alarm)
{
    mu_alarm_remove(alarm);
    alarm->handler = 0;
}

BOOL OSCheckAlarmQueue(void) { return mu_alarm_count != 0; }

void mu_fire_alarms(uint64_t now)
{
    int guard;
    /* Handlers can set and cancel alarms, so the list is re-examined after each one. */
    for (guard = 0; guard < 64; guard++) {
        OSAlarm* due = 0;
        int i;
        for (i = 0; i < mu_alarm_count; i++) {
            OSAlarm* alarm = mu_alarms[i];
            if (alarm->handler && (OSTime) now >= alarm->fire && (!due || alarm->fire < due->fire))
                due = alarm;
        }
        if (!due)
            return;
        if (due->period) {
            due->fire += due->period;
            if ((OSTime) now >= due->fire)
                due->fire = (OSTime) now + due->period;   /* fell behind: do not burst */
        } else {
            mu_alarm_remove(due);
        }
        due->handler(due, 0);
    }
}

/* ---- threads ----
 * The game creates exactly one thread, in the debug console it never opens. Everything else here
 * is the idiom of sleeping on a queue until an interrupt wakes it, which natively means running
 * the host until whatever it was waiting for has happened. */
void OSInitThreadQueue(OSThreadQueue* queue)
{
    queue->head = 0;
    queue->tail = 0;
}

void OSSleepThread(OSThreadQueue* queue)
{
    /* The console switched threads here with interrupts on, so whatever the sleeper waits for
     * arrives even though it went to sleep with them off. */
    (void) queue;
    mu_host->poll();
    mu_run_events();
}

void OSWakeupThread(OSThreadQueue* queue) { (void) queue; }

int OSCreateThread(struct OSThread* thread, void* (*func)(void*), void* param, void* stack,
                   unsigned long stackSize, long priority, unsigned short attr)
{
    (void) thread; (void) func; (void) param; (void) stack; (void) stackSize;
    (void) priority; (void) attr;
    return 0;   /* refused: the only caller is the debug console */
}

s32 OSResumeThread(OSThread* thread) { (void) thread; return 0; }
long OSCheckActiveThreads(void) { return 0; }

static OSThread mu_main_thread;
OSThread* OSGetCurrentThread(void) { return &mu_main_thread; }

/* ---- contexts ----
 * Saving and restoring processor state, which the game does around its own exception handler. The
 * host's stack is not the guest's and nothing here can unwind, so these keep the pointer only. */
static OSContext mu_main_context;   /* the one the game's main runs in; it edits its fpscr */
static OSContext* mu_current_context = &mu_main_context;

u32 OSSaveContext(OSContext* context) { (void) context; return 0; }
void OSClearContext(OSContext* context) { (void) context; }
OSContext* OSGetCurrentContext(void) { return mu_current_context; }
void OSSetCurrentContext(OSContext* context) { mu_current_context = context; }
void OSLoadFPUContext(OSContext* fpuContext) { (void) fpuContext; }
void OSSaveFPUContext(OSContext* fpuContext) { (void) fpuContext; }

/* ---- reporting and stopping ---- */
void OSReport(char* fmt, ...)
{
    char line[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    mu_host->log(line);
}

void OSPanic(char* file, int line, char* msg, ...)
{
    char text[512];
    va_list args;
    va_start(args, msg);
    vsnprintf(text, sizeof text, msg, args);
    va_end(args);
    mu_host->panic(file, line, text);
    for (;;) { }   /* the host does not return from a panic */
}

OSErrorHandler OSSetErrorHandler(OSError error, OSErrorHandler handler)
{
    (void) error;
    (void) handler;
    return 0;   /* the host installs its own crash reporter */
}

void OSResetSystem(int reset, u32 resetCode, BOOL forceMenu)
{
    (void) forceMenu;
    mu_host->stop(MU_STOP_RESET, (int32_t) (reset ? resetCode : 0));
}

unsigned long OSGetResetCode(void) { return (unsigned long) mu_host->reset_code(); }
BOOL OSGetResetSwitchState(void) { return mu_host->reset_switch(); }

/* ---- machine ---- */
/* The console's 24 MB, which is what the game sizes its choices by (a 48 MB development unit takes a
 * different path). The arena below is the host's whole reservation, larger because native pointers
 * are twice the size. */
u32 OSGetPhysicalMemSize(void) { return 24u << 20; }
u32 OSGetConsoleSimulatedMemSize(void) { return 24u << 20; }
u32 OSGetSoundMode(void) { return (u32) mu_host->sound_mode(); }
void OSSetSoundMode(unsigned long mode) { mu_host->set_sound_mode((int32_t) mode); }
unsigned long OSGetProgressiveMode(void) { return (unsigned long) mu_host->progressive_mode(); }
void OSSetProgressiveMode(u32 mode) { mu_host->set_progressive_mode((int32_t) mode); }

/* OSInit brought up the console's low memory, exceptions and heaps. The host has already placed
 * MEM1 and the arena, and the game's own OSAlloc and OSArena run natively, so what is left is the
 * pieces of state the game reads back. */
void OSInit(void)
{
    /* Low memory (the first 64 KB) holds the disc header and what the host puts there for the game. */
    OSSetArenaLo((void*) (uintptr_t) 0x80010000u);
    OSSetArenaHi((void*) (uintptr_t) (0x80000000u + mu_host->mem1_size()));
    OSInitAlarm();
    mu_interrupt_level = 1;
}
