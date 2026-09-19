/* These types overlay archive bytes, including the two views of an animation
 * entry. A host pointer or size_t silently moves fields even when C compiles. */
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftPikachu/types.h>
#include <melee/lb/lbanim.h>

#define DISC_OFFSET(type, field, offset) \
    _Static_assert(__builtin_offsetof(type, field) == (offset), #type "." #field)

_Static_assert(sizeof(ftData) == 0x60, "ftData disc stride");
DISC_OFFSET(ftData, x8, 0x08);
DISC_OFFSET(ftData, xC, 0x0C);
DISC_OFFSET(ftData, x14, 0x14);
DISC_OFFSET(ftData, x5C, 0x5C);
_Static_assert(sizeof(Fighter_WaitAnimData) == 0x18, "animation disc stride");
DISC_OFFSET(Fighter_WaitAnimData, x8, 0x08);
DISC_OFFSET(Fighter_WaitAnimData, x14, 0x14);
_Static_assert(sizeof(struct ftData_80085FD4_ret) == 0x18, "animation view stride");
DISC_OFFSET(struct ftData_80085FD4_ret, x8, 0x08);
DISC_OFFSET(struct ftData_80085FD4_ret, x14, 0x14);
DISC_OFFSET(ftPikachuAttributes, xDC, 0xDC);
DISC_OFFSET(ftPikachuAttributes, height_attributes, 0xE0);
_Static_assert(sizeof(ftPikachuAttributes) == 0xF8, "Pikachu attribute stride");
_Static_assert(sizeof(ftCollisionBox_BE) == 0x18, "collision box disc stride");
_Static_assert(sizeof(FigaTree) == 0x14, "figatree disc stride");
DISC_OFFSET(FigaTree, nodes, 0x0C);
DISC_OFFSET(FigaTree, tracks, 0x10);
_Static_assert(sizeof(FigaTrack) == 0x0C, "figatrack disc stride");
DISC_OFFSET(FigaTrack, ad_head, 0x08);
_Static_assert(sizeof(CmdUnion) == 4, "command word stride");
_Static_assert(sizeof(struct gmScriptEventDefault) == 4, "command opcode stride");
