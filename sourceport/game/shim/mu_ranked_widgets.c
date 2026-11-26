/* GPL-3.0-or-later. Behaviour/layout derived from slippi-ssbm-c d7174ca:
 * Scenes/Ranked/GameSetup.c:377-581,630-783,1200-1367 and its Components directory.
 * Art is loaded solely from the player's disc-rebuilt GameSetup_gui.dat.
 * Portable helpers below deliberately use only A1's public model contract. */
#include "mu_ranked_widgets.h"
#include <string.h>
#include <stdio.h>

unsigned mu_ranked_stage_material(unsigned stage)
{
    /* Components/CharStageIcon.c:249-265; external stage ids. */
    switch (stage) {
    case 2: return 6; case 3: return 7; case 8: return 3;
    case 28: return 4; case 31: return 2; case 32: return 5;
    default: return 1; /* Unknown remix stage: question, never wrong stage art. */
    }
}
unsigned mu_ranked_character_material(unsigned character)
{
    return character < 26 ? character+8 : 1;
}
unsigned mu_ranked_stock_frame(unsigned character, unsigned color)
{
    /* Components/StockIcon.c:11-22, external Sheik is 19. */
    if (character >= 26) return 0;
    return (character == 19 ? 29 : character > 19 ? character-1 : character) + 30*color;
}
unsigned mu_ranked_pad_buttons(unsigned long long raw)
{
    /* dolphin/pad.h:16-39: low d-pad bits, main-stick bits 16..19.
     * Bits 32..35 are synthesized CONFIRM/CANCEL, not directions. */
    unsigned result=0;
    if (raw & (0x40000|0x1)) result |= MU_SETUP_LEFT;
    if (raw & (0x80000|0x2)) result |= MU_SETUP_RIGHT;
    if (raw & (0x20000|0x4)) result |= MU_SETUP_DOWN;
    if (raw & (0x10000|0x8)) result |= MU_SETUP_UP;
    if (raw & 0x100) result |= MU_SETUP_A;
    if (raw & 0x200) result |= MU_SETUP_B;
    if (raw & 0x400) result |= MU_SETUP_X;
    if (raw & 0x800) result |= MU_SETUP_Y;
    if (raw & 0x1000) result |= MU_SETUP_START;
    return result;
}
int mu_ranked_disconnect_wait(unsigned* frames, unsigned down)
{
    /* GameSetup.c:614-627: ignore the first 30 frames, then A/Start returns. */
    if (*frames < 30) { ++*frames; return 1; }
    return !(down & (MU_SETUP_A|MU_SETUP_START));
}
float mu_ranked_selector_x(unsigned index, unsigned count)
{
    /* GameSetup.c:483-506: 8 world units between selector centers. */
    return 8.0f*((float)index - ((float)count-1)/2);
}
float mu_ranked_timeline_x(const MuRankedSetupModel* m, unsigned step, unsigned item)
{
    float x = -26;
    unsigned i;
    if (!m || step >= m->step_count) return x;
    for (i=0; i<step; ++i) x += 10 + (m->steps[i].count == 2 ? 5.2f : 0);
    return x + (m->steps[step].count == 2 && item ? 5.2f : 0);
}
int mu_ranked_selector_move(unsigned enabled, unsigned count, int current, int direction)
{
    unsigned i;
    int c = current;
    if (!count || count > 6) return -1;
    for (i=0; i<count; ++i) {
        c = (c + (direction < 0 ? -1 : 1) + (int)count) % (int)count;
        if (enabled & (1u<<c)) return c;
    }
    return -1;
}
unsigned mu_ranked_picker_move(unsigned c, unsigned direction)
{
    /* CharPickerDialog.c:65-123: 7 columns; last row has 6 slots.
     * Correct the original UP path's possible index 27 into the short row. */
    unsigned row, col, width;
    if (c > 26) c = 0;
    row = c/7; col = c%7; width = row == 3 ? 6 : 7;
    if (direction == MU_SETUP_RIGHT) return row*7+(col+1)%width;
    if (direction == MU_SETUP_LEFT) return row*7+(col+width-1)%width;
    if (direction == MU_SETUP_DOWN) row = (row+1)%4;
    else if (direction == MU_SETUP_UP) row = (row+3)%4;
    if (row == 3 && col == 6) row = direction == MU_SETUP_DOWN ? 0 : 2;
    return row*7+col;
}
unsigned mu_ranked_timer_text(char out[8], unsigned seconds, unsigned frames, int local)
{
    unsigned elapsed = frames/60;
    unsigned left = elapsed >= seconds ? 0 : seconds-elapsed;
    if (left > 5999) left = 5999;
    sprintf(out, "%u:%02u", left/60, left%60);
    return !local || left > 10 ? MU_SETUP_TIMER_WHITE :
           left > 3 ? MU_SETUP_TIMER_YELLOW : MU_SETUP_TIMER_RED;
}
static unsigned stage_mask(const MuRankedSetupModel* m)
{
    unsigned i, bits=0, n=m->set->cur_game == 1 ? 5 : 6;
    const MuYgU16* pool=n==5 ? m->policy.starters : m->policy.counterpicks;
    for (i=0; i<n; ++i) if (mu_ranked_setup_stage_allowed(m, pool[i])) bits |= 1u<<i;
    return bits;
}
void mu_ranked_view_sync(MuRankedSetupView* v, const MuRankedSetupModel* m)
{
    const MuRankedSetupStep* s;
    unsigned i, j, removed=0;
    if (!v || !m || !m->set) return;
    /* Capture completed remote, timeout AND manual choices from A1. Never infer
     * a remote character until its step completed (the waiting icon stays '?'). */
    for (i=0; i<m->step_count; ++i) {
        const MuRankedSetupStep* h=&m->steps[i];
        MuRankedSetupChoice* out=&v->history[i];
        if (i < m->step && !v->history_valid[i]) {
            memset(out, 0, sizeof *out);
            if (h->type == MU_SETUP_STRIKE) {
                for (j=0; j<h->count && removed+j<m->removed_count; ++j)
                    out->stages[out->count++]=m->removed[removed+j];
            } else if (h->type == MU_SETUP_STAGE) {
                out->stages[0]=m->result.stage; out->count=1;
            } else {
                out->character=m->result.fighters[h->player].character;
                out->color=m->result.fighters[h->player].color;
            }
            v->history_valid[i]=1;
        }
        if (h->type == MU_SETUP_STRIKE) removed += h->count;
    }
    if (v->initialized && v->step == m->step && m->status == MU_SETUP_ACTIVE) return;
    v->initialized=1; v->step=m->step; v->dialog=v->button=v->confirming=0;
    v->cursor=-1;
    memset(&v->draft, 0, sizeof v->draft);
    s=mu_ranked_setup_current(m);
    if (!s) return;
    v->draft.character=m->result.fighters[m->local_player].character;
    v->draft.color=m->result.fighters[m->local_player].color;
    if (s->type == MU_SETUP_STRIKE || s->type == MU_SETUP_STAGE)
        v->cursor=mu_ranked_selector_move(stage_mask(m), m->set->cur_game == 1 ? 5 : 6, -1, 1);
    else if (s->type == MU_SETUP_CHARACTER) {
        v->draft.color=mu_ranked_setup_next_color(m, m->local_player, v->draft.character, v->draft.color, 0);
        v->confirming=1;
    }
}
int mu_ranked_view_input(MuRankedSetupView* v, const MuRankedSetupModel* m, unsigned down, unsigned repeat)
{
    const MuRankedSetupStep* s=mu_ranked_setup_current(m);
    unsigned direction = repeat & MU_SETUP_RIGHT ? MU_SETUP_RIGHT :
        repeat & MU_SETUP_LEFT ? MU_SETUP_LEFT : repeat & MU_SETUP_DOWN ? MU_SETUP_DOWN :
        repeat & MU_SETUP_UP ? MU_SETUP_UP : 0;
    int char_step;
    if (!s || s->player != m->local_player || v->step != m->step) return 0;
    char_step=s->type == MU_SETUP_CHARACTER || s->type == MU_SETUP_COLOR;
    if (v->dialog) {
        if (down & MU_SETUP_B) { v->draft=v->saved; v->dialog=v->button=0; v->confirming=1; return 0; }
        if (down & MU_SETUP_A) {
            if (v->picker == 26) {
                /* Cosmetic random selection without consuming simulation RNG. */
                v->draft.character=(MuYgU8)(m->frames%26);
                v->draft.color=mu_ranked_setup_next_color(m,m->local_player,v->draft.character,0,0);
            }
            v->dialog=v->button=0; v->confirming=1; return 0;
        }
        if (direction) {
            v->picker=mu_ranked_picker_move(v->picker,direction);
            /* Keep the timeout draft legal while hovering Random. */
            if (v->picker<26) {
                v->draft.character=v->picker;
                v->draft.color=mu_ranked_setup_next_color(m,m->local_player,v->draft.character,0,0);
            }
        }
    } else if (char_step) {
        if (s->type == MU_SETUP_CHARACTER && ((down & MU_SETUP_B) ||
            ((down & MU_SETUP_A) && v->button == 1))) {
            v->saved=v->draft; v->picker=v->draft.character; v->dialog=1; return 0;
        }
        if (v->confirming && (down & MU_SETUP_A) && v->button == 0) return 1;
        if (direction == MU_SETUP_LEFT || direction == MU_SETUP_RIGHT)
            v->button=s->type == MU_SETUP_COLOR ? 0 : !v->button;
    } else if (v->confirming) {
        if ((down & MU_SETUP_B) || ((down & MU_SETUP_A) && v->button == 1)) {
            v->draft.count=0; v->confirming=v->button=0; return 0;
        }
        if ((down & MU_SETUP_A) && !v->button) return 1;
        if (direction == MU_SETUP_LEFT || direction == MU_SETUP_RIGHT) v->button=!v->button;
        return 0;
    } else {
        unsigned n=m->set->cur_game == 1 ? 5 : 6, i;
        const MuYgU16* pool=n==5 ? m->policy.starters : m->policy.counterpicks;
        /* Buttons precede scroll so A cannot accidentally choose a new hover. */
        if (down & MU_SETUP_B) v->draft.count=0;
        else if ((down & MU_SETUP_A) && v->cursor >= 0 &&
                 mu_ranked_setup_stage_allowed(m,pool[v->cursor])) {
            MuYgU16 stage=pool[v->cursor];
            for (i=0; i<v->draft.count; ++i) if (v->draft.stages[i] == stage) break;
            if (i<v->draft.count) {
                for (; i+1<v->draft.count; ++i) v->draft.stages[i]=v->draft.stages[i+1];
                --v->draft.count;
            } else if (v->draft.count<2) v->draft.stages[v->draft.count++]=stage;
            if (v->draft.count == s->count) v->confirming=1;
        } else if (direction == MU_SETUP_RIGHT || direction == MU_SETUP_LEFT)
            v->cursor=mu_ranked_selector_move(stage_mask(m),n,v->cursor,direction == MU_SETUP_LEFT ? -1 : 1);
        return 0;
    }
    if ((down & (MU_SETUP_X|MU_SETUP_Y)) && v->draft.character < 26) {
        v->draft.color=mu_ranked_setup_next_color(m,m->local_player,v->draft.character,
            v->draft.color,down & MU_SETUP_X ? 1 : -1);
        if (!v->dialog) v->confirming=1;
    }
    return 0;
}

