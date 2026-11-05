#include <assert.h>
#include <float.h>
#include <stddef.h>
enum { JOBJ_SKELETON_ROOT = 2 };
typedef struct { void* envelopemtx; unsigned int flags; } HSD_JObj;
static HSD_JObj root = {NULL, JOBJ_SKELETON_ROOT};
static HSD_JObj* current = &root;
static HSD_JObj* skeleton = &root;
static HSD_JObj* HSD_JObjGetCurrent(void) { return current; }
static HSD_JObj* HSD_JObjFindSkeleton(HSD_JObj* joint) { (void) joint; return skeleton; }
typedef struct HSD_Envelope { struct HSD_Envelope* next; HSD_JObj* jobj; float weight; } HSD_Envelope;
typedef struct HSD_SList { struct HSD_SList* next; void* data; } HSD_SList;
typedef struct { union { HSD_SList* envelope_list; } u; } HSD_PObj;
#include "PObjNativeEnvelopeReady_body.h"
int main(void)
{
    HSD_JObj bone = {NULL}; HSD_Envelope env = {NULL,&bone,0.5f};
    HSD_SList list = {NULL,&env}; HSD_PObj mesh = {{&list}};
    assert(!PObjNativeEnvelopeReady(&mesh));
    bone.envelopemtx = &bone; assert(PObjNativeEnvelopeReady(&mesh));
    env.jobj = NULL; assert(!PObjNativeEnvelopeReady(&mesh));
    env.jobj = &bone; bone.envelopemtx = NULL; env.weight = 1;
    assert(PObjNativeEnvelopeReady(&mesh));
    current = NULL; assert(!PObjNativeEnvelopeReady(&mesh));
    current = &bone; assert(!PObjNativeEnvelopeReady(&mesh));
    bone.envelopemtx = &bone; assert(PObjNativeEnvelopeReady(&mesh));
    skeleton = NULL; assert(!PObjNativeEnvelopeReady(&mesh));
    skeleton = &bone; bone.envelopemtx = NULL; assert(!PObjNativeEnvelopeReady(&mesh));
    current = &root; env.weight = 1.0f - FLT_EPSILON;
    assert(!PObjNativeEnvelopeReady(&mesh));
    return 0;
}
