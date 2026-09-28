/* Training Mode CE, native build: what the console's m-ex loader provided.
 *
 * On the console TM-CE's C is compiled into .dat files (TM/eventMenu.dat, TM/lab.dat, ...) and a
 * patched Start.dol loads them, relocates their code and hands the game a table of their functions.
 * Natively the same C is compiled into the game (tools/tmce/link_module.py gives each module its own
 * prefix), so loading a module means: load the .dat file for its data (models, menus: the player's
 * TM-CE disc) and hand back the functions compiled in. Compiled against the native MexTK headers.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "../MexTK/mex.h"
#include "../src/events.h"

/* the event state every module reaches through event_vars (a fixed console address on the console) */
void *mu_tmce_event_vars;

/* ---- the C library, with the console's int sizes (mex_native_prelude.h renames the calls) ---- */
void mex_memcpy(void *dest, const void *source, int size) { __builtin_memcpy(dest, source, (unsigned) size); }
void mex_memmove(void *dest, const void *source, int size) { __builtin_memmove(dest, source, (unsigned) size); }
void mex_memset(void *dest, int c, int size) { __builtin_memset(dest, c, (unsigned) size); }
int mex_strncmp(const char *a, const char *b, int n) { return __builtin_strncmp(a, b, (unsigned) n); }
int mex_strcpy(const char *dest, const char *source)
{
    __builtin_strcpy((char *) dest, source);
    return 0;
}
int mex_strlen(const char *s) { return (int) __builtin_strlen(s); }
int mu_crt_vsprintf(char *, const char *, va_list) __asm__("vsprintf");   /* MexTK's vsprintf is a macro */
int mex__vsprintf(char *str, int unk, const char *format, va_list arg)
{
    (void) unk;
    return mu_crt_vsprintf(str, format, arg);
}
/* MexTK's blr/blr2: a return instruction in the game, used as an empty callback */
void mex_blr(void) {}

/* m-ex's calloc: the current heap, cleared */
void *mex_calloc(int size)
{
    void *p = HSD_MemAlloc(size);
    if (p != 0)
        __builtin_memset(p, 0, (unsigned) size);
    return p;
}

/* ---- MexTK prototypes that list a function's arguments in another order than the game ----
 * On the console integer and float arguments go in separate register files, so these worked there;
 * natively each argument has one slot, so they call the game's function in its own order
 * (tools/tmce/check_signatures.py finds them; tools/tmce/function_overrides.json routes the calls). */
void mu_g_Fighter_ChangeMotionState(GOBJ *, int, int, float, float, float, GOBJ *) __asm__("Fighter_ChangeMotionState");
void mex_ActionStateChange(float start_frame, float anim_speed, float anim_blend, GOBJ *fighter, int state_id,
                           int flags, GOBJ *alt_state_source)
{
    mu_g_Fighter_ChangeMotionState(fighter, state_id, flags, start_frame, anim_speed, anim_blend, alt_state_source);
}
int mu_g_mpCheckFloor(float, float, float, float, float, Vec3 *, int *, u32 *, Vec3 *, int, int, int, void *, GOBJ *)
    __asm__("mpCheckFloor");
int mex_GrColl_RaycastGround(Vec3 *coll_pos, int *line_index, int *line_kind, Vec3 *unk1, int unk2, int unk3, int unk4,
                             void *cb, float from_x, float from_y, float to_x, float to_y, float unk5)
{
    return mu_g_mpCheckFloor(from_x, from_y, to_x, to_y, unk5, coll_pos, line_index, (u32 *) line_kind, unk1, unk2,
                             unk3, unk4, cb, 0);
}
void mu_g_lbColl_80008FC8(Vec3, Vec3, GXColor *, GXColor *, float) __asm__("lbColl_80008FC8");
void mex_Develop_DrawSphere(float size, Vec3 *pos1, Vec3 *pos2, GXColor *diffuse, GXColor *ambient)
{
    mu_g_lbColl_80008FC8(*pos1, *pos2, diffuse, ambient, size);
}
void mu_g_DevText_SetBGColor(DevText *, GXColor) __asm__("DevText_SetBGColor");
void mex_DevelopText_StoreBGColor(DevText *text, GXColor *rgba)
{
    mu_g_DevText_SetBGColor(text, *rgba);
}
/* the game passes the knockback magnitude; MexTK leaves that argument out (a stale register on the
 * console), so the fighter's own current knockback magnitude is passed */