#ifndef MU_RANKED_WIDGETS_TEST
#ifndef MU_NO_SLIPPI
#include <mu_native.h>
#include <mu_rank_badge.h>
#include "mu_rank_text.h"
#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gmtoulib.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/displayfunc.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/sislib.h>

/* Files.h:6-30. SceneDesc has the same four disc slots, and preserves 32-bit
 * pointers on a 64-bit host. Always DP through the archive; never cast to a
 * host-pointer table or overwrite a disc pointer with a native pointer. */
enum { BG, PANELS, BUTTON, SELECTOR, ICON, CROSS, FLAT, ARROW, CHECK,
       DIALOG, STOCK, TURN, RESULT };
enum { EMPTY, YOUR_CHAR, OPP_CHAR, STRIKE1, STRIKE23, STRIKE4, BAN, STAGE,
       SELECT_STRIKE, SELECT_TWO, WAIT_STRIKE, SELECT_BAN, SELECT_STAGE,
       WAIT_BAN, WAIT_STAGE, SELECT_CHAR, WAIT_CHAR, GET_READY,
       SELECT_COLOR, WAIT_COLOR, RETURN_CSS, DISCONNECTED };
typedef struct Part {
    HSD_GObj* gobj;
    HSD_JObj* root;
    DynamicModelDesc* set;
    int material, state, visible;
    unsigned kind;
} Part;
typedef struct Icon { Part image, stock; } Icon;
typedef struct Selector { Part box, cross, check; Icon icon; } Selector;
struct MuRankedWidgets {
    HSD_Archive* archive; /* scene heap owns the archive buffer */
    SceneDesc* gui;
    HSD_GObj* objects[192];
    unsigned object_count;
    Part background, panels, buttons[2], description, disconnect, turn[2], pips[9];
    Selector selectors[6], character;
    Icon timeline[5][2], picker[27];
    Part labels[5], arrows[5], dialog;
    MuRankBadge* badges[2];
    HSD_Text *text, *names[2];
    unsigned missing, canvas, ranks_frames, forfeit_frames;
    int forfeit;
    /* The player's own keys for A, B and X/Y: a keyboard port's bound keys, else the GameCube names. */
    char key_a[16], key_b[16], key_xy[40];
    int keyboard_xy;   /* X/Y are keyboard keys: the disc's "Press X/Y to change" art names the wrong ones */
};

