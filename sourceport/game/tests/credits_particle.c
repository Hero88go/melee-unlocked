/* Exercise the production credits highlight with absent and incomplete particles. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

typedef int32_t s32;
typedef struct { uint8_t r, g, b, a; } GXColor;
typedef struct StaffRollPtclNode {
    uint32_t x0;
    union { uint32_t ptr; s32 color; } x4;
    uint32_t x8;
    uint32_t xC;
} StaffRollPtclNode;
_Static_assert(sizeof(StaffRollPtclNode) == 16, "credits disc-node layout");
static StaffRollPtclNode nodes[6];
static StaffRollPtclNode* disc_node(uint32_t ref) { return ref ? &nodes[ref - 1] : NULL; }
typedef struct { union { void* ptcl; } u; } HSD_JObj;
typedef struct { GXColor active_color; } HSD_Text;
static HSD_JObj joint;
static HSD_JObj* gm_804D682C;
static int gm_803DD1C8[7];
static struct { HSD_Text* win[2]; } staffInfo[8];
static int missing_joint;
#define DP(p) disc_node(p)
static void lb_80011E24(HSD_JObj* root, HSD_JObj** out, int entry, int end) {
    (void)root; (void)entry; (void)end;
    /* A failed lookup deliberately leaves its output alone, as a missing child may do. */
    if (!missing_joint) *out = &joint;
}
#ifndef CREDITS_PARTICLE_FUNCTION
#define CREDITS_PARTICLE_FUNCTION "credits_particle_function.h"
#endif
#include CREDITS_PARTICLE_FUNCTION

static int failures;
static int make_chain(int idx) {
    memset(nodes, 0, sizeof nodes);
    const int links = idx == 0 ? 3 : (idx == 2 || idx == 3 ? 1 : 0);
    for (int i = 0; i < links; ++i) nodes[i].x4.ptr = i + 2;
    nodes[links].x8 = links + 2;
    nodes[links + 1].xC = links + 3;
    joint.u.ptcl = &nodes[0];
    return links + 2;
}
int main(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    GXColor color = {0x12, 0x34, 0x56, 0x78};
    s32 word;
    memcpy(&word, &color, sizeof word);
    for (int idx = 0; idx < 7; ++idx) {
        const int tail = make_chain(idx);
        gm_801AB200_ptcl(idx, &color);
        if (nodes[tail].x4.color != word) ++failures;
#ifdef MU_NATIVE
        /* Reproduce the reported null first node, then each absent downstream link. */
        make_chain(idx);
        joint.u.ptcl = NULL;
        gm_801AB200_ptcl(idx, &color);
        for (int hop = 0; hop < tail; ++hop) {
            make_chain(idx);
            if (hop < tail - 2) nodes[hop].x4.ptr = 0;
            else if (hop == tail - 2) nodes[hop].x8 = 0;
            else nodes[hop].xC = 0;
            gm_801AB200_ptcl(idx, &color);
            if (nodes[tail].x4.color != 0) ++failures;
        }
        missing_joint = 1;
        gm_801AB200_ptcl(idx, &color);
        missing_joint = 0;
#endif
    }
    HSD_Text first = {{0}}, second = {{0}};
    staffInfo[7].win[0] = &first; staffInfo[7].win[1] = &second;
    gm_801AB200_ptcl(7, &color);
    if (memcmp(&first.active_color, &color, sizeof color) ||
        memcmp(&second.active_color, &color, sizeof color)) ++failures;
    printf("credits particle regression: %s\n", failures ? "FAIL" : "PASS");
    return failures != 0;
}
