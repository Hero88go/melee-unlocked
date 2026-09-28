/* These types overlay archive bytes, including the two views of an animation
 * entry. A host pointer or size_t silently moves fields even when C compiles. */
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftPikachu/types.h>
#include <melee/lb/lbanim.h>
#include <melee/it/itCharItems.h>
#include <melee/it/itCommonItems.h>

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
_Static_assert(sizeof(itSamusGrappleAttributes) == 0xB0, "grapple attribute stride");
DISC_OFFSET(itSamusGrappleAttributes, x74, 0x74);
DISC_OFFSET(itSamusGrappleAttributes, xAC, 0xAC);
_Static_assert(sizeof(AnimBundle) == 0xC, "item animation bundle stride");
DISC_OFFSET(itLinkBoomerangAttributes, x58_anim, 0x58);
_Static_assert(sizeof(it_2E5A_TierEntry) == 0x2C, "item tier stride");
DISC_OFFSET(it_2E5A_Attrs, tiers, 0x3C);
DISC_OFFSET(itWhiteBeaAttributes, x14, 0x14);
_Static_assert(sizeof(itGamewatchchefAttrEntry) == 0x14, "projectile entry stride");
_Static_assert(sizeof(union ColorOverlay_x8_t) == 4, "color command stride");
_Static_assert(sizeof(Fighter_x2D0_t) == 0x34, "multi-jump attribute stride");
DISC_OFFSET(Fighter_x2D0_t, x2C, 0x2C);
