"""The hand edits Training Mode CE's C needs to compile natively, applied to the copy in
sourceport/game/tmce/src. Each one keeps the original line under #else, so the console build of the
same file is unchanged. Re-running is safe: an edit already applied is skipped.

usage: python tools/tmce/native_edits.py
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, 'sourceport', 'game', 'tmce', 'src')

# (file, original line stripped, native replacement line(s) at the same indentation)
EDITS = [
    # MatchInit.timer and .unk1f span several decomp bitfields, whose bit order differs natively
    ('events.c', 'matchData->unk1f = 3;', 'mexbits_set_MatchInit__unk1f(matchData, 3);'),
    ('events.c', 'matchData->timer = eventMatchData->timer;',
     'mexbits_set_MatchInit__timer(matchData, eventMatchData->timer);'),
    ('eggs.c', 'stc_match->match.timer = MATCH_TIMER_COUNTUP;',
     'mexbits_set_Match__match_timer(stc_match, MATCH_TIMER_COUNTUP);'),
    ('eggs.c', 'if (stc_match->match.timer == MATCH_TIMER_COUNTUP)',
     'if (mexbits_get_Match__match_timer(stc_match) == MATCH_TIMER_COUNTUP)'),
    ('eggs.c', 'if (stc_match->time_frames == 3600 && stc_match->match.timer != MATCH_TIMER_COUNTUP)',
     'if (stc_match->time_frames == 3600 && mexbits_get_Match__match_timer(stc_match) != MATCH_TIMER_COUNTUP)'),
    ('osds.c', 'if (!(ft_sub_data->cpu.xf8 & CPU_FLAG_ISCOPY)) {',
     'if (!(mexbits_get_FighterData__cpu_xf8(ft_sub_data) & CPU_FLAG_ISCOPY)) {'),
    # the debug camera and the damage display are separate objects natively
    ('events.c', 'DevCam_AdjustRotate(cobj, &cam->devcam_rot, &cam->devcam_pos, x, y);',
     'DevCam_AdjustRotate(cobj, MEX_DEVCAM_ROT(), MEX_DEVCAM_POS(), x, y);'),
    ('techchase.c', 'stc_matchcam->devcam_pos = saved_cam_pos;', '*MEX_DEVCAM_POS() = saved_cam_pos;'),
    ('techchase.c', 'stc_matchcam->devcam_rot = saved_cam_rot;', '*MEX_DEVCAM_ROT() = saved_cam_rot;'),
    ('techchase.c', 'saved_cam_pos = stc_matchcam->devcam_pos;', 'saved_cam_pos = *MEX_DEVCAM_POS();'),
    ('techchase.c', 'saved_cam_rot = stc_matchcam->devcam_rot;', 'saved_cam_rot = *MEX_DEVCAM_ROT();'),
    ('edgeguard.c', 'MatchHUDElement *hud = &stc_matchhud->element_data[ply];',
     'MatchHUDElement *hud = MEX_MATCHHUD_ELEMENT(ply);'),
    ('savestate_v1.c', 'MatchHUDElement *hud = &stc_matchhud->element_data[i];',
     'MatchHUDElement *hud = MEX_MATCHHUD_ELEMENT(i);'),
    # a bad friction value (a layout regression) must give a wrong target size, never a frozen game:
    # the loop runs about 20 steps for every fighter, 600 is 10 seconds of sliding
    ('wavedash.c', 'while (mag > 0)', 'for (int mu_steps = 0; mag > 0 && mu_steps < 600; mu_steps++)'),
    # Playerblock.gobj: the header puts it at 0xAC, the game keeps it at 0xB0 (twin extras mu_gobj0/1)
    ('savestate_v1.c', 'fighter_gobj[0] = playerblock->gobj[0];', 'fighter_gobj[0] = playerblock->mu_gobj0;'),
    ('savestate_v1.c', 'fighter_gobj[1] = playerblock->gobj[1];', 'fighter_gobj[1] = playerblock->mu_gobj1;'),
    ('savestate_v1.c', 'playerblock->gobj[0] = fighter_gobj[0];', 'playerblock->mu_gobj0 = fighter_gobj[0];'),
    ('savestate_v1.c', 'playerblock->gobj[1] = fighter_gobj[1];', 'playerblock->mu_gobj1 = fighter_gobj[1];'),
    # fixed console addresses: the decomp objects at those addresses (tmce_compat.h)
    ('lab.c', '*(u32*)(0x801d463c) = 0x806d3778;', 'mu_tmce_stadium_override = 1; // gr/grpstadium.c reads the choice'),
    ('events.c', 'COBJDesc ***dmgScnMdls = Archive_GetPublicAddress(*stc_ifall_archive, (void *)0x803f94d0);',
     'void *dmgScnMdls = Archive_GetPublicAddress(*stc_ifall_archive, "ScInfDmg_scene_data");'),
    ('menu.c', 'COBJDesc ***dmgScnMdls = Archive_GetPublicAddress(*stc_ifall_archive, (void *)0x803f94d0);',
     'void *dmgScnMdls = Archive_GetPublicAddress(*stc_ifall_archive, "ScInfDmg_scene_data");'),
    ('events.c', 'COBJDesc *cam_desc = dmgScnMdls[1][0];', 'COBJDesc *cam_desc = MEX_DP_AT(MEX_DP_AT(dmgScnMdls, 1), 0);'),
    ('menu.c', 'COBJDesc *cam_desc = dmgScnMdls[1][0];', 'COBJDesc *cam_desc = MEX_DP_AT(MEX_DP_AT(dmgScnMdls, 1), 0);'),
    ('ledgedash.c', 'HSD_ObjFree((void *)0x804d0f60, ptcl);', 'HSD_ObjFree((void *)mu_mx_hsd_804D0F60, ptcl);'),
    ('savestate_v1.c', 'HSD_ObjFree((void *)0x804d0f60, ptcl);', 'HSD_ObjFree((void *)mu_mx_hsd_804D0F60, ptcl);'),
    ('ledgedash.c', 'float *unk_cam = (void *)0x803bcca0;', 'float *unk_cam = (void *)mu_mx_cm_803BCCA0;'),
    ('lcancel.c', '(void *)0x803f58e0,', '(void *)mu_mx_it_803F58E0,'),
    ('lcancel.c', '(void *)0x80287458,', '(void *)mu_mx_it_3F14_Logic2_Spawned,'),
    ('lcancel.c', '(void *)0x80287e68,', '(void *)mu_mx_itTaru_Logic2_PickedUp,'),
    ('lcancel.c', '(void *)0x80287ea8,', '(void *)mu_mx_itTaru_Logic2_Dropped,'),
    ('lcancel.c', '(void *)0x80287ec8,', '(void *)mu_mx_itTaru_Logic2_Thrown,'),
    ('lcancel.c', '(void *)0x80288818,', '(void *)mu_mx_it_3F14_Logic2_DmgDealt,'),
    ('lcancel.c', '(void *)0x802889f8,', '(void *)mu_mx_it_3F14_Logic2_Reflected,'),
    ('lcancel.c', '(void *)0x802888b8,', '(void *)mu_mx_it_3F14_Logic2_Clanked,'),
    ('lcancel.c', '(void *)0x80288958,', '(void *)mu_mx_it_3F14_Logic2_HitShield,'),
    ('lcancel.c', '(void *)0x80288c68,', '(void *)mu_mx_itTaru_Logic2_EvtUnk,'),
    ('lcancel.c', '(void *)0x803f5988,', '(void *)mu_mx_it_803F5988,'),
    ('events.h', '#define event_vars_ptr_loc ((EventVars**)0x803d7054)',
     '#define event_vars_ptr_loc ((EventVars**)&mu_tmce_event_vars) // shared by every module (tmce_runtime.c)'),
    # CollData.u in MexTK is the fighter's own ecb lock word right after its collision data
    ('lab.c', 'if (vertices_num < (fighter_data->coll_data.u.ecb_bot_lock_frames - fighter_data->dmg.hitlag_frames))',
     'if (vertices_num < (fighter_data->mu_ecb_bot_lock_frames - fighter_data->dmg.hitlag_frames))'),
    ('lab.c', 'Text_SetText(text, i, "ECB Lock: %d", fighter_data->coll_data.u.ecb_bot_lock_frames);',
     'Text_SetText(text, i, "ECB Lock: %d", fighter_data->mu_ecb_bot_lock_frames);'),
    # disc pointer slots
    ('lab.c', 'char *symbol = action->anim_symbol;', 'char *symbol = MEX_DP(action->anim_symbol);'),
    ('lab.c', 'symbol = action->anim_symbol;', 'symbol = MEX_DP(action->anim_symbol);'),
    ('lab.c', 'RGB565 *orig_img = snap_image.img_ptr;', 'RGB565 *orig_img = MEX_DP(snap_image.img_ptr);'),
    ('lab.c', 'resized_image.img_ptr = new_img;                                            // store pointer to resized image',
     'MEX_DP_SET(resized_image.img_ptr, new_img); // store pointer to resized image'),
]


def apply(path, orig, native):
    text = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in text else '\n'
    lines = text.split(nl)
    changed = 0
    i = 0
    while i < len(lines):
        if lines[i].strip() == orig and not (i > 0 and lines[i - 1].strip() == '#else'):
            ind = lines[i][:len(lines[i]) - len(lines[i].lstrip())]
            block = ['#ifdef MU_NATIVE'] + [ind + n for n in native.split('\n')] + ['#else', lines[i], '#endif']
            lines[i:i + 1] = block
            i += len(block)
            changed += 1
            continue
        i += 1
    if changed:
        open(path, 'w', encoding='utf-8', newline='').write(nl.join(lines))
    return changed


# Whole blocks, from the line equal to `first` through the next line equal to `last`.
DISC = 'MEX_DISC_STRUCT'   # big-endian, as the struct sits in the .dat file (tmce_compat.h)
BLOCK_EDITS = [
    # TM-CE's own asset tables, read straight out of its .dat files: pointers are 4-byte disc slots
    ('menu.h', 'typedef struct evMenu', '} evMenu;', '''typedef struct MEX_DISC_STRUCT evMenu
{
    unsigned int menu;          // JOBJDesc *, read with MEX_DP()
    unsigned int popup;
    unsigned int scroll;
    unsigned int check;
    unsigned int arrow;
    unsigned int playback;
    unsigned int message;
    unsigned int hud_cobjdesc;  // COBJDesc *
    unsigned int tip_jobj;
    unsigned int tip_jointanim; // disc array of pointers
} evMenu;'''),
    ('lab_common.h', 'typedef struct Arch_ImportData', '} Arch_ImportData;', '''typedef struct MEX_DISC_STRUCT Arch_ImportData
{
    unsigned int import_button; // JOBJDesc *, read with MEX_DP()
    unsigned int import_menu;
    unsigned int import_cam;    // COBJDesc *
    unsigned int import_popup;
} Arch_ImportData;'''),
    ('lab_common.h', 'typedef struct Arch_LabData', '} Arch_LabData;', '''typedef struct MEX_DISC_STRUCT Arch_LabData
{
    unsigned int stick;         // JOBJDesc *, read with MEX_DP()
    unsigned int cstick;
    unsigned int save_icon;
    unsigned int save_banner;
    unsigned int controller;
    unsigned int export_menu;
    unsigned int export_popup;
} Arch_LabData;'''),
    ('wavedash.h', 'struct WavedashAssets', '};', '''struct MEX_DISC_STRUCT WavedashAssets
{
    unsigned int hud;           // JOBJDesc *, read with MEX_DP()
    unsigned int hudmatanim;    // disc array of pointers
    unsigned int target_jobj;
    unsigned int target_jointanim;
    unsigned int target_matanim;
};'''),
    # stage internals the lab sets by console offset: the decomp's own fields natively
    ('lab.c', 'u8 *tform_data = gobj->userdata;', '*TRANSFORMATION_ID_PTR = transformation_id;',
     '''    // native: the same ground countdown and stage timers, set by name (gr/grpstadium.c)
    mu_tmce_stadium_transform(gobj, value - 1);'''),
    ('lab.c', 'struct FODData { u16 mode, timer; } *data = (void*)((u8*)platform_gobj->userdata + 0xC4);',
     'data->timer = 0xFF;',
     '''    mu_tmce_fod_platform_set(platform_gobj, 1, 0xFF); // gr/grizumi.c'''),
    ('slalom.c', 'static struct WavedashAssets {', '} *assets;', '''static struct MEX_DISC_STRUCT WavedashAssets {
    unsigned int hud;           // JOBJDesc *, read with MEX_DP()
    unsigned int hudmatanim;    // disc arrays of pointers
    unsigned int pole_jobj;
    unsigned int pole_jointanim;
    unsigned int pole_matanim;
} *assets;'''),
    # values TM-CE passes between its modules through spare console memory: runtime variables natively
    ('lab_common.h', 's8 *onload_fileno = R13_OFFSET(-0x4670);', 's8 *onload_slot = R13_OFFSET(-0x466F);',
     '''s8 *onload_fileno = &mu_tmce_onload_fileno;
s8 *onload_slot = &mu_tmce_onload_slot;'''),
]


# Reads of the asset slots above: `base->slot` becomes MEX_DP(base->slot), `base->slot[i]` becomes
# MEX_DP_AT(MEX_DP(base->slot), i). Lines are rewritten in the listed files wherever a listed base is
# followed by a listed slot (the original line kept under #else).
SLOT_BASES = r'(?:stc_event_vars\.menu_assets|event_vars->menu_assets|menuAssets|menu_assets|stc_lab_data|' \
             r'stc_import_assets|event_data->assets|assets)'
SLOT_FIELDS = r'(?:menu|popup|scroll|check|arrow|playback|message|hud_cobjdesc|tip_jobj|tip_jointanim|' \
              r'import_button|import_menu|import_cam|import_popup|stick|cstick|save_icon|save_banner|controller|' \
              r'export_menu|export_popup|hud|hudmatanim|target_jobj|target_jointanim|target_matanim|pole_jobj|' \
              r'pole_jointanim|pole_matanim)'
SLOT_FILES = ['events.c', 'menu.c', 'lab.c', 'lab_css.c', 'wavedash.c', 'slalom.c', 'lcancel.c', 'ledgedash.c']


def wrap_slots(line):
    import re
    idx = re.compile(r'\b(' + SLOT_BASES + r'->' + SLOT_FIELDS + r')\[([^\]]+)\]')
    line2 = idx.sub(r'MEX_DP_AT(MEX_DP(\1), \2)', line)
    plain = re.compile(r'(?<![\w>.])(' + SLOT_BASES + r'->' + SLOT_FIELDS + r')\b(?!\s*[\[=(])')
    out, pos = [], 0
    for m in plain.finditer(line2):
        before = line2[:m.start()]
        if before.endswith('MEX_DP(') or before.endswith('&'):
            continue
        out.append(line2[pos:m.start()] + 'MEX_DP(' + m.group(1) + ')')
        pos = m.end()
    out.append(line2[pos:])
    return ''.join(out)


def apply_slots(path):
    import re
    text = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in text else '\n'
    lines = text.split(nl)
    probe = re.compile(r'\b' + SLOT_BASES + r'->' + SLOT_FIELDS + r'\b')
    out, changed, i = [], 0, 0
    depth = 0
    while i < len(lines):
        l = lines[i]
        s = l.strip()
        guarded = out and out[-1].strip() in ('#else', '#ifdef MU_NATIVE')
        if probe.search(l) and not s.startswith('//') and not guarded and 'MEX_DP' not in l:
            new = wrap_slots(l)
            if new != l:
                out += ['#ifdef MU_NATIVE', new, '#else', l, '#endif']
                changed += 1
                i += 1
                continue
        out.append(l)
        i += 1
    if changed:
        open(path, 'w', encoding='utf-8', newline='').write(nl.join(out))
    return changed


def apply_block(path, first, last, native):
    text = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in text else '\n'
    lines = text.split(nl)
    for i, l in enumerate(lines):
        if l.strip() == first and not (i > 0 and lines[i - 1].strip() == '#else'):
            for j in range(i, len(lines)):
                if lines[j].strip() == last:
                    lines[i:j + 1] = ['#ifdef MU_NATIVE'] + native.split('\n') + ['#else'] + lines[i:j + 1] + ['#endif']
                    open(path, 'w', encoding='utf-8', newline='').write(nl.join(lines))
                    return 1
            return 0
    return 0


def main():
    total = 0
    missing = []
    for fn, first, last, native in BLOCK_EDITS:
        total += apply_block(os.path.join(SRC, fn), first, last, native)
        if first not in open(os.path.join(SRC, fn), encoding='utf-8').read():
            missing.append((fn, first))
    for fn in SLOT_FILES:
        total += apply_slots(os.path.join(SRC, fn))
    for fn, orig, native in EDITS:
        n = apply(os.path.join(SRC, fn), orig, native)
        total += n
        text = open(os.path.join(SRC, fn), encoding='utf-8').read()
        if orig not in text:
            missing.append((fn, orig))
    print('applied', total)
    for fn, orig in missing:
        print('NOT FOUND', fn, orig)
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
