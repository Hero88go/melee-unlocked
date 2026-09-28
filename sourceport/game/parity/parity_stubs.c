/* Symbols the maths sources reference but the parity library never reaches: lbvector.c's
 * screen-space helpers want a camera and the GX projection, and the assertion sink. */


void* HSD_CObjGetEyePosition(void* cobj, void* out) { (void) cobj; return out; }
void* HSD_CObjGetInterest(void* cobj, void* out) { (void) cobj; return out; }
int HSD_CObjGetProjectionType(void* cobj) { (void) cobj; return 0; }
void* HSD_CObjGetUpVector(void* cobj, void* out) { (void) cobj; return out; }
void* HSD_CObjGetViewingMtxPtr(void* cobj) { (void) cobj; return 0; }
void GXProject(void) {}
void MTXOrtho(void) {}
void MTXPerspective(void) {}
void C_MTXLookAt(void) {}
void mu_trace_native_world_to_screen(void* cobj, const float* world,
                                     const float* eye, const float* up,
                                     const float* target, const float* view_z,
                                     const float* screen, int d)
{
    (void) cobj; (void) world; (void) eye; (void) up; (void) target;
    (void) view_z; (void) screen; (void) d;
}
void __assert(char* str, unsigned long line, char* file) { (void) str; (void) line; (void) file; __builtin_trap(); }
/* mtx.c's matrix-stack allocator; the bone matrix builders under test never allocate. */
void* HSD_ObjAlloc(void* info) { (void) info; __builtin_trap(); }
void HSD_ObjFree(void* info, void* obj) { (void) info; (void) obj; __builtin_trap(); }
void HSD_ObjAllocInit(void* info, unsigned long size, unsigned long align) { (void) info; (void) size; (void) align; }
