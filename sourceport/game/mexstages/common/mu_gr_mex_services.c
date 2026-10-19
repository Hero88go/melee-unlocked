/* m-ex stage services that stage blocks call through the console's m-ex runtime.
 *
 * mu_gr_mex_bgm_clock: Beat Block Galaxy (GrBeat +7D4/+AE4) reads the console's music streaming
 * state (the BGM voice, the stream chunk headers) to time its beats to the song. The Source Port
 * plays music through the host jukebox (shim/mu_audio.c, mu_jukebox_music: every run but
 * --vanilla-game), where that transport does not exist; the block has its own deterministic 60 Hz
 * path for a console whose master clock routine is patched, and the snapshot sends it there. With
 * --vanilla-game the emulated streams are not read either: the same path (a mod disc does not run
 * with --vanilla-game). The snapshot reports no voice, so the block's streaming branch is never
 * taken natively.
 *
 * mu_gr_mex_devtext_remove: Swordplay Arena's menu (GrWiiSword DbMenu_Free +A14) takes its text
 * off the development text draw list with DevText_Remove(&devtext_drawlist), which removes the
 * list's head: the menu's own text whenever it is the newest. Here the text itself is unlinked,
 * the same result for the head; for a text further down (another development text drawn after
 * it) the console would unlink that one and leave the freed menu text in the list. */
#include "../beatblock/beatblock.h"
#include "../swordplay/swordplay.h"

#include <melee/if/textdraw.h>
#include <string.h>

extern DevText* devtext_drawlist;

int mu_gr_mex_bgm_clock(MuGrBgmClock* out)
{
    memset(out, 0, sizeof(*out));
    out->fallback = 1;
    out->stream_index = -1;
    out->stream_loading_index = -1;
    out->chunk_start = -1;
    out->chunk_next = -1;
    out->voice_loop = -1;
    out->voice_current = -1;
    return 0;
}

void mu_gr_mex_devtext_remove(DevText* text)
{
    if (devtext_drawlist == text) {
        DevText_Remove(&devtext_drawlist);
    } else {
        DevText* slot = text;
        DevText_Remove(&slot);
    }
}
