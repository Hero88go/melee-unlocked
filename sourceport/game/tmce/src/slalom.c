#include "../MexTK/mex.h"
#include "events.h"

#define Pole_MaxDistance 30.f
#define Pole_MinDistance 10.f
#define Pole_Scale (Vec3) {0.15f, 1.0f, 0.15f}
#define Pole_ScaleQueued (Vec3) {0.1f, 0.2f, 0.1f}
#define PoleJOBJ_Aura 3

static Text *hud_score_text, *hud_best_text;
int high_score, pole_counter;

static GOBJ *active_pole_gobj;
static GOBJ *next_pole_gobj;

static void Pole_ChangeState(GOBJ *pole_gobj, int state);
static void Pole_Think(GOBJ *pole_gobj);
static GOBJ *Pole_Spawn(void);
static void Pole_QueueNext(void);

static void Exit(GOBJ *menu) {
    stc_match->state = 3;
    Match_EndVS();
}

static void Retry(GOBJ *menu) {
    stc_match->end_kind = MATCHENDKIND_RETRY;
    stc_match->state = 3;
    Match_EndVS();
}

enum PoleState {
    PoleState_Spawn,
    PoleState_Wait,
    PoleState_Despawn,
};

typedef struct PoleData {
    int state;
    Vec3 pos;
    float side;
    int line_index;
    CmSubject *cam;
} PoleData;

#ifdef MU_NATIVE
static struct MEX_DISC_STRUCT WavedashAssets {
    unsigned int hud;           // JOBJDesc *, read with MEX_DP()
    unsigned int hudmatanim;    // disc arrays of pointers
    unsigned int pole_jobj;
    unsigned int pole_jointanim;
    unsigned int pole_matanim;
} *assets;
#else
static struct WavedashAssets {
    JOBJDesc *hud;
    void **hudmatanim;
    JOBJDesc *pole_jobj;
    void **pole_jointanim;
    void **pole_matanim;
} *assets;
#endif

enum options_main {
    OPT_RETRY,
    OPT_EXIT,

    OPT_COUNT
};

static EventOption Options_Main[OPT_COUNT] = {
    {
        .kind = OPTKIND_FUNC,
        .name = "Retry",
        .desc = { "Retry this event. Or, press Z while paused." },
        .OnSelect = Retry
    },
    {
        .kind = OPTKIND_FUNC,
        .name = "Exit",
        .desc = { "Return to the Event Select Screen." },
        .OnSelect = Exit,
    },
};

static EventMenu Menu_Main = {
    .name = "Slalom",
    .option_num = countof(Options_Main),
    .options = Options_Main,
};

// Pole functions

static int Pole_CheckArea(int line, Vec3 *pos, float x_offset, int *ret_line, Vec3 *ret_pos, Vec3 *ret_slope) {
    return GrColl_CrawlGround(line, pos, ret_line, ret_pos, 0, ret_slope, x_offset, 0);
}

static void Pole_QueueNext(void) {
    if (active_pole_gobj)
        Pole_ChangeState(active_pole_gobj, PoleState_Despawn);

    if (next_pole_gobj) {
        JOBJ *pole_jobj = next_pole_gobj->hsd_object;
        pole_jobj->scale = Pole_Scale;
    }

    active_pole_gobj = next_pole_gobj;
    next_pole_gobj = Pole_Spawn();
}

