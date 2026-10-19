/* GrTDa export 5, +0: retail Peach target on_load, then the file's load message.
 * Every other callback and the map table stay the MxDt row's retail ones. */
#include "../common/mu_gr_common.h"
#include <melee/gr/grtpeach.h>
#include <dolphin/os.h>
static void daisy_load(void)
{
    grTPe_StageData.on_load();
    OSReport("Daisy target test loaded!\n");
}
const MuGrStage mu_gr_targets_daisy = {
    .name = "Targets!Daisy", .file = "GrTDa.dat", .on_load = daisy_load, .flags = MU_GR_READY,
};
