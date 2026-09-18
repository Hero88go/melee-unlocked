/* The GX command FIFO, natively: bookkeeping only.
 *
 * On the console GXFifo.c owned a ring buffer in memory, the CPU wrote into it through the
 * write-gather pipe and the graphics processor read from it, with interrupts for high and low
 * watermarks and breakpoints. None of that exists here. GXW_* (mu_gxpipe.h) append command bytes
 * to a staging buffer that mu_wg_flush hands to the host's FIFO decoder, so every function below
 * keeps the object fields the rest of the library reads and does nothing with the hardware. */
#include <dolphin/gx.h>
#include <dolphin/os.h>

#include "gx/__gx.h"
#include "mu_gxpipe.h"

static struct __GXFifoObj* CPUFifo;
static struct __GXFifoObj* GPFifo;
static OSThread* __GXCurrentThread;
static GXBreakPtCallback BreakPointCB;
static u32 __GXOverflowCount;

/* ---- the staging buffer behind GXW_* ---- */
static unsigned char mu_wg_buffer[64 * 1024];
unsigned char* mu_wg_cursor = mu_wg_buffer;
unsigned char* mu_wg_high_water = mu_wg_buffer + sizeof(mu_wg_buffer) - 64;

/* Provided by the host through the shim: the same stream the shipped game's decoder parses. */
void mu_host_gx_fifo_bytes(const unsigned char* data, unsigned long size);

void mu_wg_flush(void)
{
    if (mu_wg_cursor != mu_wg_buffer) {
        mu_host_gx_fifo_bytes(mu_wg_buffer, (unsigned long) (mu_wg_cursor - mu_wg_buffer));
        mu_wg_cursor = mu_wg_buffer;
    }
}

/* ---- FIFO objects ---- */
void GXInitFifoBase(GXFifoObj* fifo, void* base, u32 size)
{
    struct __GXFifoObj* realFifo = (struct __GXFifoObj*) fifo;
    realFifo->base = base;
    realFifo->top = (u8*) base + size - 4;
    realFifo->size = size;
    realFifo->count = 0;
    GXInitFifoLimits(fifo, size - 16 * 1024, (size / 2) & ~31u);
    GXInitFifoPtrs(fifo, base, base);
}

void GXInitFifoPtrs(GXFifoObj* fifo, void* readPtr, void* writePtr)
{
    struct __GXFifoObj* realFifo = (struct __GXFifoObj*) fifo;
    realFifo->rdPtr = readPtr;
    realFifo->wrPtr = writePtr;
    realFifo->count = (s32) ((u8*) writePtr - (u8*) readPtr);
    if (realFifo->count < 0)
        realFifo->count += realFifo->size;
}

void GXInitFifoLimits(GXFifoObj* fifo, u32 hiWatermark, u32 loWatermark)
{
    struct __GXFifoObj* realFifo = (struct __GXFifoObj*) fifo;
    realFifo->hiWatermark = hiWatermark;
    realFifo->loWatermark = loWatermark;
}

void GXSetCPUFifo(GXFifoObj* fifo)
{
    CPUFifo = (struct __GXFifoObj*) fifo;
    if (CPUFifo)
        CPUFifo->bind_cpu = 1;
}

void GXSetGPFifo(GXFifoObj* fifo)
{
    GPFifo = (struct __GXFifoObj*) fifo;
    if (GPFifo)
        GPFifo->bind_gp = 1;
}

void GXSaveCPUFifo(GXFifoObj* fifo) { (void) fifo; mu_wg_flush(); }
void __GXSaveCPUFifoAux(struct __GXFifoObj* realFifo) { (void) realFifo; mu_wg_flush(); }
void GXSaveGPFifo(GXFifoObj* fifo) { (void) fifo; }

void GXGetGPStatus(GXBool* overhi, GXBool* underlow, GXBool* readIdle, GXBool* cmdIdle, GXBool* brkpt)
{
    *overhi = GX_FALSE;
    *underlow = GX_FALSE;
    *readIdle = GX_TRUE;
    *cmdIdle = GX_TRUE;
    *brkpt = GX_FALSE;
}

void GXGetFifoStatus(GXFifoObj* fifo, GXBool* overhi, GXBool* underflow, u32* fifoCount, GXBool* cpuWrite, GXBool* gpRead, GXBool* fifowrap)
{
    struct __GXFifoObj* realFifo = (struct __GXFifoObj*) fifo;
    *overhi = GX_FALSE;
    *underflow = GX_FALSE;
    *fifoCount = 0;
    *cpuWrite = realFifo == CPUFifo;
    *gpRead = realFifo == GPFifo;
    *fifowrap = GX_FALSE;
}

void GXGetFifoPtrs(GXFifoObj* fifo, void** readPtr, void** writePtr)
{
    struct __GXFifoObj* realFifo = (struct __GXFifoObj*) fifo;
    *readPtr = realFifo->rdPtr;
    *writePtr = realFifo->wrPtr;
}

void* GXGetFifoBase(GXFifoObj* fifo) { return ((struct __GXFifoObj*) fifo)->base; }
u32 GXGetFifoSize(GXFifoObj* fifo) { return ((struct __GXFifoObj*) fifo)->size; }

void GXGetFifoLimits(GXFifoObj* fifo, u32* hi, u32* lo)
{
    struct __GXFifoObj* realFifo = (struct __GXFifoObj*) fifo;
    *hi = realFifo->hiWatermark;
    *lo = realFifo->loWatermark;
}

GXBreakPtCallback GXSetBreakPtCallback(GXBreakPtCallback cb)
{
    GXBreakPtCallback old = BreakPointCB;
    BreakPointCB = cb;
    return old;
}

void GXEnableBreakPt(void* break_pt) { (void) break_pt; }
void GXDisableBreakPt(void) {}

void __GXFifoInit(void)
{
    CPUFifo = 0;
    GPFifo = 0;
    __GXCurrentThread = OSGetCurrentThread();
    __GXOverflowCount = 0;
}

/* __GXFifoReadEnable, __GXFifoLink, __GXWriteFifoIntEnable and the rest were file-local to
 * GXFifo.c and only ever called from inside it, so they have no callers here. */
void __GXInsaneWatermark(void) {}
void __GXCleanGPFifo(void) { mu_wg_flush(); }

OSThread* GXSetCurrentGXThread(void)
{
    OSThread* prev = __GXCurrentThread;
    __GXCurrentThread = OSGetCurrentThread();
    return prev;
}

OSThread* GXGetCurrentGXThread(void) { return __GXCurrentThread; }
GXFifoObj* GXGetCPUFifo(void) { return (GXFifoObj*) CPUFifo; }
GXFifoObj* GXGetGPFifo(void) { return (GXFifoObj*) GPFifo; }
u32 GXGetOverflowCount(void) { return __GXOverflowCount; }

u32 GXResetOverflowCount(void)
{
    u32 old = __GXOverflowCount;
    __GXOverflowCount = 0;
    return old;
}

/* Redirecting the pipe into memory is how the console recorded display lists. Nothing in the game
 * does it (GXBeginDisplayList is never called), so it is left unimplemented on purpose. */
volatile void* GXRedirectWriteGatherPipe(void* ptr)
{
    (void) ptr;
    OSPanic(__FILE__, __LINE__, "GXRedirectWriteGatherPipe is not available natively");
    return 0;
}

void GXRestoreWriteGatherPipe(void) {}
