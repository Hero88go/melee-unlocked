/* A costume accessory may contain fewer meshes than its retail visibility table. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#define MU_NATIVE 1
#define DP(p) (p)
typedef unsigned char u8;
typedef struct HSD_DObj { int flags; } HSD_DObj;
typedef struct { unsigned count; HSD_DObj** data; } DObjList;
typedef struct { int x0; u8* x4; } TempS;
typedef struct { int x0; TempS* x4; } FtPartsVisLookup;
typedef struct { int model_num; FtPartsVisLookup* xC[5]; int cleared[5]; } FtPartsVis;
enum { true = 1, false = 0 };
static HSD_DObj mesh;
static int writes;
static void HSD_DObjSetFlags(HSD_DObj* object, int bits) {
    assert(object == &mesh); object->flags |= bits; ++writes;
}
static void HSD_DObjClearFlags(HSD_DObj* object, int bits) {
    assert(object == &mesh); object->flags &= ~bits; ++writes;
}
#include "ftParts_80074D7C_body.h"
#include "ftParts_80074CA0_body.h"
int main(void) {
    /* The unused entries deliberately hold invalid pointers, as a reused object pool can. */
    HSD_DObj* objects[256];
    for (int i = 0; i < 256; ++i) objects[i] = (HSD_DObj*)(uintptr_t)0x14141414;
    objects[0] = &mesh;
    objects[1] = NULL;
    u8 indices[] = {0, 1, 31, 32, 255};
    TempS group = {5, indices};
    FtPartsVisLookup lookup = {1, &group};
    FtPartsVis visibility = {1, {&lookup, NULL, NULL, NULL, NULL}, {1, 0, 0, 0, 0}};
    DObjList list = {1, objects};
    ftParts_80074D7C(&visibility, 0, &list);
    assert(mesh.flags == 1 && writes == 1 && !visibility.cleared[0]);
    ftParts_80074CA0(&visibility, 0, &list);
    assert(mesh.flags == 0 && writes == 2 && visibility.cleared[0]);
    /* A legitimate null mesh must not be dereferenced either. */
    list.count = 2;
    ftParts_80074D7C(&visibility, 0, &list);
    assert(writes == 3);
    return 0;
}