static GOBJ *Pole_Spawn(void) {
    GOBJ *hmn = Fighter_GetGObj(0);
    FighterData *hmn_data = hmn->userdata;

    Vec3 ray_angle;
    Vec3 ray_pos;
    int ray_index;
    
    if (hmn_data->coll_data.ground_index == -1)
        return 0;


    Vec3 source_pos;
    float check_direction;
    if (active_pole_gobj) {
        PoleData *active_data = active_pole_gobj->userdata;
        source_pos = active_data->pos;
        check_direction = -active_data->side;
        
        // move away from sides
        if (source_pos.X >  60.f) source_pos.X -= 10.f;
        if (source_pos.X < -60.f) source_pos.X += 10.f;
    } else {
        source_pos = hmn_data->phys.pos;
        check_direction = HSD_Randi(2) ? -1.f : 1.f;
    }

    // begin looking for valid ground at a random distance
    for (;;)
    {
        // random distance
        float max = Pole_MaxDistance;
        float min = Pole_MinDistance;
        float distance = HSD_Randf() * (max - min) + min;
        float x = distance * check_direction;

        // check if valid
        int ok = Pole_CheckArea(hmn_data->coll_data.ground_index, &source_pos, x, &ray_index, &ray_pos, &ray_angle);
        if (ok) break;

        check_direction = -check_direction;
    }

    // create pole gobj
    GOBJ *pole_gobj = GObj_Create(10, 11, 0);

    // pole data
    PoleData *pole_data = calloc(sizeof(PoleData));
    pole_data->side = check_direction;
    GObj_AddUserData(pole_gobj, 4, HSD_Free, pole_data);

    // add proc
    GObj_AddProc(pole_gobj, Pole_Think, 16);

    // create jobj
#ifdef MU_NATIVE
    JOBJ *pole_jobj = JOBJ_LoadJoint(MEX_DP(assets->pole_jobj));
#else
    JOBJ *pole_jobj = JOBJ_LoadJoint(assets->pole_jobj);
#endif
    GObj_AddObject(pole_gobj, 3, pole_jobj);
    GObj_AddGXLink(pole_gobj, GXLink_Common, 5, 0);

    // scale pole
    pole_jobj->scale = Pole_ScaleQueued;

    // move pole
    pole_jobj->trans.X = ray_pos.X;
    pole_jobj->trans.Y = ray_pos.Y;
    pole_jobj->trans.Z = ray_pos.Z;

    // adjust rotation based on line slope
    JOBJ *aura_jobj;
    JOBJ_GetChild(pole_jobj, &aura_jobj, PoleJOBJ_Aura, -1);
    aura_jobj->rot.Z = -atan2(ray_angle.X, ray_angle.Y);

    // create camera box
    CmSubject *cam = CameraSubject_Alloc();
    cam->boundleft_proj = -10;
    cam->boundright_proj = 10;
    cam->boundtop_proj = 10;
    cam->boundbottom_proj = -10;

    // update camerabox position
    cam->cam_pos.X = pole_jobj->trans.X;
    cam->cam_pos.Y = pole_jobj->trans.Y + 15;
    cam->cam_pos.Z = pole_jobj->trans.Z;

    // init pole data
    Pole_ChangeState(pole_gobj, PoleState_Spawn); // enter spawn state
    pole_data->cam = cam;                          // save camera
    pole_data->line_index = ray_index;             // save line index
    pole_data->pos = ray_pos;                      // save position

    return pole_gobj;
}

static void Pole_Think(GOBJ *pole_gobj) {
    JOBJ *pole_jobj = pole_gobj->hsd_object;
    PoleData *pole_data = pole_gobj->userdata;

    // update anim
    JOBJ_AnimAll(pole_jobj);

    // ensure line still exists
    if (GrColl_CheckIfLineEnabled(pole_data->line_index) == 0) {
        // enter exit state
        Pole_ChangeState(pole_gobj, PoleState_Despawn);
    }

    // update pole position (look into how fighters are rooted on ground)
    Vec3 pos_diff;
    GrColl_GetPosDifference(pole_data->line_index, &pole_data->pos, &pos_diff);
    VECAdd(&pole_data->pos, &pos_diff, &pole_data->pos);

    // update pole orientation
    Vec3 slope;
    GrColl_GetLineSlope(pole_data->line_index, &slope);
    JOBJ *aura_jobj;
    JOBJ_GetChild(pole_jobj, &aura_jobj, PoleJOBJ_Aura, -1);
    aura_jobj->rot.Z = -1 * atan2(slope.X, slope.Y);

    // update camerabox position
    CmSubject *cam = pole_data->cam;
    cam->cam_pos.X = pole_data->pos.X;
    cam->cam_pos.Y = pole_data->pos.Y + 15;
    cam->cam_pos.Z = pole_data->pos.Z;

    // update position
    pole_jobj->trans = pole_data->pos;
    JOBJ_SetMtxDirtySub(pole_jobj);

    // state based logic
    switch (pole_data->state) {
        case (PoleState_Spawn): {
            // check if ended
            if (JOBJ_CheckAObjEnd(pole_jobj) == 0)
                Pole_ChangeState(pole_gobj, PoleState_Wait);
        
            break;
        }
        case (PoleState_Wait): {
            if (pole_gobj == active_pole_gobj) {
                FighterData *hmn_data = Fighter_GetGObj(0)->userdata;
                if ((pole_data->pos.X < hmn_data->phys.pos.X) != (pole_data->pos.X < hmn_data->phys.pos_prev.X)) {
                    // decent: 105, 157, 182, 230, 249, 287, 354, 355, 356, 357, 358
                    SFX_Play(249);
                    pole_counter++;
                    Pole_QueueNext();
                }
            }
        
            break;
        }
        case (PoleState_Despawn): {
            // check if ended
            if (JOBJ_CheckAObjEnd(pole_jobj) == 0)
            {
                CameraSubject_Destroy(pole_data->cam);
                GObj_Destroy(pole_gobj);
            }
        
            break;
        }
    }
}

