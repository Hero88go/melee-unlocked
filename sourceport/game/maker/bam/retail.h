/* Game state overlay code reads directly. On the console these are raw
 * addresses (state the decomp keeps file-local or has not named, and
 * Slippi's own helpers); natively each is the game's own symbol, made
 * visible for Melee Maker in the game's source (#if MU_ROGUE). */
#ifndef BAM_RETAIL_H
#define BAM_RETAIL_H
#include <dolphin/types.h>
#include <dolphin/os.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/types.h>

/* ---- the scene controller ---- */
#define BAM_SCENE_MAJOR (gm_GetCurrentGameMode())
#define BAM_SCENE_MINOR (gm_GetCurrentSceneIndex())
#define BAM_SCENE_VS 2
#define BAM_SCENE_ONLINE 8       /* Slippi Online */
#define BAM_SCENE_TRAINING 0x1C
/* gm_80479D58.x2: the scene loop's "run one frame" bit (frame advance), and
 * .x3: x2 as of the last frame. */
struct gm_80479D58_t* mu_gm_engine_state(void);   /* gm/gmscene.c */
#define BAM_FRAME_STEP (mu_gm_engine_state()->unk_10.x2)
#define BAM_FRAME_STEP_SEEN (mu_gm_engine_state()->unk_10.x3)

/* ---- controllers ---- */
/* gm_EvaluateAllControllerInputs copies the pads into this per-port table
 * (controller_map) before the scene's frame runs. */
struct BamGmPadState { u64 button, trigger, repeat, release, repeat2; s32 timer, x2C; };
void* mu_bm_gm_pads(void);
#define BAM_GM_PADS ((struct BamGmPadState*) mu_bm_gm_pads())

/* ---- character select ---- */
/* mnCharSel's sub-screen (4: the name-entry keyboard) and its port. */
extern u8 mnCharSel_804D6CF6;
extern s8 mnCharSel_804D6CF9;
#define BAM_CSS_SUBSCREEN mnCharSel_804D6CF6
#define BAM_CSS_SUBSCREEN_PORT mnCharSel_804D6CF9

/* ---- fighters and effects ---- */
/* ftMapping_list: per character, internal kind, extra kind, ... */
extern struct BamFtMapping { s8 internal_id, extra_internal_id, has_transformation; } ftMapping_list[];
#define BAM_FT_MAPPING ((const s8*) ftMapping_list)
/* efAsync_DatEntries: effect files (natively the same three pointers). */
typedef struct BamEfDatEntry { char* file; char* table; void* data; } BamEfDatEntry;
extern BamEfDatEntry efAsync_DatEntries[];
#define BAM_EF_ENTRIES (efAsync_DatEntries)

/* ---- time ---- */
#define BAM_TICKS_PER_MS (OS_TIMER_CLOCK / 1000)
#endif
