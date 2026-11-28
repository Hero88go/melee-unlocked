"""Exercise the actual shim event queue/accessor with an offline host stub."""
import os
import shutil
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "sourceport/game/shim/mu_os.c").read_text()
events = source[source.index("#define MU_EVENT_MAX"):source.index("void mu_poll(void)")]
stub = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define MU_EXPORT
#define MU_CONSOLE_SERVICE 11
typedef void (*MuEventFn)(void*, intptr_t);
static int mu_interrupt_level;
static void panic_stub(const char* f, int l, const char* m) { (void)f; (void)l; (void)m; assert(0); }
static struct { unsigned version; int (*console_rollback)(int,int,void*); void (*panic)(const char*,int,const char*); }
    host = {27, NULL, panic_stub}, *mu_host = &host;
'''
test = r'''
static int writes;
static void completion(void* a, intptr_t value) {
    (void)a;
    assert(mu_handoff_callbacks_pending_v1() > 0); /* delivering counts after pop, even for last callback */
    writes += (int)value;
}
int main(void) {
    assert(mu_handoff_callbacks_pending_v1() == 0);
    mu_post(completion, NULL, 1); /* DVD */
    mu_post(completion, NULL, 2); /* ARQ */
    mu_post(completion, NULL, 4); /* AI/CARD */
    assert(mu_handoff_callbacks_pending_v1() == 3);
    mu_deliver_pending();
    assert(writes == 0 && mu_handoff_callbacks_pending_v1() == 3);
    mu_interrupt_level = 1;
    mu_deliver_pending();
    assert(writes == 7 && mu_handoff_callbacks_pending_v1() == 0);
    mu_post(completion, NULL, 8);
    mu_deliver_pending();
    assert(writes == 15 && mu_handoff_callbacks_pending_v1() == 0);
    puts("handoff quiescence: queued DVD/ARQ/AI/CARD and active dispatch checks passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="handoff-queue-") as work:
    binary = Path(work) / "test"
    # ctest passes the configured C compiler; a build box may have no plain `cc` on PATH.
    subprocess.run([os.environ.get("CC") or shutil.which("cc") or "gcc", "-std=c11", "-x", "c", "-", "-o", str(binary)],
                   input=stub + events + test, text=True, check=True)
    subprocess.run([str(binary)], check=True)