float mu_g_ftCo_Damage_CalcAngle(FighterData *, float) __asm__("ftCo_Damage_CalcAngle");
float mex_Fighter_GetKnockbackAngle(FighterData *fighter_data)
{
    return mu_g_ftCo_Damage_CalcAngle(fighter_data, fighter_data->dmg.kb_mag);
}

/* ---- the modules ---- */
#define EV_DECL(m)                                                                                  \
    void tmce_##m##_Event_Init(GOBJ *) __attribute__((weak));                                       \
    void tmce_##m##_Event_Update(void) __attribute__((weak));                                       \
    void tmce_##m##_Event_Think(GOBJ *) __attribute__((weak));                                      \
    extern EventMenu *tmce_##m##_Event_Menu;
EV_DECL(lab)
EV_DECL(lcancel)
EV_DECL(ledgedash)
EV_DECL(wavedash)
EV_DECL(powershield)
EV_DECL(dthrowknee)
EV_DECL(edgeguard)
EV_DECL(fc)
EV_DECL(sweetspot)
EV_DECL(laserland)
EV_DECL(eggs)
EV_DECL(techchase)
EV_DECL(slalom)

typedef struct EventModule {
    const char *file;
    evFunction functions;
} EventModule;

#define EV_ROW(m) { "TM/" #m ".dat", { tmce_##m##_Event_Init, tmce_##m##_Event_Update, tmce_##m##_Event_Think, &tmce_##m##_Event_Menu } }
static const EventModule event_modules[] = {
    EV_ROW(lab), EV_ROW(lcancel), EV_ROW(ledgedash), EV_ROW(wavedash), EV_ROW(powershield),
    EV_ROW(dthrowknee), EV_ROW(edgeguard), EV_ROW(fc), EV_ROW(sweetspot), EV_ROW(laserland),
    EV_ROW(eggs), EV_ROW(techchase), EV_ROW(slalom),
};

void tmce_labCSS_OnCSSLoad(HSD_Archive *archive);

int mu_tmce_dvd_entrynum(const char *path) __asm__("DVDConvertPathToEntrynum");

static int same_file(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
        char y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y)
            return 0;
        if (x == 0)
            return 1;
    }
}

/* m-ex's MEX_LoadRelArchive: load the module's file and fill `functions` with its entry points.
 * A module with no data of its own may have no file on the disc (a newer TM-CE than the player's
 * disc): its code is here all the same, and its archive is NULL. */
HSD_Archive *MEX_LoadRelArchive(char *file, void *functions, char *symbol)
{
    HSD_Archive *archive = 0;
    if (mu_tmce_dvd_entrynum(file) >= 0)
        archive = Archive_LoadFile(file);
    if (__builtin_strcmp(symbol, "cssFunction") == 0) {
        *(void **) functions = (void *) tmce_labCSS_OnCSSLoad;
        return archive;
    }
    for (unsigned i = 0; i < sizeof event_modules / sizeof event_modules[0]; i++) {
        if (same_file(event_modules[i].file, file)) {
            __builtin_memcpy(functions, &event_modules[i].functions, sizeof(evFunction));
            return archive;
        }
    }
    OSReport("[tmce] no native module for %s\n", file);
    __builtin_memset(functions, 0, sizeof(evFunction));
    return archive;
}

/* TM-CE's current event page, kept in a spare byte of the save file (MexTK TM_EventPage, twin-mapped) */
int mu_tmce_event_page(void) { return stc_memcard->TM_EventPage; }
void mu_tmce_set_event_page(int page) { stc_memcard->TM_EventPage = (u8) page; }
