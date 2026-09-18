/* The development kit's host channel (MCC) and file server (FIO), as a retail console has them: absent.
 *
 * HSD's debug layer opens a channel to a PC over the dev kit's EXI cable, to print and to load files.
 * A retail GameCube has no such cable, so on the machine the game shipped for, MCCInit fails and
 * everything after it is skipped. Returning the same failure here gives the retail path. The SDK's own
 * MCC/FIO sources are Metrowerks-only C and are not compiled. */
#include <dolphin/types.h>
#include <dolphin/mcc.h>

int MCCInit(enum MCC_EXI exiChannel, u8 timeout, MCC_CBSysEvent callbackSysEvent)
{
    (void) exiChannel; (void) timeout; (void) callbackSysEvent;
    return 0;
}
void MCCExit(void) {}
int MCCPing(void) { return 0; }
int MCCEnumDevices(MCC_CBEnumDevices callbackEnumDevices) { (void) callbackEnumDevices; return 0; }
u8 MCCGetFreeBlocks(enum MCC_MODE mode) { (void) mode; return 0; }
u8 MCCGetLastError(void) { return 0; }
int MCCGetChannelInfo(enum MCC_CHANNEL chID, MCC_Info* info) { (void) chID; (void) info; return 0; }
int MCCGetConnectionStatus(enum MCC_CHANNEL chID, enum MCC_CONNECT* connect) { (void) chID; (void) connect; return 0; }
int MCCNotify(enum MCC_CHANNEL chID, u32 notify) { (void) chID; (void) notify; return 0; }
int MCCOpen(enum MCC_CHANNEL chID, u8 blockSize, MCC_CBEvent callbackEvent) { (void) chID; (void) blockSize; (void) callbackEvent; return 0; }
int MCCClose(enum MCC_CHANNEL chID) { (void) chID; return 0; }
int MCCRead(enum MCC_CHANNEL chID, u32 offset, void* data, long size, enum MCC_SYNC_STATE async)
{
    (void) chID; (void) offset; (void) data; (void) size; (void) async;
    return 0;
}
int MCCWrite(enum MCC_CHANNEL chID, u32 offset, void* data, long size, enum MCC_SYNC_STATE async)
{
    (void) chID; (void) offset; (void) data; (void) size; (void) async;
    return 0;
}
int MCCStreamOpen(enum MCC_CHANNEL chID, u8 blockSize) { (void) chID; (void) blockSize; return 0; }
int MCCStreamClose(enum MCC_CHANNEL chID) { (void) chID; return 0; }
int MCCStreamWrite(enum MCC_CHANNEL chID, void* data, u32 dataBlockSize) { (void) chID; (void) data; (void) dataBlockSize; return 0; }
u32 MCCStreamRead(enum MCC_CHANNEL chID, void* data) { (void) chID; (void) data; return 0; }

int FIOInit(enum MCC_EXI exiChannel, enum MCC_CHANNEL chID, u8 blockSize) { (void) exiChannel; (void) chID; (void) blockSize; return 0; }
void FIOExit(void) {}
int FIOQuery(void) { return 0; }
u8 FIOGetLastError(void) { return 0; }
int FIOFopen(const char* filename, u32 mode) { (void) filename; (void) mode; return -1; }
int FIOFclose(int handle) { (void) handle; return 0; }
u32 FIOFread(int handle, void* data, u32 size) { (void) handle; (void) data; (void) size; return 0; }
u32 FIOFwrite(int handle, void* data, u32 size) { (void) handle; (void) data; (void) size; return 0; }