static HSD_GObj* track(MuRankedWidgets* w, HSD_GObj* obj)
{
    if (obj && w->object_count < sizeof(w->objects)/sizeof(w->objects[0]))
        w->objects[w->object_count++]=obj;
    return obj;
}
static void camera_draw(HSD_GObj* obj, intptr_t unused)
{
    HSD_CObj* camera=obj->hsd_obj;
    (void)unused;
    if (!HSD_CObjSetCurrent(camera)) return;
    HSD_SetEraseColor(0,0,0,255);
    HSD_CObjEraseScreen(camera,1,0,1);
    HSD_GObj_80390ED0(obj,7);
    HSD_CObjEndCurrent();
}
#include "mu_ranked_widgets_motion.h"
static void part_init(MuRankedWidgets* w, Part* p, unsigned model, unsigned layer)
{
    p->material=p->state=p->visible=-1;
    p->kind=model;
    if (!w->gui || !DP(w->gui->models)) return;
    p->set=DP(DP(w->gui->models)[model]);
    if (!p->set || !DP(p->set->joint)) return;
    p->gobj=track(w,fn_8019035C(0,p->set,0,3,layer,1,animate,0));
    p->root=p->gobj ? p->gobj->hsd_obj : NULL;
}
/* A composite child has exactly one owner, its parent JObj. It has no separate
 * GObj to draw/free it a second time. Animation runs with the parent tree. */
