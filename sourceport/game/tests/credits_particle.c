/* Exercise the production credits highlight. The joint's union holds a DObj: the console variant
 * walks it as 32-bit nodes, the native variant follows the host DObj -> next -> MObj -> Material
 * chain (8-byte pointers) and must write mat->diffuse of the right DObj, including when that DObj
 * has a next one (the 0.8.82 crash) and when a link is missing. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

typedef int32_t s32;
typedef struct { uint8_t r, g, b, a; } GXColor;
#ifdef MU_NATIVE
typedef struct HSD_Material { GXColor ambient, diffuse, specular; float alpha, shininess; } HSD_Material;
typedef struct HSD_MObj { void* class_info; uint32_t rendermode; void* tobj; HSD_Material* mat; } HSD_MObj;
typedef struct HSD_DObj { void* class_info; struct HSD_DObj* next; HSD_MObj* mobj; void* pobj; } HSD_DObj;
typedef struct { union { void* ptcl; HSD_DObj* dobj; } u; } HSD_JObj;
static HSD_DObj* HSD_JObjGetDObj(HSD_JObj* jobj) { return jobj ? jobj->u.dobj : NULL; }
static HSD_Material mats[4];
static HSD_MObj mobjs[4];
static HSD_DObj dobjs[4];
#else
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
#define DP(p) disc_node(p)
#endif
typedef struct { GXColor active_color; } HSD_Text;
static HSD_JObj joint;
static HSD_JObj* gm_804D682C;
static int gm_803DD1C8[7];
static struct { HSD_Text* win[2]; } staffInfo[8];
static int missing_joint;
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
static int links_for(int idx) { return idx == 0 ? 3 : (idx == 2 || idx == 3 ? 1 : 0); }
#ifdef MU_NATIVE
static void make_chain(void) {
    memset(mats, 0, sizeof mats);
    memset(mobjs, 0, sizeof mobjs);
    memset(dobjs, 0, sizeof dobjs);
    for (int i = 0; i < 4; ++i) {
        /* Host addresses: their upper halves are not zero in a 64-bit image, as on the game heap
         * the lower halves read byte-swapped are not addresses. */
        dobjs[i].class_info = &dobjs[i];
        dobjs[i].next = i < 3 ? &dobjs[i + 1] : NULL;
        dobjs[i].mobj = &mobjs[i];
        mobjs[i].class_info = &mobjs[i];
        mobjs[i].mat = &mats[i];
    }
    joint.u.dobj = &dobjs[0];
}
static int painted(int i, const GXColor* color) { return !memcmp(&mats[i].diffuse, color, sizeof *color); }
#else
static int make_chain_console(int idx) {
    memset(nodes, 0, sizeof nodes);
    const int links = links_for(idx);
    for (int i = 0; i < links; ++i) nodes[i].x4.ptr = i + 2;
    nodes[links].x8 = links + 2;
    nodes[links + 1].xC = links + 3;
    joint.u.ptcl = &nodes[0];
    return links + 2;
}
#endif
int main(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    GXColor color = {0x12, 0x34, 0x56, 0x78};
    const GXColor none = {0, 0, 0, 0};
    for (int idx = 0; idx < 7; ++idx) {
        const int links = links_for(idx);
#ifdef MU_NATIVE
        /* The full chain: only the DObj `links` hops down gets the color. */
        make_chain();
        gm_801AB200_ptcl(idx, &color);
        for (int i = 0; i < 4; ++i) {
            if (painted(i, &color) != (i == links)) ++failures;
        }
        /* Absent joint, DObj, next link, MObj and material: nothing is written, nothing faults. */
        for (int gap = 0; gap < 5; ++gap) {
            make_chain();
            if (gap == 0) missing_joint = 1;
            else if (gap == 1) joint.u.dobj = NULL;
            else if (gap == 2) { if (links == 0) continue; dobjs[links - 1].next = NULL; }
            else if (gap == 3) dobjs[links].mobj = NULL;
            else mobjs[links].mat = NULL;
            gm_801AB200_ptcl(idx, &color);
            missing_joint = 0;
            for (int i = 0; i < 4; ++i) {
                if (!painted(i, &none)) ++failures;
            }
        }
#else
        s32 word;
        memcpy(&word, &color, sizeof word);
        const int tail = make_chain_console(idx);
        gm_801AB200_ptcl(idx, &color);
        if (nodes[tail].x4.color != word) ++failures;
        (void)links;
#endif
    }
    (void)none;
    HSD_Text first = {{0}}, second = {{0}};
    staffInfo[7].win[0] = &first; staffInfo[7].win[1] = &second;
    gm_801AB200_ptcl(7, &color);
    if (memcmp(&first.active_color, &color, sizeof color) ||
        memcmp(&second.active_color, &color, sizeof color)) ++failures;
    printf("credits particle regression: %s\n", failures ? "FAIL" : "PASS");
    return failures != 0;
}
