/* The m-ex runtime routines native stage code calls (mu_gr_mex.h). Each body was read from the C2
 * code of the disc's codes.gct hooked at the routine's address (Akaneia 1.0.1 and ACE 2.0.0 carry the
 * same bytes), the r2 words they read from the table m-ex's boot code fills (the C2 code at
 * HSD_OSInit+AC: r2 + 4 * i gets a path from mexData), and the call sites in the 35 stage blocks
 * (run-source/rel09-stages grdis.py). */
#include "mu_gr_mex.h"

#include <dolphin/os.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/memory.h>

#include <string.h>

/* 803D706C: mr r31, r3; bl HSD_MemAlloc; mr r30, r3; li r4, 0; mr r5, r31; bl memset; mr r3, r30.
 * The console passes the size as a signed word (HSD_MemAlloc takes ssize_t): <= 0 gives NULL. */
void* mu_gr_mex_calloc(u32 size)
{
    void* p = HSD_MemAlloc((s32) size);
    if (p != NULL) {
        memset(p, 0, size);
    }
    return p;
}

/* 803D7078: mr r31, r3; r3 = stage_info.grkind (8049E6C8 + 0x88); r4 = r2 word 0x108 (mexData
 * ->stage->sound); lbz r3, grkind * 3 (r4); r3 = r3 * 10000 + r31; bl Ground_801C53EC. */
void mu_gr_mex_sfx(int id)
{
    const int bank = mu_mex_stage_ssm(stage_info.grkind, 0);
    if (bank < 0) {
        /* No MxDt sound row for this stage: the console reads one for every id it has; only a
         * stage the native layer started without an MxDt row could get here. */
        static int logged;
        if (!logged) {
            logged = 1;
            OSReport("[mex] stage %d: no MxDt sound row; stage sound %d not played\n",
                     (int) stage_info.grkind, id);
        }
        return;
    }
    Ground_801C53EC((u32) (bank * 10000 + id));
}

/* 803D7094: cmpwi r3, 0x13; bge error; then a jump table of bl words, each target loading one r2
 * word (or a path from mexData, r2 word 0x178):
 *   0 r2+148 metadata.ft_internal   1 r2+14C ft_external     2 r2+150 css_icons
 *   3 r2+0   menu->css              4 r2+16C gr_internal     5 r2+170 gr_external
 *   6 r2+154 sss_icons              7 r2+100 menu->sss       8 r2+5C  fighter +3C (run time)
 *   9 r2+10C stage_desc             10 stage->ext            11 stage->names
 *   12 fighter->names               13 fighter->pl_files     14 fighter->ext_map (+C)
 *   15 fighter->insignia (+8)       16 mexData               17 r2+184 (run time)
 *   18 r2+1A0 (run time)
 * The r2 slots were read from the boot code's path table (r2+148 = mexData->+0->+4 and so on). */
const void* mu_gr_mex_data(int index)
{
    MexData* mx = mu_mex_root();
    MexMeta* meta;
    MexMenu* menu;
    MexFighter* ft;
    MexStage* st;
    if (index < 0 || index >= 0x13) {
        /* The console prints this, then asserts ("m-ex", line 0). A negative index jumps into
         * its table at a negative offset there; it stops here too. */
        OSReport("error: MEX_GetData() does not have data for id %d\n", index);
        OSPanic("m-ex", 0, "MEX_GetData: no such table");
    }
    if (mx == NULL) {
        return NULL;
    }
    meta = DP(mx->metadata);
    menu = DP(mx->menu);
    ft = DP(mx->fighter);
    st = DP(mx->stage);
    switch (index) {
    case MU_MXDT_FTINTNUM: return (const void*) (mu_uptr) meta->ft_internal;
    case MU_MXDT_FTEXTNUM: return (const void*) (mu_uptr) meta->ft_external;
    case MU_MXDT_FTICONNUM: return (const void*) (mu_uptr) meta->css_icons;
    case MU_MXDT_FTICONDATA: return menu != NULL ? DP(menu->css) : NULL;
    case MU_MXDT_GRINTNUM: return (const void*) (mu_uptr) meta->gr_internal;
    case MU_MXDT_GREXTNUM: return (const void*) (mu_uptr) meta->gr_external;
    case MU_MXDT_GRICONNUM: return (const void*) (mu_uptr) meta->sss_icons;
    case MU_MXDT_GRICONDATA: return menu != NULL ? DP(menu->sss) : NULL;
    case MU_MXDT_GRDESC: return DP(mx->stage_desc);
    case MU_MXDT_GREXTLOOKUP: return st != NULL ? DP(st->ext) : NULL;
    case MU_MXDT_GRNAME: return st != NULL ? DP(st->names) : NULL;
    case MU_MXDT_FTNAME: return ft != NULL ? DP(ft->names) : NULL;
    case MU_MXDT_FTDAT: return ft != NULL ? DP(ft->pl_files) : NULL;
    case MU_MXDT_FTKINDDESC: return ft != NULL ? DP(ft->ext_map) : NULL;
    case MU_MXDT_FTEMBLEMLOOKUP: return ft != NULL ? DP(ft->insignia) : NULL;
    case MU_MXDT_MEXDATA: return mx;
    default:
        /* 8, 17, 18: tables m-ex builds at run time (costume archives, its preload heap, the MxPt
         * file). The native game keeps those elsewhere; no stage block asks for them. */
        OSReport("[mex] MEX_GetData(%d): m-ex run time data, not served natively; NULL\n", index);
        return NULL;
    }
}

int mu_gr_mex_stage_bank(int grkind)
{
    return mu_mex_stage_ssm(grkind, 0);
}

const char* mu_gr_mex_fighter_name(int ckind)
{
    const int ext = mu_ak_mex_external(ckind);
    return ext >= 0 ? mu_mex_fighter_name(ext) : NULL;
}

int mu_gr_mex_fighter_emblem(int ckind)
{
    const int ext = mu_ak_mex_external(ckind);
    return ext >= 0 ? mu_mex_insignia(ext) : 0;
}

/* 803D707C: r3 = stage_info.grkind; r4 = mexData (r2 word 0x178) ->stage (+28) ->playlists (+14);
 * return r4 + grkind * 8. */
MexPlaylist* mu_gr_mex_playlist(void)
{
    MexData* mx = mu_mex_root();
    MexStage* st = mx != NULL ? DP(mx->stage) : NULL;
    MexPlaylist* rows = st != NULL ? DP(st->playlists) : NULL;
    const int grkind = (int) stage_info.grkind;
    if (rows == NULL || grkind < 0 || grkind >= (int) DP(mx->metadata)->gr_internal) {
        return NULL;
    }
    return &rows[grkind];
}
