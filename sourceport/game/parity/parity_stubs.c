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
void __assert(char* str, unsigned long line, char* file) { (void) str; (void) line; (void) file; __builtin_trap(); }
