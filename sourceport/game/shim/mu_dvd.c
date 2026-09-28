/* The disc. The host owns the image and its filesystem table; the game names files by path or entry
 * number exactly as on the console, and reads complete through the event queue as the drive's
 * interrupt did. */
#include <dolphin/dvd.h>

#include "mu_shim.h"

void DVDInit(void) {}
BOOL DVDCheckDisk(void) { mu_poll(); return 1; }
long DVDGetDriveStatus(void) { mu_poll(); return mu_host->disc_status(); }
s32 DVDConvertPathToEntrynum(const char* path) { return mu_host->disc_entrynum(path); }

/* The disc header the console's boot loader copied to the start of MEM1; the host does the same. */
struct DVDDiskID* DVDGetCurrentDiskID(void) { return (struct DVDDiskID*) (uintptr_t) 0x80000000u; }

BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo)
{
    uint32_t start, length;
    if (!mu_host->disc_file(entrynum, &start, &length))
        return 0;
    fileInfo->startAddr = start;
    fileInfo->length = length;
    fileInfo->callback = 0;
    fileInfo->cb.state = 0;   /* DVD_STATE_END: idle */
    return 1;
}

BOOL DVDClose(DVDFileInfo* fileInfo)
{
    (void) fileInfo;
    return 1;
}

static void mu_dvd_finish(void* a, intptr_t result)
{
    DVDFileInfo* fileInfo = (DVDFileInfo*) a;
    fileInfo->cb.state = result < 0 ? -1 : 0;
    fileInfo->cb.transferredSize = result < 0 ? 0 : (u32) result;
    if (fileInfo->callback)
        fileInfo->callback((s32) result, fileInfo);
}

/* From the host, inside poll(): queued so it runs when the interrupt would have. */
static void mu_dvd_done(int32_t result, void* user)
{
    mu_post(mu_dvd_finish, user, result);
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, DVDCallback callback, s32 prio)
{
    (void) prio;
    fileInfo->callback = callback;
    fileInfo->cb.state = 1;   /* DVD_STATE_BUSY */
    fileInfo->cb.addr = addr;
    fileInfo->cb.length = (u32) length;
    fileInfo->cb.offset = fileInfo->startAddr + (u32) offset;
    fileInfo->cb.transferredSize = 0;
    mu_host->disc_read(fileInfo->startAddr + (u32) offset, addr, (uint32_t) length, mu_dvd_done, fileInfo);
    return 1;
}
