/* Exercise the native alarm service against the SDK's absolute periodic schedule.
 * No disc, wall clock or rendering is involved. */
#include <dolphin/os.h>
#include "mu_shim.h"

extern int printf(const char*, ...);
void mu_fire_alarms(uint64_t now);

const MuHostApi* mu_host;
static MuHostApi host;
static uint64_t now, epoch;
static int calls, failures;

static uint64_t ticks(void) { return now; }
static uint64_t boot_time(void) { return epoch; }
static void panic(const char* file, int32_t line, const char* message)
{
    (void) file; (void) line;
    printf("unexpected panic: %s\n", message);
    failures++;
}
static void callback(OSAlarm* alarm, OSContext* context)
{
    (void) alarm; (void) context;
    calls++;
}
void OSSetArenaLo(void* p) { (void) p; }
void OSSetArenaHi(void* p) { (void) p; }

static void check(int condition, const char* description)
{
    if (!condition) {
        printf("FAIL: %s\n", description);
        failures++;
    }
}

int main(void)
{
    OSAlarm alarm;
    host.ticks = ticks;
    host.boot_time = boot_time;
    host.panic = panic;
    mu_host = &host;
    OSInitAlarm();
    OSCreateAlarm(&alarm);

    now = 105;
    OSSetPeriodicAlarm(&alarm, 10, 10, callback);
    check(alarm.fire == 110, "past start schedules the next phase-aligned deadline");
    mu_fire_alarms(now);
    check(calls == 0, "registering a past start does not invent an immediate input sample");
    now = 110;
    mu_fire_alarms(now);
    check(calls == 1 && alarm.fire == 120, "first deadline fires once and retains phase");
    now = 147;
    mu_fire_alarms(now);
    check(calls == 2 && alarm.fire == 150, "late delivery skips missed periods without changing phase");
    OSCancelAlarm(&alarm);

    now = 200;
    OSSetPeriodicAlarm(&alarm, 250, 10, callback);
    check(alarm.fire == 250, "future start is retained");
    OSCancelAlarm(&alarm);
    OSSetPeriodicAlarm(&alarm, 200, 10, callback);
    check(alarm.fire == 200, "start equal to current time is immediately due");
    OSCancelAlarm(&alarm);

    epoch = 1000;
    OSSetPeriodicAlarm(&alarm, 1210, 10, callback);
    check(alarm.fire == 210, "calendar start is converted to host timebase");
    OSCancelAlarm(&alarm);
    OSSetAlarm(&alarm, 25, callback);
    check(alarm.fire == 225 && alarm.period == 0, "relative one-shot remains relative");
    now = 225;
    calls = 0;
    mu_fire_alarms(now);
    mu_fire_alarms(now);
    check(calls == 1, "one-shot fires once");
    printf("native alarm: %d failures\n", failures);
    return failures ? 1 : 0;
}
