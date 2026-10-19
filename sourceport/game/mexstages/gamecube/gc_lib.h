#ifndef MEXGR_GC_LIB_H
#define MEXGR_GC_LIB_H
#include "../common/mu_gr_common.h"
#include "../common/mu_gr_mex.h"
#include <melee/gr/types.h>
#include <sysdolphin/baselib/gobj.h>
void* mexgr_gc_yakumono(const char* file, int line);
bool mexgr_gc_unk(Ground_GObj* gobj);
bool mexgr_gc_false(Ground_GObj* gobj);
void mexgr_gc_anim0_create(Ground_GObj* gobj);
void mexgr_gc_deletion(Ground_GObj* gobj);
#define GC_VARS(type, gp) ((type*) (void*) &((Ground*) (gp))->u)
#define GC_FIT(type) _Static_assert(sizeof(type) <= sizeof(((Ground*) 0)->u), #type " fits Ground.u")
#endif
