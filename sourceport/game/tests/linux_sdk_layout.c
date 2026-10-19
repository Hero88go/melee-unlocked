/* The LP64 overlay must preserve console scalar and sample widths. Native
 * pointers remain 64-bit; these SDK working structs are not disc structures. */
#include <dolphin/types.h>
#include <dolphin/ax.h>
#include <dolphin/axfx.h>

_Static_assert(sizeof(s32) == 4 && sizeof(u32) == 4, "console integer widths");
_Static_assert(sizeof(s64) == 8 && sizeof(u64) == 8, "console 64-bit integer widths");
_Static_assert(sizeof(((struct AXFX_REVSTD_DELAYLINE*) 0)->inPoint) == 4, "audio delay index width");
_Static_assert(sizeof(((struct AXFX_CHORUS_WORK*) 0)->oldLeft[0]) == 4, "audio history sample width");
_Static_assert(sizeof(*((struct AXFX_BUFFERUPDATE*) 0)->left) == 4, "reverb sample width");
_Static_assert(sizeof(*((struct AX_AUX_DATA*) 0)->l) == 4, "AX auxiliary sample width");
_Static_assert(sizeof(void*) == 8, "native pointers must not narrow");

int main(void) { return 0; }
