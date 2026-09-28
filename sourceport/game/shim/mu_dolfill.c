/* Data the decompiled sources take from the retail binary rather than spelling out: two font atlases
 * (config.yml "extract:"). Nothing from the retail binary is stored in this repository, so at start
 * the tables are copied out of the DOL on the player's own disc, by the address the console loaded
 * them at. */
#include <dolphin/types.h>

#include "mu_shim.h"

typedef struct { u8 data[512]; } MuGlyph512;
typedef struct { u8 data[56]; } MuGlyph56;
extern MuGlyph512 HSD_SisLib_FontAtlas[];
extern MuGlyph56 HSD_DebugFontAtlas[];

static const struct {
    void* dest;
    unsigned int size;
    unsigned int address;   /* where the console's loader put it */
} mu_dol_tables[] = {
    { HSD_SisLib_FontAtlas, 0x23E00, 0x8040CD40u },
    { HSD_DebugFontAtlas, 0x1C00, 0x804088B8u },
};

static volatile int mu_read_done;
static void mu_read_finished(int32_t result, void* user)
{
    (void) user;
    mu_read_done = result >= 0 ? 1 : -1;
}

/* The host completes disc reads before disc_read returns; anything else would wait here. */
static int mu_disc_read_now(unsigned int offset, void* dst, unsigned int size)
{
    mu_read_done = 0;
    mu_host->disc_read(offset, dst, size, mu_read_finished, 0);
    while (mu_read_done == 0)
        mu_host->poll();
    return mu_read_done > 0;
}

static unsigned int mu_be32(const u8* p)
{
    return (unsigned int) p[0] << 24 | (unsigned int) p[1] << 16 | (unsigned int) p[2] << 8 | p[3];
}

void mu_fill_from_dol(void)
{
    u8 disc_header[0x440], dol[0x100];
    unsigned int dol_offset, t, s;
    if (!mu_disc_read_now(0, disc_header, sizeof disc_header))
        return;
    dol_offset = mu_be32(disc_header + 0x420);
    if (!mu_disc_read_now(dol_offset, dol, sizeof dol))
        return;
    for (t = 0; t < sizeof mu_dol_tables / sizeof mu_dol_tables[0]; t++) {
        int found = 0;
        /* 7 text then 11 data sections: file offsets at 0x00, addresses at 0x48, sizes at 0x90. */
        for (s = 0; s < 18 && !found; s++) {
            const unsigned int file = mu_be32(dol + s * 4), addr = mu_be32(dol + 0x48 + s * 4), size = mu_be32(dol + 0x90 + s * 4);
            if (!size || mu_dol_tables[t].address < addr || mu_dol_tables[t].address + mu_dol_tables[t].size > addr + size)
                continue;
            found = mu_disc_read_now(dol_offset + file + (mu_dol_tables[t].address - addr), mu_dol_tables[t].dest,
                                     mu_dol_tables[t].size);
        }
        if (!found)
            mu_host->log("native: a table the game takes from its DOL was not found on this disc");
    }
}