static void part_attach(MuRankedWidgets* w, Part* p, HSD_JObj* parent, unsigned model)
{
    p->material=p->state=p->visible=-1;
    p->kind=model;
    if (!parent || !w->gui || !DP(w->gui->models)) return;
    p->set=DP(DP(w->gui->models)[model]);
    if (!p->set || !DP(p->set->joint)) return;
    p->root=HSD_JObjLoadJoint(DP(p->set->joint));
    HSD_JObjAddChild(parent,p->root);
}
static void position(Part* p, float x, float y)
{
    if (!p->root) return;
    p->root->translate=(Vec3){x,y,0}; HSD_JObjSetMtxDirtySub(p->root);
}
static void visibility(Part* p, int visible)
{
    if (!p->root) return;
    /* Some archive roots have no DObj. Set the whole subtree, as Slippi's
     * component visibility functions do for the drawable children. */
    if (visible) HSD_JObjClearFlagsAll(p->root,JOBJ_HIDDEN);
    else HSD_JObjSetFlagsAll(p->root,JOBJ_HIDDEN);
    p->visible=visible;
}
static void material(Part* p, int animation, unsigned frame)
{
    if (!p->root || p->material == (int)frame) return;
    HSD_JObjRemoveAnimAll(p->root);
    gm_8016895C(p->root,p->set,animation);
    HSD_JObjReqAnimAll(p->root,(float)frame); HSD_JObjAnimAll(p->root);
    HSD_JObjRemoveAnimAll(p->root);
    p->material=frame; p->state=-1;
}
static void tint(HSD_JObj* root, float alpha, GXColor color)
{
    HSD_JObj* child;
    HSD_DObj* d;
    if (!root) return;
    for (d=root->flags & (JOBJ_PTCL|JOBJ_SPLINE) ? NULL : root->u.dobj; d; d=d->next) if (d->mobj && d->mobj->mat) {
        d->mobj->mat->alpha=alpha; d->mobj->mat->diffuse=color;
    }
    for (child=root->child; child; child=child->next) tint(child,alpha,color);
}
static void flat_size(Part* p, float width, float height)
{
    HSD_JObj* j=p->root ? p->root->child : NULL;
    const Vec3 corners[4]={{width/2,height/2,0},{-width/2,-height/2,0},
                           {-width/2,height/2,0},{width/2,-height/2,0}};
    unsigned i;
    for (i=0; i<4 && j; ++i,j=j->next) { j->translate=corners[i]; HSD_JObjSetMtxDirtySub(j); }
}
static void icon_init(MuRankedWidgets* w, Icon* i, unsigned layer)
{
    HSD_JObj* anchor;
    part_init(w,&i->image,ICON,layer);
    anchor=i->image.root && i->image.root->child ? i->image.root->child->next : NULL;
    part_attach(w,&i->stock,anchor,STOCK);
}
static void icon_draw(Icon* i, float x, float y, unsigned mat, unsigned c,
                      unsigned color, int stock, int visible, unsigned display)
{
    HSD_JObj* stock_bg;
    position(&i->image,x,y);
    material(&i->image,1,mat); material(&i->stock,0,mu_ranked_stock_frame(c,color));
    state(&i->image,display,display == 4 ? 2 : 0);
    visibility(&i->image,visible); visibility(&i->stock,visible && stock);
    /* CharStageIcon.c:82-89: the stock border is in a separate sibling tree. */
    stock_bg=i->image.root && i->image.root->child && i->image.root->child->next ?
        i->image.root->child->next->child : NULL;
    if (stock_bg) {
        if (visible && stock) HSD_JObjClearFlagsAll(stock_bg,JOBJ_HIDDEN);
        else HSD_JObjSetFlagsAll(stock_bg,JOBJ_HIDDEN);
    }
}
static void selector_init(MuRankedWidgets* w, Selector* s)
{
    part_init(w,&s->box,SELECTOR,1); icon_init(w,&s->icon,1);
    part_init(w,&s->cross,CROSS,1); part_init(w,&s->check,CHECK,1);
}
static void selector_draw(Selector* s, float x, float y, unsigned mat, unsigned c,
                          unsigned color, int stock, int visible, int hover,
                          int selected, int strike, int disabled)
{
    HSD_JObj* glow;
    position(&s->box,x,y); position(&s->cross,x,y); position(&s->check,x,y);
    visibility(&s->box,visible);
    glow=s->box.root ? s->box.root->child : NULL;
    if (glow) {
        if (visible && hover) HSD_JObjClearFlagsAll(glow,JOBJ_HIDDEN);
        else HSD_JObjSetFlagsAll(glow,JOBJ_HIDDEN);
        if (glow->next) {
            if (visible && !disabled) HSD_JObjClearFlagsAll(glow->next,JOBJ_HIDDEN);
            else HSD_JObjSetFlagsAll(glow->next,JOBJ_HIDDEN);
        }
    }
    icon_draw(&s->icon,x,y,mat,c,color,stock,visible,disabled ? 2 : stock && hover ? 4 : 0);
    visibility(&s->cross,visible && (disabled || (strike && selected)));
    visibility(&s->check,visible && selected && !strike);
    if (s->cross.root) tint(s->cross.root,1,disabled ? (GXColor){135,135,135,255} : (GXColor){255,0,0,255});
}
void mu_rank_badge_art_only(MuRankBadge*);   /* mu_rank_badge.c: the art without the numeral */
static HSD_Text* text_create(MuRankedWidgets* w, float x, float y, float scale)
{
    /* Editable text (803A6754): the static constructor (803A5ACC) leaves alloc_data NULL, and the
     * subtexts below wrote through it (10-31, Ranked CSS Start -> setup crash). */
    HSD_Text* t;
    if (!HSD_SisLib_804D1124[0]) {
        OSReport("Ranked setup text_create failed: SIS slot 0 missing\n");
        return NULL;
    }
    t=HSD_SisLib_803A6754(0,(int)w->canvas);
    if (!t) {
        OSReport("Ranked setup text_create failed: editable allocation\n");
        return NULL;
    }
    if (!t->alloc_data || !t->alloc_data->data || !t->alloc_data->end || !t->sis_buffer || !t->entity ||
        !t->entity->user_data_remove_func) {
        OSReport("Ranked setup text_create failed: editable storage/entity\n");
        HSD_SisLib_803A5CC4(t);
        return NULL;
    }
    t->pos_x=x; t->pos_y=y; t->pos_z=0; t->box_size_x=t->box_size_y=20;
    t->default_kerning=1; t->x34.y=scale;
    /* True widescreen stretches this flat layer: glyphs keep their shape (header_text). */
    { float mu_picture_widen(void); t->x34.x=scale/mu_picture_widen(); }
    HSD_SisLib_803A6B98(t,0,0,"%s","");
    return t;
}
static void text_set(HSD_Text* t, unsigned sub, const char* value)
{
    if (t) HSD_SisLib_803A70A0(t,sub,"%s",value);
}
/* This scene owns a pixel-space canvas, unlike CSS/HUD world-space text. True widescreen widens the
 * perspective scene (the timer bar) but not this flat layer, so a pixel x is brought toward the centre
 * by the picture's widening (above 1 in True widescreen: 16:9 over 4:3): the names and badges stay over the bar, not at the
 * screen edges. 1 in 4:3 and with Slippi's widescreen code. */
