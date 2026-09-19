/* The memory card: slot A is the host's folder of .gci files, slot B is empty. Operations complete at
 * once on the host, and the asynchronous forms deliver their callback through the event queue, as the
 * card's interrupt did. */
#include <dolphin/card.h>

#include "mu_shim.h"

#define MU_CARD_READY 0
#define MU_CARD_NOCARD (-3)

static s32 mu_xferred;

static void mu_card_finish(void* a, intptr_t packed)
{
    CARDCallback callback = (CARDCallback) a;
    callback((s32) (packed >> 32), (s32) (int32_t) packed);
}

/* result is returned too, so every async wrapper reads as one line. */
static s32 mu_card_complete(CARDCallback callback, s32 chan, s32 result)
{
    if (callback && result == MU_CARD_READY)
        mu_post(mu_card_finish, (void*) callback, (intptr_t) (((int64_t) chan << 32) | (uint32_t) result));
    return result;
}

void CARDInit(void) {}
int CARDProbe(long chan) { mu_poll(); return chan == 0; }
s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize) { return mu_host->card_probe(chan, (int32_t*) memSize, (int32_t*) sectorSize); }
s32 CARDUnmount(s32 chan) { return mu_host->card_unmount(chan); }
long CARDGetXferredBytes(long chan) { return chan == 0 ? mu_xferred : 0; }

s32 CARDMountAsync(s32 chan, void* workArea, CARDCallback detachCallback, CARDCallback attachCallback)
{
    (void) workArea; (void) detachCallback;
    return mu_card_complete(attachCallback, chan, mu_host->card_mount(chan));
}

s32 CARDCheckAsync(s32 chan, CARDCallback callback)
{
    return mu_card_complete(callback, chan, chan == 0 ? MU_CARD_READY : MU_CARD_NOCARD);
}

s32 CARDFormatAsync(s32 chan, CARDCallback callback)
{
    return mu_card_complete(callback, chan, mu_host->card_format(chan));
}

s32 CARDFreeBlocks(s32 chan, s32* byteNotUsed, s32* filesNotUsed)
{
    return mu_host->card_free_blocks(chan, (int32_t*) byteNotUsed, (int32_t*) filesNotUsed);
}

static void mu_card_set_file(CARDFileInfo* fileInfo, s32 chan, int32_t file_no)
{
    fileInfo->chan = chan;
    fileInfo->fileNo = file_no;
    fileInfo->offset = 0;
    fileInfo->length = 0;
    fileInfo->iBlock = 0;
}

s32 CARDOpen(s32 chan, const char* fileName, CARDFileInfo* fileInfo)
{
    int32_t file_no = -1;
    uint32_t length = 0;
    s32 result = mu_host->card_open(chan, fileName, &file_no, &length);
    if (result == MU_CARD_READY) {
        mu_card_set_file(fileInfo, chan, file_no);
        fileInfo->length = (s32) length;
    }
    return result;
}

s32 CARDFastOpen(s32 chan, s32 fileNo, CARDFileInfo* fileInfo)
{
    CARDStat stat;
    s32 result = mu_host->card_stat(chan, fileNo, &stat, sizeof stat);
    if (result == MU_CARD_READY) {
        mu_card_set_file(fileInfo, chan, fileNo);
        fileInfo->length = (s32) stat.length;
    }
    return result;
}

s32 CARDClose(CARDFileInfo* fileInfo)
{
    s32 result = mu_host->card_close(fileInfo->chan, fileInfo->fileNo);
    fileInfo->chan = -1;
    return result;
}

s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo, CARDCallback callback)
{
    int32_t file_no = -1;
    s32 result = mu_host->card_create(chan, fileName, size, &file_no);
    if (result == MU_CARD_READY) {
        mu_card_set_file(fileInfo, chan, file_no);
        fileInfo->length = (s32) size;
    }
    return mu_card_complete(callback, chan, result);
}

s32 CARDDeleteAsync(s32 chan, char* fileName, CARDCallback callback)
{
    return mu_card_complete(callback, chan, mu_host->card_delete(chan, fileName));
}

s32 CARDRenameAsync(s32 chan, const char* oldName, const char* newName, CARDCallback callback)
{
    return mu_card_complete(callback, chan, mu_host->card_rename(chan, oldName, newName));
}

long CARDRead(struct CARDFileInfo* fileInfo, void* buf, long length, long offset)
{
    s32 result = mu_host->card_read(fileInfo->chan, fileInfo->fileNo, buf, (uint32_t) length, (uint32_t) offset);
    if (result == MU_CARD_READY)
        mu_xferred += length;
    return result;
}

s32 CARDReadAsync(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset, CARDCallback callback)
{
    return mu_card_complete(callback, fileInfo->chan, CARDRead(fileInfo, buf, length, offset));
}

long CARDWrite(struct CARDFileInfo* fileInfo, void* buf, long length, long offset)
{
    s32 result = mu_host->card_write(fileInfo->chan, fileInfo->fileNo, buf, (uint32_t) length, (uint32_t) offset);
    if (result == MU_CARD_READY)
        mu_xferred += length;
    return result;
}

long CARDWriteAsync(struct CARDFileInfo* fileInfo, void* buf, long length, long offset, void (*callback)(long, long))
{
    return mu_card_complete((CARDCallback) callback, fileInfo->chan, CARDWrite(fileInfo, buf, length, offset));
}

/* CARDStat holds no pointers, so the host reads and writes it as the game lays it out. */
s32 CARDGetStatus(s32 chan, s32 fileNo, CARDStat* stat)
{
    return mu_host->card_stat(chan, fileNo, stat, sizeof *stat);
}

s32 CARDSetStatusAsync(s32 chan, s32 fileNo, CARDStat* stat, CARDCallback callback)
{
    return mu_card_complete(callback, chan, mu_host->card_set_stat(chan, fileNo, stat, sizeof *stat));
}
