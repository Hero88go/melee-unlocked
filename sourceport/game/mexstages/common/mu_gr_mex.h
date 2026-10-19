/* The m-ex runtime routines native stage code calls (run-source/rel09-stages/WORKPACKAGES.md P5).
 *
 * On the console these are routines of the m-ex codeset that stage blocks reach by fixed addresses
 * (803D7054 to 803D70A8). Each is a C2 code of the disc's codes.gct hooked at its address, the same
 * bytes on Akaneia 1.0.1 and ACE 2.0.0; m-ex's boot code (the C2 code at HSD_OSInit+AC) fills the r2
 * words those bodies read from mexData. Each service below names its routine and says what the
 * body does; no PowerPC runs here. The MxDt tables are the disc's bytes (big-endian, pointer words
 * relocated in place): read them through the types of shim/mu_mex_data.h with DP().
 *
 * A routine the blocks call that is not here: ask in run-source/rel09-stages/requests/WP<n>.md. */
#ifndef MU_GR_MEX_H
#define MU_GR_MEX_H

#include <Runtime/platform.h>

#include "../../shim/mu_mex_data.h"

/* 803D706C (MexTK calloc): HSD_MemAlloc(size), then memset(p, 0, size). Body: r31 = size;
 * bl HSD_MemAlloc; bl memset(p, 0, size); return p. Size 0 gives NULL (HSD_MemAlloc); a failed
 * allocation stops in HSD_MemAlloc's assert, as on the console. Free it with HSD_Free (the blocks
 * pass HSD_Free as the user data destructor). */
void* mu_gr_mex_calloc(u32 size);

/* 803D7078: plays sound `id` of the current stage's own sound bank. Body: bank = MxDt stage sound
 * row byte 0 of stage_info.grkind (the r2 word mexData->stage->sound); Ground_801C53EC(bank * 10000
 * + id), which is lbAudioAx_800237A8(id, 0x7F, 0x40). No return value. Not MexTK's bp(): the blocks
 * call it on ordinary paths with the id in r3 (a checkpoint, a wall breaking, a lava rise), so there
 * is no mu_gr_mex_bp. A bank that is not loaded plays nothing: the release build turns an id past
 * the retail ones into the retail "no sound" id, the experimental build asks the driver, which
 * refuses an id of a bank not in audio memory. Any HSD_Randi draw for the id stays in the caller. */
void mu_gr_mex_sfx(int id);

/* 803D7094 MEX_GetData(index): the table m-ex keeps for `index` (MexTK MEX_GETDATA order; the body
 * is a jump table over r2 words). An index past 18 prints the console's
 * "error: MEX_GetData() does not have data for id %d" and stops (m-ex asserts). NULL in the retail
 * view. The blocks use only 16 (MU_MXDT_MEXDATA), 12 (MU_MXDT_FTNAME) and 15
 * (MU_MXDT_FTEMBLEMLOOKUP); the helpers below do what they do with them and are the easier path.
 * Tables 12 to 15 are indexed by the disc's EXTERNAL fighter id, while Player_GetPlayerCharacter
 * gives a native character kind (an added fighter's is not its external id): convert it with
 * mu_ak_mex_external first, or use the helpers. */
enum {
    MU_MXDT_FTINTNUM = 0,         /* the count itself, not a table: (int) (mu_uptr) result */
    MU_MXDT_FTEXTNUM = 1,         /* count */
    MU_MXDT_FTICONNUM = 2,        /* count */
    MU_MXDT_FTICONDATA = 3,       /* MexMenu.css, the character select data block */
    MU_MXDT_GRINTNUM = 4,         /* count */
    MU_MXDT_GREXTNUM = 5,         /* count */
    MU_MXDT_GRICONNUM = 6,        /* count */
    MU_MXDT_GRICONDATA = 7,       /* MexMenu.sss, the stage select icons (0x20 bytes each) */
    MU_MXDT_FTCOSTUMEARCHIVE = 8, /* m-ex run time data: NULL here (akaneia mu_ak_costume_archive) */
    MU_MXDT_GRDESC = 9,           /* MexData.stage_desc: MexGrDescPtr[internal] (raw retail words) */
    MU_MXDT_GREXTLOOKUP = 10,     /* MexStage.ext: MexStageExt[external] */
    MU_MXDT_GRNAME = 11,          /* MexStage.names: MexStr[internal] */
    MU_MXDT_FTNAME = 12,          /* MexFighter.names: MexStr[external] */
    MU_MXDT_FTDAT = 13,           /* MexFighter.pl_files: MexPlFile[internal] */
    MU_MXDT_FTKINDDESC = 14,      /* MexFighter.ext_map: MexExtMap[external] */
    MU_MXDT_FTEMBLEMLOOKUP = 15,  /* MexFighter.insignia: u8[external] */
    MU_MXDT_MEXDATA = 16,         /* the MexData root */
    MU_MXDT_PRELOADHEAP = 17,     /* m-ex run time data: NULL here */
    MU_MXPT_FILE = 18,            /* m-ex run time data (the MxPt archive): NULL here */
};
const void* mu_gr_mex_data(int index);

/* ---- what the blocks do with MEX_GetData, as helpers ---- */

/* MEX_GetData(16)->stage->sound[grkind * 3]: the sound bank of an internal stage id, -1 when there
 * is none (retail view, no row). The blocks read it for Stage_8022519C(Stage_80225194()) and add
 * the sound's index to bank * 10000 for HSD_AudioSFXStartParam. */
int mu_gr_mex_stage_bank(int grkind);

/* MEX_GetData(12)[external id of `ckind`]: the disc's display name of a native character kind
 * (Player_GetPlayerCharacter), "?" when MxDt has no name there, NULL when the kind has no external
 * id or in the retail view. The blocks compare it with strcmp ("Fox", "Wolf"...). */
const char* mu_gr_mex_fighter_name(int ckind);

/* MEX_GetData(15)[external id of `ckind`]: the emblem (series icon) index MxDt gives a native
 * character kind, 0 when it has none (as mu_mex_insignia). */
int mu_gr_mex_fighter_emblem(int ckind);

/* 803D707C: the playlist row of the current stage (MxDt stage playlists[stage_info.grkind]), NULL
 * when there is none. Body: mexData->stage->playlists + 8 * stage_info.grkind. Writable, as on the
 * console: Village's OnInit (Village_InitPlaylist+E4) rewrites the chance of each of its songs in
 * place, and the m-ex music pick (gr/ground.c Ground_801C24F8) reads the same rows. */
MexPlaylist* mu_gr_mex_playlist(void);

#endif