float mu_picture_widen(void);
static float widen_x(float x) { return 320.f + (x - 320.f) / mu_picture_widen(); }
static void header_text(MuRankedWidgets* w, const MuRankedSetupModel* m, const MuMatchState* ms)
{
    unsigned i;
    HSD_SisLib_803A5E70();
    /* CSS has exited; its font/data slot may have been unloaded. Use the same
     * SIS data as the working VS splash before text_create checks slot 0. */
    if (!HSD_SisLib_804D1124[0])
        HSD_SisLib_803A62A0(0,"SdSlChr.usd","SIS_SelCharData");
    w->canvas=HSD_SisLib_803A611C(0,NULL,9,18,0,3,0,19);
    /* The timer (line 0) centred on the turn arrows (y 57: captured 8 below them at y 45, 11-25); the
     * other lines keep their pixel places through T (their offsets were measured from y 45). */
#define T 8.0f
    w->text=text_create(w,320,45-T,.7f);
    if (w->text) {
        w->text->default_alignment=1;
        HSD_SisLib_803A6B98(w->text,0,130+T,"%s","");
        HSD_SisLib_803A6B98(w->text,0,190+T,"%s","");
        HSD_SisLib_803A6B98(w->text,0,240+T,"%s","");
        HSD_SisLib_803A6B98(w->text,0,295+T,"%s","");
        for (i=1; i<5; ++i) HSD_SisLib_803A7548(w->text,i,.5f/mu_picture_widen(),.5f);
        /* Pips are at pixel y 25.5, turn arrows at y 61; score sits below. */
        HSD_SisLib_803A6B98(w->text,0,92.0f-45.0f+T,"%s","");
        HSD_SisLib_803A7548(w->text,5,.45f/mu_picture_widen(),.45f);
        /* 6-8: the winner's lines in DISCONNECTED's place when the opponent left (CTO 11-01).
         * Glyph tops at about y 165, 195 and 218, above "Press A" at about y 262 (build 31). */
        HSD_SisLib_803A6B98(w->text,0,101+T,"%s","");
        HSD_SisLib_803A6B98(w->text,0,135+T,"%s","");
        HSD_SisLib_803A6B98(w->text,0,158+T,"%s","");
        HSD_SisLib_803A7548(w->text,6,.55f/mu_picture_widen(),.55f);
        for (i=7; i<9; ++i) HSD_SisLib_803A7548(w->text,i,.45f/mu_picture_widen(),.45f);
    }
#undef T
    for (i=0; i<2; ++i) {
        /* Slippi GameSetup.c InitPlayerInfo: the name at scale 5 and the code 2 units under it at scale 3,
         * gray, both 3.55 units in from x 28 to leave room for the rank icon (10 px a unit here). */
        w->names[i]=text_create(w,widen_x(i ? 564.5f : 75.5f),39,.7f*5.f/6.f);
        if (w->names[i]) {
            unsigned player=i ? !m->local_player : m->local_player;
            char name[31], code[10];
            memcpy(name,ms->names[player],30); name[30]=0;
            memcpy(code,ms->codes[player],9); code[9]=0;
            w->names[i]->default_alignment=i ? 2 : 0;
            text_set(w->names[i],0,name);
            {
                const GXColor code_gray={128,128,128,255};
                const int sub=HSD_SisLib_803A6B98(w->names[i],0,20.f/(.7f*5.f/6.f),"%s",code);
                HSD_SisLib_803A7548(w->names[i],sub,.6f/mu_picture_widen(),.6f);
                HSD_SisLib_803A74F0(w->names[i],sub,(GXColor*)&code_gray);
            }
        }
    }
}
static void header_score(MuRankedWidgets* w, const MuRankedSetupModel* m)
{
    char line[64];
    if (!m->set) return;
    /* Practice (agent runs only) says so on this row: a fifth row under the stage panel
     * touched the strip's top border (build 29). */
    sprintf(line,"%sGame %u - %u:%u",mu_ranked_practice() ? "Practice  " : "",(unsigned)m->set->cur_game,
        (unsigned)m->set->score[m->local_player],(unsigned)m->set->score[!m->local_player]);
    text_set(w->text,5,line);
}
static void header_ranks(MuRankedWidgets* w, const MuRankedSetupModel* m)
{
    MuYgRanks ranks;
    const MuYgRank* r[2];
    unsigned seat;
    memset(&ranks,0,sizeof ranks);
    mu_ranked_read_ranks(&ranks);
    r[m->local_player]=&ranks.self; r[!m->local_player]=&ranks.opponent;
    for (seat=0; seat<2; ++seat) {
        unsigned side=seat == m->local_player ? 0 : 1;
        /* Slippi's rank icon spot (x +-27.5, y 17.85 world: pixel 320 -+ 275, 54.5), art only as its icons. */
        if (!w->badges[seat]) {
            w->badges[seat]=mu_rank_badge_create(NULL,widen_x(side ? 595.f : 45.f),54.5f,1.4f,r[seat]);
            mu_rank_badge_art_only(w->badges[seat]);
        } else mu_rank_badge_set(w->badges[seat],r[seat]);
    }
}
/* The room gave this player the set because the opponent left (CTO 11-01): its lines replace
 * DISCONNECTED. The result arrives after the leave, so poll at 4 Hz until it does; it is final. */