static void Pole_ChangeState(GOBJ *pole_gobj, int state) {
    PoleData *pole_data = pole_gobj->userdata;
    JOBJ *pole_jobj = pole_gobj->hsd_object;

    // update state
    pole_data->state = state;

    // add anim
#ifdef MU_NATIVE
    JOBJ_AddAnimAll(pole_jobj, MEX_DP_AT(MEX_DP(assets->pole_jointanim), state), MEX_DP_AT(MEX_DP(assets->pole_matanim), state), 0);
#else
    JOBJ_AddAnimAll(pole_jobj, assets->pole_jointanim[state], assets->pole_matanim[state], 0);
#endif
    JOBJ_ReqAnimAll(pole_jobj, 0); // req anim
}

void Event_Init(GOBJ *gobj) {
    // use wavedash pole as slalom pole
    assets = Archive_GetPublicAddress(event_vars->event_archive, "wavedash");

    int canvas = Text_CreateCanvas(2, 0, 0, 0, 0, GXLINK_HUD, 81, 19);

    // Create text objects
    hud_score_text = Text_CreateText(2, canvas);
    hud_score_text->kerning = 1;
    hud_score_text->align = 0;
    hud_score_text->use_aspect = 1;
    hud_score_text->trans.X = 50;
    hud_score_text->trans.Y = 25;
    hud_score_text->viewport_scale.X = 1;
    hud_score_text->viewport_scale.Y = 1;
    
    hud_best_text = Text_CreateText(2, canvas);
    hud_best_text->kerning = 1;
    hud_best_text->align = 0;
    hud_best_text->use_aspect = 1;
    hud_best_text->trans.X = 50;
    hud_best_text->trans.Y = 40;
    hud_best_text->scale.X = 0.35;
    hud_best_text->scale.Y = 0.35;
    hud_best_text->viewport_scale.X = 1;
    hud_best_text->viewport_scale.Y = 1;

    // dummy text
    Text_AddSubtext(hud_score_text, 0, 0, "-");
    Text_AddSubtext(hud_best_text, 0, 0, "-");
    
    high_score = Events_GetSavedScore(stc_memcard->EventBackup.event);

    stc_match->end_kind = MATCHENDKIND_NONE;
}

void Event_Think(GOBJ *event) {
    if (event_vars->game_timer == 1) {
        GOBJ *hmn = Fighter_GetGObj(0);
        FighterData *hmn_data = hmn->userdata;
        hmn_data->phys.pos.X = 0.f;
        Fighter_UpdateCameraBox(hmn);

        CmSubject *subject = hmn_data->camera_subject;
        subject->boundleft_curr = subject->boundleft_proj;
        subject->boundright_curr = subject->boundright_proj;

        Match_CorrectCamera();
    }

    if (!active_pole_gobj)
        Pole_QueueNext();

    // Update egg count, hide when paused
    if (Pause_CheckStatus(1) != 2) {
        Text_SetText(hud_score_text, 0, "%d", pole_counter);

        if (high_score > 0) {
            Text_SetText(hud_best_text, 0, "Best: %d", high_score);
            GXColor text_white = {255, 255, 255, 255};
            Text_SetColor(hud_best_text, 0, &text_white);
        }
        else {
            Text_SetText(hud_best_text, 0, "");
        }
    } 
    else {
        Text_SetText(hud_score_text, 0, "");
        Text_SetText(hud_best_text, 0, "");
    }

    // After 1 minute and not in free practice
    if (stc_match->time_frames == 3600) {
        int event_id = stc_memcard->EventBackup.event;
        Events_SetEventAsPlayed(event_id);
        Match_SetEndGraphic(MATCHENDKIND_VSGAME);

        // high score logic
        int high_score = Events_GetSavedScore(event_id);
        if (pole_counter > high_score) {
            MatchInfo_0x0010_store(0x9c40); // Makes "new record" audio happen, IDK why
            Match_SetPostMatchSFX(325);
            Events_StoreEventScore(event_id, pole_counter);
        }
        else {
            Match_SetPostMatchSFX(324);
        }

        Match_EndImmediate();
    }
}

EventMenu *Event_Menu = &Menu_Main;
