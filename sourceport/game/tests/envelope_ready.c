#include <assert.h>
#include <float.h>
#include <stddef.h>
typedef struct { void* envelopemtx; } HSD_JObj;
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
    return 0;
}