static int forfeit_lines(MuRankedWidgets* w, const MuRankedSetupModel* m)
{
    MuYgRanks ranks;
    char headline[64], standing[80], rating[48];
    unsigned rgb;
    GXColor white={255,255,255,255}, tier, chip={0x86,0xef,0xac,0xff};
    if (w->forfeit || !w->text || !m->set || m->local_player>1) return w->forfeit;
    if (w->forfeit_frames++ % 15) return 0;
    memset(&ranks,0,sizeof ranks);
    if (!mu_ranked_read_ranks(&ranks) || !mu_rank_text_forfeit(headline,standing,rating,&ranks.result,
            m->set->score[m->local_player],m->set->score[!m->local_player])) return 0;
    rgb=mu_rank_text_rgb(ranks.result.rank_after.tier);
    tier=(GXColor){rgb >> 16,rgb >> 8,rgb,255};
    if (ranks.result.after <= ranks.result.before) chip=white;
    text_set(w->text,6,headline); HSD_SisLib_803A74F0(w->text,6,&white);
    text_set(w->text,7,standing); HSD_SisLib_803A74F0(w->text,7,&tier);
    text_set(w->text,8,rating); HSD_SisLib_803A74F0(w->text,8,&chip);
    return w->forfeit=1;
}
MuRankedWidgets* mu_ranked_widgets_create(const MuRankedSetupModel* m)
{
    MuRankedWidgets* w=HSD_MemAlloc(sizeof *w);
    HSD_GObj* camera;
    unsigned i,j;
    const MuMatchState* ms=mu_slippi_match_state();
    if (!w) return NULL;
    memset(w,0,sizeof *w);
    strcpy(w->key_a,"A"); strcpy(w->key_b,"B"); strcpy(w->key_xy,"X/Y");
    if (DVDConvertPathToEntrynum("GameSetup_gui.dat") >= 0) {
        w->archive=lbArchive_LoadArchive("GameSetup_gui.dat");
        if (w->archive) w->gui=HSD_ArchiveGetPublicAddress(w->archive,"ScGamTour_scene_data");
    }
    /* Camera, lights and fog: GameSetup.c:60-94. The native SceneDesc types
     * include the animation slots that the original treats as pointer arrays. */
    if (w->gui && DP(w->gui->cameras) && DP(DP(w->gui->cameras)[0].desc)) {
        HSD_CObj* c=HSD_CObjLoadDesc(DP(DP(w->gui->cameras)[0].desc));
        camera=track(w,GObj_Create(2,3,128));
        HSD_GObjObject_80390A70(camera,HSD_GObj_CameraKind,c);
        GObj_SetupGXLinkMax(camera,camera_draw,0); camera->gxlink_prios=7;
        if (DP(w->gui->lights)) {
            HSD_GObj* light=track(w,GObj_Create(3,4,128));
            HSD_GObjObject_80390A70(light,HSD_GObj_LightKind,lb_80011AC4(DP(w->gui->lights)));
            GObj_SetupGXLink(light,HSD_GObj_LObjCallback,0,128);
        }
        if (DP(w->gui->fogs) && DP(DP(w->gui->fogs)[0].desc)) {
            HSD_GObj* fog=track(w,GObj_Create(14,2,0));
            HSD_GObjObject_80390A70(fog,HSD_GObj_FogKind,HSD_FogLoadDesc(DP(DP(w->gui->fogs)[0].desc)));
            GObj_SetupGXLink(fog,HSD_GObj_FogCallback,0,128);
        }
    } else {
        /* Missing disc assets must leave a readable, operable screen. */
        w->missing=1; w->gui=NULL;
        {
            HSD_CObj* c=HSD_CObjAlloc();
            Vec3 eye={0,0,100}, interest={0,0,0};
            HSD_CObjSetEyePosition(c,&eye); HSD_CObjSetInterest(c,&interest);
            HSD_CObjSetViewportfx4(c,0,640,0,480); HSD_CObjSetScissorx4(c,0,640,0,480);
            HSD_CObjSetNear(c,.1f); HSD_CObjSetFar(c,1000);
            HSD_CObjSetOrtho(c,24,-24,-32,32);
            camera=track(w,GObj_Create(2,3,128));
            HSD_GObjObject_80390A70(camera,HSD_GObj_CameraKind,c);
            GObj_SetupGXLinkMax(camera,camera_draw,0); camera->gxlink_prios=7;
        }
    }
    header_text(w,m,ms);
    part_init(w,&w->background,BG,1); part_init(w,&w->panels,PANELS,1);
    for (i=0; i<2; ++i) {
        part_init(w,&w->buttons[i],BUTTON,1); material(&w->buttons[i],1,i);
        position(&w->buttons[i],i ? 4.8f : -4.8f,-3);
        part_init(w,&w->turn[i],TURN,1); position(&w->turn[i],i ? 6 : -6,17.9f);
        if (w->turn[i].root && !i) w->turn[i].root->rotate.z=3.14159265f;
    }
    part_init(w,&w->description,FLAT,1); flat_size(&w->description,45,1.8f);
    position(&w->description,0,-3);
    part_init(w,&w->disconnect,FLAT,1); flat_size(&w->disconnect,35,4);
    position(&w->disconnect,0,3); material(&w->disconnect,0,DISCONNECTED);
    if (m->set) for (i=0; i<m->set->max_games && i<9; ++i) {
        part_init(w,&w->pips[i],RESULT,1);
        position(&w->pips[i],2.2f*((float)i-(float)(m->set->max_games/2)),21.45f);
        material(&w->pips[i],0,i+1<m->set->cur_game ?
            (m->set->game_results[i] == m->local_player ? 1 : 2) : 0);
    }
    for (i=0; i<6; ++i) selector_init(w,&w->selectors[i]);
    selector_init(w,&w->character);
    for (i=0; i<5; ++i) {
        for (j=0; j<2; ++j) icon_init(w,&w->timeline[i][j],1);
        part_init(w,&w->labels[i],FLAT,1); flat_size(&w->labels[i],12,1.2f);
        part_init(w,&w->arrows[i],ARROW,1);
    }
    /* CharPickerDialog.c:148-165: all 27 archive anchors, including Random.
     * Dialog owns the icon roots, which in turn own their stock roots. */
    part_init(w,&w->dialog,DIALOG,2);
    {
        HSD_JObj* anchor=w->dialog.root && w->dialog.root->child && w->dialog.root->child->child &&
            w->dialog.root->child->child->next ? w->dialog.root->child->child->next->child : NULL;
        for (i=0; i<27; ++i) {
            HSD_JObj* stock_anchor;
            part_attach(w,&w->picker[i].image,anchor,ICON);
            stock_anchor=w->picker[i].image.root && w->picker[i].image.root->child ?
                w->picker[i].image.root->child->next : NULL;
            part_attach(w,&w->picker[i].stock,stock_anchor,STOCK);
            if (anchor) anchor=anchor->next;
        }
    }
    if (m->set && m->local_player<2) header_ranks(w,m);
    return w;
}
void mu_ranked_widgets_keys(MuRankedWidgets* w, int port)
{
    /* dolphin/pad.h bits, as mu_ranked_pad_buttons reads them: A 0x100, B 0x200, X 0x400, Y 0x800. */
    char x[16]="X", y[16]="Y";
    int kx, ky;
    if (!w) return;
    strcpy(w->key_a,"A"); strcpy(w->key_b,"B");
    mu_host_button_label(port,0x100,w->key_a,sizeof w->key_a);
    mu_host_button_label(port,0x200,w->key_b,sizeof w->key_b);
    kx=mu_host_button_label(port,0x400,x,sizeof x);
    ky=mu_host_button_label(port,0x800,y,sizeof y);
    w->keyboard_xy=kx || ky;   /* a controller keeps "X/Y" and the disc's own art */
    sprintf(w->key_xy,"%s/%s",x,y);   /* each name under 16 chars: fits 40 */
}
static unsigned description(const MuRankedSetupModel* m, const MuRankedSetupStep* s)
{
    int local=s->player == m->local_player;
    if (s->type == MU_SETUP_COLOR) return local ? SELECT_COLOR : WAIT_COLOR;
    if (s->type == MU_SETUP_CHARACTER) return local ? SELECT_CHAR : WAIT_CHAR;
    if (s->type == MU_SETUP_STAGE) return local ? SELECT_STAGE : WAIT_STAGE;
    if (m->set->cur_game>1) return local ? SELECT_BAN : WAIT_BAN;
    return !local ? WAIT_STRIKE : s->count == 2 ? SELECT_TWO : SELECT_STRIKE;
}
void mu_ranked_widgets_draw(MuRankedWidgets* w, const MuRankedSetupModel* m, const MuRankedSetupView* v)
{
    const MuRankedSetupStep* s=mu_ranked_setup_current(m);
    unsigned i,j,n=m->set && m->set->cur_game == 1 ? 5 : 6;
    const MuYgU16* pool=n==5 ? m->policy.starters : m->policy.counterpicks;
    int local=s && s->player == m->local_player;
    int char_step=s && (s->type == MU_SETUP_COLOR || s->type == MU_SETUP_CHARACTER);
    int active=s != NULL, disconnected=m->status == MU_SETUP_DISCONNECTED || m->status == MU_SETUP_INVALID;
    int forfeit, color_hint;
    char timer[8], line[256];
    unsigned col=mu_ranked_timer_text(timer,s ? s->seconds : 0,m->frames,local);
    GXColor colors[3]={{255,255,255,255},{254,202,52,255},{255,50,50,255}};
    if (!w) return;
    forfeit=m->status == MU_SETUP_DISCONNECTED && forfeit_lines(w,m);
    header_score(w,m);
    if (forfeit) text_set(w->text,5,""); /* the headline has the games; not "Game 2 - 1:0" */
    text_set(w->text,0,active ? timer : "");
    if (w->text) HSD_SisLib_803A74F0(w->text,0,&colors[col]);
    visibility(&w->disconnect,disconnected && !forfeit);
    if (disconnected && w->disconnect.root) tint(w->disconnect.root,1,(GXColor){255,0,0,255});
    material(&w->description,0,disconnected ? RETURN_CSS : s ? description(m,s) : GET_READY);
    /* The colour step's art reads "Same color selected. Press X/Y to change". On a keyboard those
     * are other keys (C/V by default): the art gives way to the same line with the bound keys. */
    color_hint=active && local && s->type == MU_SETUP_COLOR && !v->confirming && !v->dialog && w->keyboard_xy;
    visibility(&w->description,!color_hint && (!active || !local || (!v->confirming && !v->dialog)));
    for (i=0; i<2; ++i) {
        int visible=active && local && v->confirming && !v->dialog && (i == 0 || s->type != MU_SETUP_COLOR);
        visibility(&w->buttons[i],visible);
        state(&w->buttons[i],visible && v->button == i ? 1 : 0,0);
        /* The button's child joint is its yellow highlight frame: the selected button's blink fades it in and
         * out, but the static pose holds it solid, so the unselected button looked selected (OK and Redo
         * seemed swapped). Only the selected button shows it, as in Slippi. */
        if (visible && v->button != i && w->buttons[i].root && w->buttons[i].root->child && w->buttons[i].root->child->child)
            HSD_JObjSetFlagsAll(w->buttons[i].root->child->child,JOBJ_HIDDEN);
        if (!i) position(&w->buttons[i],s && s->type == MU_SETUP_COLOR ? 0 : -4.8f,-3);
        state(&w->turn[i],active && s->player == (i ? !m->local_player : m->local_player) ? local ? 3 : 1 : 0,0);
    }
    for (i=0; i<6; ++i) {
        int selected=0, disabled=active && i<n && !mu_ranked_setup_stage_allowed(m,pool[i]);
        for (j=0; j<v->draft.count && j<2; ++j) selected |= i<n && v->draft.stages[j] == pool[i];
        selector_draw(&w->selectors[i],mu_ranked_selector_x(i,n),5,mu_ranked_stage_material(i<n ? pool[i] : 0),
            0,0,0,active && !char_step && i<n,local && !v->confirming && v->cursor == (int)i,
            selected,s && s->type == MU_SETUP_STRIKE,disabled);
    }
    selector_draw(&w->character,0,5,local ? mu_ranked_character_material(v->draft.character) : 1,
        v->draft.character,v->draft.color,local,active && char_step,local && !v->confirming,0,0,0);
    for (i=0; i<5; ++i) {
        const MuRankedSetupStep* h=i<m->step_count ? &m->steps[i] : NULL;
        float x=mu_ranked_timeline_x(m,i,0);
        int done=v->history_valid[i], is_char=h && (h->type == MU_SETUP_COLOR || h->type == MU_SETUP_CHARACTER);
        unsigned label=!h ? EMPTY : is_char ? (h->player == m->local_player ? YOUR_CHAR : OPP_CHAR) :
            h->type == MU_SETUP_STAGE ? STAGE : m->set->cur_game>1 ? BAN : i == 2 ? STRIKE1 : i == 3 ? STRIKE23 : STRIKE4;
        for (j=0; j<2; ++j) {
            const MuRankedSetupChoice* c=&v->history[i];
            unsigned mat=done ? is_char ? mu_ranked_character_material(c->character) : mu_ranked_stage_material(c->stages[j]) : 1;
            icon_draw(&w->timeline[i][j],mu_ranked_timeline_x(m,i,j),-17.5f,mat,c->character,c->color,
                done && is_char,h && j<h->count,h && i>m->step ? 2 : active && i == m->step ? 1 : 0);
        }
        material(&w->labels[i],0,label); position(&w->labels[i],x+(h && h->count == 2 ? 2.6f : 0),-14.3f);
        visibility(&w->labels[i],h != NULL);
        position(&w->arrows[i],x-5,-17.5f); visibility(&w->arrows[i],h && i>0);
        state(&w->arrows[i],i>m->step ? 2 : active && i == m->step ? 1 : 0,0);
    }
    position(&w->dialog,0,-9); visibility(&w->dialog,active && local && v->dialog);
    for (i=0; i<27; ++i) {
        icon_draw(&w->picker[i],0,0,mu_ranked_character_material(i),i,v->draft.color,
            i<26 && i == v->picker,active && local && v->dialog,i == v->picker ? 4 : 0);
    }
    if (++w->ranks_frames >= 60 && m->set && m->local_player<2) {
        header_ranks(w,m); w->ranks_frames=0;
    }
    /* Readable fallback also makes missing disc registration diagnosable. */
    if (w->missing) {
        const char* action=!s ? disconnected ? "Disconnected" : "Get ready" :
            s->type == MU_SETUP_STRIKE ? m->set->cur_game == 1 ? "Strike stages" : "Ban stage" :
            s->type == MU_SETUP_STAGE ? "Counterpick stage" : s->type == MU_SETUP_COLOR ? "Choose costume" : "Choose character";
        sprintf(line,"%s%s",active && !local ? "Opponent: " : "",action); text_set(w->text,1,line);
        if (active && !char_step) {
            static const char* names[]={"?","?","Battlefield","Yoshi's","Dream Land","Final Destination","Fountain","Stadium"};
            int cursor=v->cursor;
            unsigned stage=cursor>=0 && cursor<(int)n ? pool[cursor] : 0;
            sprintf(line,local ? ">%s (%u)  Selected %u/%u" : "%s",
                local ? names[mu_ranked_stage_material(stage)] : "Waiting for opponent's stage",stage,v->draft.count,s->count);
        } else if (char_step && local) {
            static const char* names[]={"Falcon","Donkey Kong","Fox","Mr. Game & Watch","Kirby","Bowser","Link",
                "Luigi","Mario","Marth","Mewtwo","Ness","Peach","Pikachu","Ice Climbers","Jigglypuff","Samus",
                "Yoshi","Zelda","Sheik","Falco","Young Link","Dr. Mario","Roy","Pichu","Ganondorf","Random"};
            unsigned c=v->dialog ? v->picker : v->draft.character;
            sprintf(line,"%s  Costume %u",names[c<27 ? c : 26],v->draft.color);
        } else if (char_step) strcpy(line,"Waiting for opponent's character");
        else line[0]=0;
        text_set(w->text,2,line);
        if (v->dialog)
            sprintf(line,"Arrows: character  %s: costume  %s: choose  %s: cancel",w->key_xy,w->key_a,w->key_b);
        else if (v->confirming)
            sprintf(line,"%s    %s: confirm  %s: redo",v->button ? "OK   >Redo" : ">OK   Redo",w->key_a,w->key_b);
        else sprintf(line,"Arrows: move  %s: select  %s: clear  %s: costume",w->key_a,w->key_b,w->key_xy);
        text_set(w->text,3,line);
        text_set(w->text,4,"GameSetup visuals unavailable");
    } else {
        for (i=1; i<5; ++i) text_set(w->text,i,"");
        if (color_hint) {
            /* Line 3 sits where the description art does (y 240 + 45 on the 640x480 canvas). */
            sprintf(line,"Same color selected. Press %s to change",w->key_xy);
            text_set(w->text,3,line);
        }
    }
}
void mu_ranked_widgets_free(MuRankedWidgets* w)
{
    unsigned i;
    if (!w) return;
    /* Badges/text before their canvas/camera; GObjs in reverse creation order.
     * Archive lifetime belongs to lbArchive's scene heap, not this container. */
    for (i=0; i<2; ++i) {
        if (w->badges[i]) mu_rank_badge_free(w->badges[i]);
        if (w->names[i]) HSD_SisLib_803A5CC4(w->names[i]);
    }
    if (w->text) HSD_SisLib_803A5CC4(w->text);
    HSD_SisLib_803A5E70(); /* this scene's private SIS canvas and ortho camera */
    while (w->object_count) HSD_GObjFree(w->objects[--w->object_count]);
    HSD_Free(w);
}
#endif
#endif
