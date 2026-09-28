#include "../MexTK/mex.h"
#include "events.h"

void Exit(GOBJ *menu);

enum options_main {
    OPT_BARREL,
    OPT_EXIT,

    OPT_COUNT
};

static EventOption Options_Main[OPT_COUNT] = {
    {
        .kind = OPTKIND_TOGGLE,
        .name = "Target",
        .desc = {"Enable a target to attack."},
    },
    {
        .kind = OPTKIND_FUNC,
        .name = "Exit",
        .desc = { "Return to the Event Select Screen." },
        .OnSelect = Exit,
    },
};

static EventMenu Menu_Main = {
    .name = "Float Cancel Training",
    .option_num = countof(Options_Main),
    .options = Options_Main,
};

enum PeachActionStates {
    ASID_FLOAT = 341,
    ASID_FLOATENDF,
    ASID_FLOATENDB,
    ASID_FLOATATTACKN,
    ASID_FLOATATTACKF,
    ASID_FLOATATTACKB,
    ASID_FLOATATTACKU,
    ASID_FLOATATTACKD,
};

enum Action {
    Action_None,
    Action_Wait,
    Action_Float,
    Action_FloatNeutral,
    Action_FloatAttack,
    Action_Hitlag,
    Action_FloatAttackFall,
    Action_FloatAttackFastFall,
    Action_Landing,
    
    Action_Count
};
static char *action_names[] = {
    "None",
    "Wait",
    "Float",
    "Deadzone",
    "Attack",
    "Hitlag",
    "Fall",
    "Fastfall",
    "Landing",
};

static u8 action_log[35];
static u32 action_log_cur = countof(action_log); // start with log disabled
static GOBJ *barrel;

static GXColor action_colors[Action_Count] = {
    {40, 40, 40, 180},  // dark gray - none
    {80, 80, 80, 180},  // light gray - wait
    {255, 128, 128, 180}, // red - float
    {230, 22, 198, 180}, // magenta - deadzone
    {52, 202, 228, 180}, // cyan - attack
    {70, 40, 60, 180}, // hitlag
    {128, 128, 255, 180}, // blue - fall
    {128, 255, 128, 180}, // green - fastfall
    {220, 220, 70, 180}, // yellow - landing
};

void GX(GOBJ *gobj, int pass) {
    // hitbox GX
    if (pass == 1) {
        GOBJ *ft = Fighter_GetGObj(0);
        FighterData *ft_data = ft->userdata;
        
        static GXColor diffuse = {255, 0, 0, 100};
        static GXColor ambient = {255, 255, 255, 255};
        for (u32 i = 0; i < countof(ft_data->hitbox); ++i) {
            ftHit *hit = &ft_data->hitbox[i];
            if (hit->active)
                Develop_DrawSphere(hit->size, &hit->pos, &hit->pos_prev, &diffuse, &ambient);
        }
    }
    
    // action log gx
    if (pass == 2) {
        event_vars->HUD_DrawActionLogBar(
            action_log,
            action_colors,
            countof(action_log)
        );
        
        // skip none and wait states
        event_vars->HUD_DrawActionLogKey(
            &action_names[2],
            &action_colors[2],
            countof(action_names) - 2
        );
    }
}

void Exit(GOBJ *menu) {
    stc_match->state = 3;
    Match_EndVS();
}

void Event_Init(GOBJ *menu) {
    // init display gobj
    GOBJ *gobj = GObj_Create(0, 0, 0);
    GObj_AddGXLink(gobj, GX, 5, 0);
}

void Barrel_Spawn(void) {
    Vec3 pos = { 0 };
    SpawnItem spawn = {
        .it_kind = ITEM_BARREL,
        .pos = pos,
        .pos2 = pos,
        .facing_direction = 1,
        .is_raycast_below = 1,
    };
    barrel = Item_CreateItem2(&spawn);
}

int Barrel_OnTakeDamage(GOBJ *_) {
    GOBJ *ft = Fighter_GetGObj(0);
    FighterData *ft_data = ft->userdata;
    int state = ft_data->state_id;
    
    if (ASID_FLOATATTACKN <= state && state <= ASID_FLOATATTACKD) {
        ItemData *data = barrel->userdata;
        Effect_SpawnSync(1063, barrel, &data->pos);
    }
    return false;
}

void Barrel_OnDestroy(GOBJ *_) {
    if (Options_Main[OPT_BARREL].val)
        Barrel_Spawn();
}

void Event_Think(GOBJ *menu) {
    GOBJ *ft = Fighter_GetGObj(0);
    FighterData *ft_data = ft->userdata;

    // barrel ------------------------------------------------------------------
    
    if (Options_Main[OPT_BARREL].val) {
        if (barrel == 0)
            Barrel_Spawn();
        
        // Some internal functions adjust these in think functions,
        // so we need to set it every frame.
        ItemData *data = barrel->userdata;
        data->it_func->OnTakeDamage = Barrel_OnTakeDamage;
        data->it_func->OnDestroy = Barrel_OnDestroy;
        data->can_hold = false;
    }
    
    if (!Options_Main[OPT_BARREL].val && barrel != 0) {
        GObj_Destroy(barrel);
        barrel = 0;
    }

    // action log --------------------------------------------------------------
    
    {
        int state = ft_data->state_id;
        int is_fastfall = ft_data->flags.is_fastfall;
        
        // determine current action
        int cur_action;
        bool reset = false;
        if (ft_data->flags.hitlag) {
            cur_action = Action_Hitlag;
        } else if (state == ASID_JUMPF || state == ASID_JUMPB) {
            cur_action = Action_None;
            reset = ft_data->TM.state_frame == 2;
        } else if (state == ASID_JUMPAERIALF || state == ASID_JUMPAERIALB) {
            cur_action = Action_None;
            reset = ft_data->TM.state_frame == 1;
        } else if (state == ASID_FLOAT) {
            if (ft_data->input.lstick.X == 0.f && ft_data->input.lstick.Y == 0.f)
                cur_action = Action_FloatNeutral;
            else
                cur_action = Action_Float;
        } else if (state >= ASID_FLOATATTACKN && state <= ASID_FLOATATTACKD) {
            if (ft_data->phys.self_vel.Y == 0.f)
                cur_action = Action_FloatAttack;
            else
                cur_action = is_fastfall ? Action_FloatAttackFastFall : Action_FloatAttackFall;
        } else if (state == ASID_LANDING || state == ASID_SQUAT || state == ASID_SQUATWAIT || state == ASID_SQUATRV) {
            cur_action = Action_Landing;
        } else {
            cur_action = Action_Wait;
        }
            
        if (reset) {
            memset(action_log, Action_None, sizeof(action_log));
            action_log_cur = 0;
        } else if (action_log_cur < countof(action_log)) {
            action_log[action_log_cur++] = cur_action;
        }
    }
}

EventMenu *Event_Menu = &Menu_Main;
