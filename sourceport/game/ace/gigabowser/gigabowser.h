/* ACE 2.0.0's playable Giga Bowser, native.
 *
 * The fighter ships in PlGkp.dat (ftData symbol "ftDataGkooopa") with an m-ex "ftFunction" block
 * of 0x424 bytes that exports one slot, OnLoad. Every other slot is the m-ex default MxDt names
 * for him, and each of those is the function the retail game has for its own Giga Bowser
 * (Ft_Kind_GKoops): his death and item events, his frame event, Bowser's eight special move
 * entries, the 23 state table at 0x803D35E8 (ftGk_Init_MotionStateTable), the two results screen
 * callbacks and the demo state table. So this port has no state table of its own and no special
 * move code: the registry fills those slots from the retail fighter's rows, and the only code
 * written here is OnLoad and the CPU helper m-ex links into the file.
 *
 * Offsets in comments ("code+0x44") are offsets into that ftFunction block; the listing and the
 * function table are in run-source/rel09-ace-native/gigabowser/. */
#ifndef MU_ACE_GIGABOWSER_H
#define MU_ACE_GIGABOWSER_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <melee/ft/types.h>

#include "../../akaneia/mu_ak_fighter.h"

/* The kind a CPU Giga Bowser poses as while the game's CPU code runs: the second argument of
 * MexCPU_InitSpoofData in [OnLoad] (code+0x24, li r4, 0x1F), written to fp->kind as is. 0x1F is
 * the retail Giga Bowser, and the game's CPU code compares kinds with the retail numbers. */
#define ftGkp_CpuSpoofKind Ft_Kind_GKoops

/* code+0x44 [MexCPU_InitSpoofData] */
void ftGkp_Cpu_InitSpoofData(HSD_GObj* gobj);

#endif
