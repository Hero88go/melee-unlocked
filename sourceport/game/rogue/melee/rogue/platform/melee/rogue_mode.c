#include "camp_scene.h"
#include "score_capture.h"
#include "native_intro.h"
#include "native_race_rules.h"
#include "rogue_mode.h"
#include "capabilities.h"
#include "debug_launch.h"
#include "ui_scene.h"
#include "melee_fighter.h"
#include "qa/special_matrix.h"
#include "qa/aerial_matrix.h"
#include "qa/passives.h"
#include "qa/native_hrc.h"
#include "native_hrc.h"
#include "native_targets.h"
#include "native_race.h"
#include "../../core/event_metrics.h"
#include "../../director/rogue_director.h"
#include "../../upgrades/upgrade_registry.h"
#include "../../encounters/encounter_registry.h"
#include "../../core/offers.h"
#include "specials/special_engine.h"
#include <melee/ft/types.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/gmregclear.h>
#include <melee/pl/player.h>
#include <melee/lb/forward.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/mn/inlines.h>
#include <dolphin/os.h>
#include <string.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/controller.h>
#include "native_menu_label.inc"

void RogueMode_CssHeading(HSD_JObj* heading)
{
    HSD_DObj* dobj;
#ifdef MU_NATIVE
    /* the label is cut from the disc at run time (native_menu_label.inc); until then the native heading stays */
    static HSD_ImageDesc image = { DISC_STATIC(0), 96, 32, GX_TF_I4, 0, 0.0f, 0.0f };
    if (gm_GetCurrentGameMode() != GM_ROGUE || !mu_rogue_css_label_ready()) return;
    DISC_SET(image.image_ptr, rogue_css_label);
#else
    static HSD_ImageDesc image = { (void*) rogue_css_label, 96, 32,
                                  GX_TF_I4, 0, 0.0f, 0.0f };
    if (gm_GetCurrentGameMode() != GM_ROGUE) return;
#endif
    for (dobj = HSD_JObjGetDObj(heading); dobj; dobj = dobj->next) {
        HSD_TObj* texture;
        if (!dobj->mobj) continue;
        for (texture = dobj->mobj->tobj; texture; texture = texture->next) {
            HSD_ImageDesc* original = texture->imagedesc;
            if (original && original->width == 96 && original->height == 32 &&
                original->format == GX_TF_I4) {
                /* Only this scene instance changes. No native archive or
                 * animation table is mutated and no heap pointer is retained. */
                texture->imagedesc = &image;
            }
        }
    }
}

static VsModeData selection;
static CSSData css;
static StartMeleeData match;
static MatchExitInfo result;
static RogueUiContext ui;
#define screen ui.text
static unsigned cursor, build_view, native_ui;
static RogueText native_overlay_back;
static RogueAcquireChoice acquire_choice;
static unsigned choice_cursor, choice_release;
static const char* feedback;
static unsigned qa_cycles, qa_frames, qa_matches, qa_failures;
static unsigned encounter_frames;
/* Physical input port is independent of the run's logical fighter slot zero. */
static unsigned run_port;
static unsigned native_route_cursor, native_route_release;
static const unsigned stages[6] = { 31, 32, 28, 8, 2, 3 };

static void plan_native_events(void)
{
    RogueRun* run=RogueDirector_Run();
    RogueEventAvailability available[ROGUE_EVENT_TEMPLATES];
    unsigned i;
    /* Only implemented native adapters enter normal route generation. The
     * planner owns deterministic opportunity selection, never scene input. */
    RogueEvent_NativeAvailability(run->character,available);
    if(!RogueRun_PlanEvents(run,available,stages))
        OSPanic(__FILE__,__LINE__,"native event route planning rejected");
    for(i=0;i<ROGUE_EVENT_OPPORTUNITIES;++i) {
        const RogueEventOpportunity* opportunity=&run->event_opportunities[i];
        if(opportunity->present)
            OSReport("[rogue] native_event_plan act=%u floor=%u side=%u template=%u stage=%u character=%u\n",
                i,opportunity->floor,opportunity->side,opportunity->event.template_id,
                opportunity->event.native_stage,run->character);
    }
}

static void change_state(unsigned id)
{
    gm_SetNextGameModeStateId(id);
    gm_801A4B60();
}

void RogueMode_Load(void)
{
    /* Native Regular Match confirm records its initiating physical port. */
    run_port = gm_801677F0();
    if (run_port >= 4) run_port = 0;
    OSReport("[rogue] run_controller=%u\n", run_port + 1);
    RogueSpecialQa_Reset();
    RogueAerialQa_Reset();
    gm_InitVsMode(&selection);
    selection.start.players[run_port].ckind = CKind_Fox;
    selection.start.players[run_port].slot_type = Gm_PKind_Human;
    selection.start.players[run_port].stocks = 3;
    selection.start.players[run_port].cpu_level = 2;
    css.unk_0x0 = run_port + 1; /* Retail single-player CSS uses a 1-based port. */
#if (ROGUE_DEBUG && ROGUE_QA_MODE) || ROGUE_QA_MODE == 5 || ROGUE_QA_NATIVE_CAMP || ROGUE_QA_EVENT_RELEASE
    qa_cycles = qa_frames = 0;
    qa_matches = qa_failures = 0;
    RogueDirector_Start(ROGUE_FIXTURE_SEED, ROGUE_LAUNCH_RECIPIENT);
#if ROGUE_QA_MODE == 3
    {
        RogueRun* run = RogueDirector_Run();
        const RogueSpecialDef* special = RogueSpecial_Find(ROGUE_LAUNCH_SPECIAL);
        unsigned tries;
        const unsigned aerials[5]=ROGUE_LAUNCH_AERIALS;
        memcpy(run->aerials,aerials,sizeof(run->aerials));
#if ROGUE_LAUNCH_GOLD >= 0
        run->gold=ROGUE_LAUNCH_GOLD;
#endif
#if ROGUE_QA_PASSIVES
        for (tries = 0; tries < ROGUE_UPGRADES; ++tries) run->stacks[tries] = 1;
#endif
        if (special) run->specials[special->slot] = special->id;
#if ROGUE_QA_NATIVE_HRC || ROGUE_QA_NATIVE_TARGETS || ROGUE_QA_NATIVE_RACE
        /* Synthetic route progression only; the native HRC attempt and its
         * receipt/reward handoff use the production owner. */
        {
            RogueEventAvailability available[7];
            unsigned seed,side;
            memset(available,0,sizeof(available));
#if ROGUE_QA_NATIVE_RACE
            available[2].supported=1;available[2].native_stage=82;
            available[2].timer_frames=60*rogue_race_seconds[ROGUE_LAUNCH_RECIPIENT];
#elif ROGUE_QA_NATIVE_TARGETS
            available[1].supported=1;
            available[1].native_stage=RogueTarget_Stage(ROGUE_LAUNCH_RECIPIENT);
            available[1].timer_frames=2700;
#else
            available[0].supported=1;available[0].native_stage=84;available[0].timer_frames=600;
#endif
            for(seed=1;seed<1000;++seed) {
                RogueDirector_Start(seed,ROGUE_LAUNCH_RECIPIENT);
                if(!RogueRun_PlanEvents(run,available,stages))
                    OSPanic(__FILE__,__LINE__,"HRC fixture plan rejected");
                if(run->event_opportunities[0].present) {
#if ROGUE_QA_NATIVE_EVENT_CAMP
                    /* Select an existing route; never overwrite its nodes. */
                    if(run->event_opportunities[0].floor!=1 ||
                       run->route[2][0]>ROGUE_ELITE ||
                       (run->route[3][0]<ROGUE_NODE_SHOP && run->route[3][1]<ROGUE_NODE_SHOP)) continue;
                    if(ROGUE_QA_EVENT_CAMP_KIND &&
                       run->route[3][0]!=ROGUE_QA_EVENT_CAMP_KIND &&
                       run->route[3][1]!=ROGUE_QA_EVENT_CAMP_KIND) continue;
                    OSReport("[rogue] event_camp_seed=%u event_floor=1 combat_floor=2 camp_floor=3\n",seed);
#endif
                    break;
                }
            }
            if(seed==1000) OSPanic(__FILE__,__LINE__,"HRC fixture seed unavailable");
            while(run->phase!=ROGUE_EVENT) {
                int accepted;
                if(run->phase==ROGUE_REWARD) accepted=RogueRun_ChooseUpgrade(run,0);
                else if(run->phase==ROGUE_ROUTE) {
                    side=run->preview[1].kind==ROGUE_NODE_EVENT ? 1 : 0;
#if ROGUE_QA_NATIVE_TARGET_ENTRY || ROGUE_QA_NATIVE_RACE_ENTRY
                    if(run->preview[side].kind==ROGUE_NODE_EVENT) break;
#endif
                    accepted=RogueRun_ChooseRoute(run,side);
                } else if(run->phase==ROGUE_FIGHT) accepted=RogueRun_MatchEnd(run,1,0);
                else accepted=RogueRun_LeaveService(run);
                if(!accepted) OSPanic(__FILE__,__LINE__,"HRC fixture progression rejected");
            }
            memset(run->stacks,0,sizeof(run->stacks));
            memset(run->upgrade_choices,0,sizeof(run->upgrade_choices));
            memcpy(run->aerials,aerials,sizeof(run->aerials));
            if(special) run->specials[special->slot]=special->id;
            run->carried_percent=80;
#if ROGUE_QA_NATIVE_TARGET_ENTRY || ROGUE_QA_NATIVE_RACE_ENTRY
            run->stacks[2]=1;
#endif
        }
#endif
        if (ROGUE_LAUNCH_SERVICE == 1) RogueRun_Shop(run);
        if (ROGUE_LAUNCH_SERVICE == 2) {
#if ROGUE_QA_NATIVE_CAMP
            run->phase=ROGUE_ROUTE;run->route[0][0]=ROGUE_NODE_REST;
            run->preview[0].kind=ROGUE_NODE_REST;
            memset(&run->preview[0].payload,0,sizeof(run->preview[0].payload));
            if(!RogueRun_ChooseRoute(run,0)) OSPanic(__FILE__,__LINE__,"camp REST fixture rejected");
#else
            run->phase=ROGUE_REST;
#endif
            run->carried_percent=60;
        }
#if ROGUE_QA_CHOICE
        {
            RogueOfferRef fixture={ROGUE_OFFER_UPGRADE,1};
            RogueOfferState* offers=run->phase==ROGUE_SHOP ? &run->camp.general : &run->reward;
            unsigned card;
            for(card=0;card<3;++card) {
                RogueOfferRef ordinary={ROGUE_OFFER_UPGRADE,card+1+(ROGUE_QA_CHOICE>1)};
                offers->ids[card]=RogueOffer_Encode(ordinary);
                offers->sold[card]=0;
                if(run->phase==ROGUE_SHOP) run->camp.general_prices[card]=RogueOfferRef_Price(run,ordinary);
            }
            offers->ids[(ROGUE_QA_CHOICE-1)%3]=RogueOffer_Encode(fixture);
            if(run->phase==ROGUE_SHOP) run->camp.general_prices[(ROGUE_QA_CHOICE-1)%3]=RogueOfferRef_Price(run,fixture);
            OSReport("[rogue] choice_fixture phase=%u gold=%u\n",run->phase,run->gold);
        }
#endif
#if ROGUE_QA_NATIVE_INTRO && ROGUE_QA_MAP_FLOOR
        /* Synthetic progression fixture for native map pictures only. */
        for(tries=0;tries<ROGUE_QA_MAP_FLOOR;++tries) {
            unsigned side=run->preview[0].kind==ROGUE_NODE_EVENT ? 1 : 0;
            if(run->phase==ROGUE_REWARD && !RogueRun_ChooseUpgrade(run,0))
                OSPanic(__FILE__,__LINE__,"map fixture reward failed");
            if(!RogueRun_ChooseRoute(run,side)) OSPanic(__FILE__,__LINE__,"map fixture route failed");
            if(run->phase==ROGUE_FIGHT) {
                if(!RogueRun_MatchEnd(run,1,0)) OSPanic(__FILE__,__LINE__,"map fixture result failed");
            } else if(!RogueRun_LeaveService(run)) OSPanic(__FILE__,__LINE__,"map fixture service failed");
        }
        OSReport("[rogue] native_map_fixture floor=%u synthetic=1\n",run->floor);
#endif
#if ROGUE_QA_NATIVE_INTRO == 2
        /* Value-only, labelled group fixture; no live fighter edits. */
        run->preview[0].payload.combat.enemy_count=3;
        run->preview[0].payload.combat.fighters[0]=2;
        run->preview[0].payload.combat.fighters[1]=2;
        run->preview[0].payload.combat.fighters[2]=9;
        run->preview[1].payload.combat.enemy_count=ROGUE_QA_SIX_ACTORS ? 3 : 2;
        run->preview[1].payload.combat.fighters[0]=14;
        run->preview[1].payload.combat.fighters[1]=3;
        run->preview[1].payload.combat.fighters[2]=4;
        /* Roster expansion must also supply playable stock budgets. */
        run->preview[0].payload.combat.stocks[1]=run->preview[0].payload.combat.stocks[0];
        run->preview[0].payload.combat.stocks[2]=run->preview[0].payload.combat.stocks[0];
        run->preview[1].payload.combat.stocks[1]=run->preview[1].payload.combat.stocks[0];
        if(ROGUE_QA_SIX_ACTORS)
            run->preview[1].payload.combat.stocks[2]=run->preview[1].payload.combat.stocks[0];
        OSReport("[rogue] native_intro_fixture groups=3,%u duplicate=1 linked=1\n",
            run->preview[1].payload.combat.enemy_count);
#endif
#if ROGUE_QA_NATIVE_INTRO >= 3
        /* Presentation-only typed service/event fixture, never an event clear. */
        run->preview[1].kind=ROGUE_QA_NATIVE_INTRO==3 ? ROGUE_NODE_SHOP :
            ROGUE_QA_NATIVE_INTRO==4 ? ROGUE_NODE_REST : ROGUE_NODE_EVENT;
        run->route[run->floor][1]=run->preview[1].kind;
        memset(&run->preview[1].payload,0,sizeof(run->preview[1].payload));
        if(run->preview[1].kind==ROGUE_NODE_EVENT) {
            run->preview[1].payload.event.template_id=ROGUE_EVENT_HOME_RUN;
            run->preview[1].payload.event.timer_frames=600;
#if ROGUE_QA_NATIVE_INTRO == 8
            run->preview[1].payload.event.template_id=ROGUE_EVENT_RACE;
            run->preview[1].payload.event.base_character=run->character;
            run->preview[1].payload.event.native_stage=82;
            run->preview[1].payload.event.timer_frames=60*rogue_race_seconds[run->character];
            run->preview[1].payload.event.native_kit_assist=1;
#endif
#if ROGUE_QA_NATIVE_INTRO == 9
            run->preview[1].payload.event.template_id=ROGUE_EVENT_DASH;
            run->preview[1].payload.event.base_character=run->character;
            run->preview[1].payload.event.native_stage=59;
            run->preview[1].payload.event.timer_frames=90*60;
            run->preview[1].payload.event.native_kit_assist=1;
#endif
#if ROGUE_QA_NATIVE_INTRO == 10
            run->preview[1].payload.event.template_id=ROGUE_EVENT_ESCAPE;
            run->preview[1].payload.event.base_character=run->character;
            run->preview[1].payload.event.native_stage=66;
            run->preview[1].payload.event.timer_frames=40*60;
            run->preview[1].payload.event.native_kit_assist=1;
#endif
#if ROGUE_QA_NATIVE_INTRO == 12
            run->preview[1].payload.event.template_id=ROGUE_EVENT_GAUNTLET;
            run->preview[1].payload.event.timer_frames=0;
            run->preview[1].payload.event.dangerous=1;
            run->preview[1].payload.event.opponents[0]=14;
            run->preview[1].payload.event.opponents[1]=19;
            run->preview[1].payload.event.opponents[2]=3;
            run->preview[1].payload.event.combat_stages[0]=31;
            run->preview[1].payload.event.combat_stages[1]=32;
            run->preview[1].payload.event.combat_stages[2]=8;
#endif
#if ROGUE_QA_NATIVE_INTRO == 11
            run->preview[1].payload.event.template_id=ROGUE_EVENT_SANDBAG;
            run->preview[1].payload.event.base_character=run->character;
            run->preview[1].payload.event.native_stage=84;
            run->preview[1].payload.event.timer_frames=20*60;
#endif
#if ROGUE_QA_NATIVE_INTRO == 7
            run->preview[1].payload.event.template_id=ROGUE_EVENT_TARGETS;
            run->preview[1].payload.event.base_character=run->character;
            run->preview[1].payload.event.native_stage=gm_801647F8(run->character);
            run->preview[1].payload.event.timer_frames=45*60;
            run->preview[1].payload.event.native_kit_assist=1;
#endif
        }
#if ROGUE_QA_NATIVE_INTRO == 6
        /* Two native service previews exercise one shared scene archive,
         * both camera links and zero native demo fighters. */
        run->preview[0].kind=ROGUE_NODE_SHOP;
        run->preview[1].kind=ROGUE_NODE_REST;
        run->route[run->floor][0]=ROGUE_NODE_SHOP;
        run->route[run->floor][1]=ROGUE_NODE_REST;
        memset(&run->preview[0].payload,0,sizeof(run->preview[0].payload));
        memset(&run->preview[1].payload,0,sizeof(run->preview[1].payload));
#endif
        OSReport("[rogue] native_intro_mixed kind=%u\n",run->preview[1].kind);
#endif
        if (ROGUE_LAUNCH_COMBAT) {
            if (ROGUE_LAUNCH_ENCOUNTER) {
                unsigned tier = rogue_recipes[ROGUE_LAUNCH_ENCOUNTER-1].tier;
                run->route[0][0] = tier;
                for (tries = 0; tries < 128; ++tries) {
                    RogueEncounter_Generate(run, tier, &run->preview[0].payload.combat);
                    if (run->preview[0].payload.combat.recipe == ROGUE_LAUNCH_ENCOUNTER) break;
                }
                if (run->preview[0].payload.combat.recipe != ROGUE_LAUNCH_ENCOUNTER)
                    OSPanic(__FILE__, __LINE__, "developer recipe unavailable");
            }
            run->phase = ROGUE_ROUTE;
            RogueRun_ChooseRoute(run, 0);
        }
    }
#endif
#endif
}

void RogueMode_Unload(void)
{
#if ROGUE_DEBUG && ROGUE_QA_CYCLES
    OSReport("[rogue] scene_qa cycles=%u resources=%u active=%d\n", qa_cycles,
        RogueRuntime_Get()->scene_resources, RogueRuntime_IsActive());
#endif
    RogueNativeIntro_End();
    RogueCampScene_End();
    RogueHrcProbe_Reset();
    RogueHrc_Reset();
    RogueTargets_Reset();
    RogueNativeRace_Reset();
    RogueDirector_End();
}

static void enter_css(GameModeState* state)
{
    gmVsMelee_EnterCss(state, &selection, REG_CLASSIC);
}

static void exit_css(GameModeState* state)
{
    gmVsMelee_ExitCss(state, &selection);
    if (css.pending_scene_change == CSSPendingSceneChange_2) return;
    RogueDirector_Start((RogueSeed) OSGetTime(), selection.start.players[run_port].ckind);
    if (!RogueRun_Configure(RogueDirector_Run(),
            selection.start.players[run_port].stocks,
            selection.start.players[run_port].cpu_level,
            selection.start.players[run_port].color))
        OSPanic(__FILE__, __LINE__, "invalid Rogue setup settings");
    plan_native_events();
    OSReport("[rogue] setup stocks=%u difficulty=%u costume=%u run_id=%08x%08x\n",
        RogueDirector_Run()->starting_stocks, RogueDirector_Run()->difficulty,
        RogueDirector_Run()->costume,
        (unsigned)(RogueDirector_Run()->run_id>>32), (unsigned)RogueDirector_Run()->run_id);
#if !ROGUE_QA_MODE
    gm_SetNextGameModeStateId(3);
#else
    gm_SetNextGameModeStateId(1);
#endif
}

static void enter_match(GameModeState* state)
{
    RogueRun* run = RogueDirector_Run();
    unsigned i;
    u64 audio = 0;
    (void) state;
    memset(&result, 0, sizeof(result));
    RogueFighter_ResetMatch();
    encounter_frames = 0;
    gm_SetupRulesDefaults(&match.rules);
    match.rules.match_kind = MatchKind_Stock;
    match.rules.is_stock = true;
    match.rules.is_vs = true;
    match.rules.is_teams = true;
    match.rules.x4_4 = true; /* Retail Stage Clear and bonus-score presentation. */
    match.rules.x18 = RogueScore_Enter();
    match.rules.item_freq = -1;
    match.rules.stkind = stages[run->current.payload.combat.stage - 1];
    for (i = 0; i < GM_MAX_PLAYERS; ++i) {
        PlayerInitData* player = &match.players[i];
        gm_SetupPlayerDefaults(player);
        player->slot = i;
        if (i > run->current.payload.combat.enemy_count) continue;
        player->ckind = i ? run->current.payload.combat.fighters[i - 1] : run->character;
        player->slot_type = i ? Gm_PKind_Cpu : Gm_PKind_Human;
        if (!i) player->sub_color = run_port; /* Retail controller_index input. */
        player->stocks = i ? run->current.payload.combat.stocks[i - 1] : run->starting_stocks + run->stacks[10];
#if (ROGUE_DEBUG && ROGUE_QA_MODE == 4) || ROGUE_QA_MODE == 5
        player->stocks = 99;
#endif
        player->team = i ? 1 : 0;
        player->cpu_level = RogueRun_CpuLevel(run);
        if (!i) player->color = run->costume;
        if (i) {
            player->attack_ratio = run->current.payload.combat.damage / 100.0f;
            player->defense_ratio = 100.0f / run->current.payload.combat.defense;
            player->model_scale = run->current.payload.combat.scale / 100.0f;
            player->damage = run->current.payload.combat.starting_percent;
            player->vs_metal = (run->current.payload.combat.tags & 8) != 0;
            if ((run->current.payload.combat.tags & 1024) && i == 1) {
                player->attack_ratio *= 1.20f;
                player->defense_ratio *= 0.85f;
                player->model_scale *= 1.15f;
            }
        } else {
            player->damage = run->carried_percent;
            player->attack_ratio = 1.0f + run->stacks[14] * 0.05f;
            player->defense_ratio = 1.0f - run->stacks[15] * 0.05f;
        }
        audio |= lbAudioAx_80026E84(player->ckind);
    }
    run->stacks[10] = 0;
    lbAudioAx_80026F2C(24);
    lbAudioAx_8002702C(4, audio);
    lbAudioAx_8002702C(8, lbAudioAx_80026EBC(match.rules.stkind));
    lbAudioAx_80027168();
    gm_LoadAnnouncer();
    OSReport("[rogue] match floor=%u recipe=%u stage=%u enemies=%u\n",
             run->floor, run->current.payload.combat.recipe, run->current.payload.combat.stage, run->current.payload.combat.enemy_count);
}

static void exit_match(GameModeState* state)
{
    int won = result.match_end.outcome == OUTCOME_TEAM_ELIMINATION &&
        result.match_end.player_standings[0].stocks > 0;
    (void) state;
    if(!RogueScore_Finish(won,&result.match_end)) {
        gm_SetNextGameModeStateId(1);
        return;
    }
    RogueSpecialQa_Result(won);
    RogueAerialQa_Result(won);
#if ROGUE_DEBUG && ROGUE_QA_MODE == 2
#if ROGUE_QA_SETTLEMENT
    if(!RogueScore_TestRepeat(won,&result.match_end)) qa_failures++;
#endif
    qa_matches++;
    if (won != (qa_matches < ROGUE_QA_MATCHES)) qa_failures++;
    qa_frames = 0;
    OSReport("[rogue] transition_qa index=%u won=%d failures=%u\n", qa_matches, won, qa_failures);
#endif
    OSReport("[rogue] match_exit outcome=%u won=%d phase=%u floor=%u\n",
        result.match_end.outcome, won, RogueDirector_Run()->phase, RogueDirector_Run()->floor);
#if !ROGUE_QA_MODE || ROGUE_QA_NATIVE_TARGET_ENTRY || ROGUE_QA_NATIVE_RACE_ENTRY || ROGUE_QA_NATIVE_EVENT_CAMP
    gm_SetNextGameModeStateId(RogueDirector_Run()->phase==ROGUE_REWARD || RogueDirector_Run()->phase==ROGUE_ROUTE ? 3 : 1);
#else
    gm_SetNextGameModeStateId(1);
#endif
}

static void enter_native_intro(GameModeState* state)
{
    (void)state;
    native_route_cursor=0;
    native_route_release=1;
    if(!RogueNativeIntro_Prepare(run_port))
        OSPanic(__FILE__,__LINE__,"native intro preflight unsupported descriptor");
}
static void exit_native_intro(GameModeState* state)
{ (void)state; RogueNativeIntro_End(); }

static void enter_camp(GameModeState* state)
{
    (void)state;
    memset(&result,0,sizeof(result));
    RogueCampScene_Prepare(&match,run_port);
}
static void exit_camp(GameModeState* state)
{
    (void)state;
    RogueCampScene_End();
    gm_SetNextGameModeStateId(RogueDirector_Run()->phase==ROGUE_ROUTE ? 3 : 1);
}

static void enter_hrc(GameModeState* state)
{
    (void)state;
    memset(&result,0,sizeof(result));
    RogueHrc_Reset();
    if(!RogueHrc_Prepare(&match,run_port))
        OSPanic(__FILE__,__LINE__,"HRC attempt preflight rejected");
    RogueHrcProbe_Prepare(&match,run_port);
}
static void exit_hrc(GameModeState* state)
{
    (void)state;
    RogueHrcProbe_Finish();
    if(!RogueHrc_Finish()) { gm_SetNextGameModeStateId(1); return; }
    gm_SetNextGameModeStateId(RogueDirector_Run()->phase==ROGUE_REWARD ? 3 : 1);
}

static unsigned event_state(void)
{
    unsigned id=RogueDirector_Run()->current.payload.event.template_id;
    if(id==ROGUE_EVENT_HOME_RUN) return 5;
    if(id==ROGUE_EVENT_TARGETS) return 6;
    if(id==ROGUE_EVENT_RACE) return 7;
    OSPanic(__FILE__,__LINE__,"Event adapter is not registered");
    return 1;
}
static void enter_targets(GameModeState* state)
{
    (void)state;
    memset(&result,0,sizeof(result));
    RogueTargets_Reset();
    if(!RogueTargets_Prepare(&match,run_port))
        OSPanic(__FILE__,__LINE__,"Target attempt preflight rejected");
}
static void exit_targets(GameModeState* state)
{
    (void)state;
    if(!RogueTargets_Finish()) { gm_SetNextGameModeStateId(1); return; }
    gm_SetNextGameModeStateId(RogueDirector_Run()->phase==ROGUE_REWARD ? 3 : 1);
}

static void enter_race(GameModeState* state)
{
    (void)state;
    memset(&result,0,sizeof(result));
    RogueNativeRace_Reset();
    if(!RogueNativeRace_Prepare(&match,run_port))
        OSPanic(__FILE__,__LINE__,"Race attempt preflight rejected");
}
static void exit_race(GameModeState* state)
{
    (void)state;
    if(!RogueNativeRace_Finish()) { gm_SetNextGameModeStateId(1); return; }
    gm_SetNextGameModeStateId(RogueDirector_Run()->phase==ROGUE_REWARD ? 3 : 1);
}

GameModeState RogueMode_States[] = {
#if (ROGUE_DEBUG && ROGUE_QA_MODE) || ROGUE_QA_MODE == 5 || ROGUE_QA_NATIVE_CAMP || ROGUE_QA_EVENT_RELEASE
    { 0, lbDvdPreload_2, 0, NULL, NULL, { GS_ROGUE, NULL, NULL } },
#else
    { 0, lbDvdPreload_3, 0, enter_css, exit_css, { GS_CSS, &css, &css } },
#endif
    { 1, lbDvdPreload_2, 0, NULL, NULL, { GS_ROGUE, NULL, NULL } },
    { 2, lbDvdPreload_2, 0, enter_match, exit_match, { GS_VS, &match, &result } },
    { 3, lbDvdPreload_3, 0, enter_native_intro, exit_native_intro, { GS_INTRO_EASY, &rogue_native_intro, NULL } },
    { 4, lbDvdPreload_2, 0, enter_camp, exit_camp, { GS_VS, &match, &result } },
    { 5, lbDvdPreload_2, 0, enter_hrc, exit_hrc, { GS_VS, &match, &result } },
    { 6, lbDvdPreload_2, 0, enter_targets, exit_targets, { GS_VS, &match, &result } },
    { 7, lbDvdPreload_2, 0, enter_race, exit_race, { GS_VS, &match, &result } },
    { GM_GAMEMODESTATE_TERMINATE }
};

static const char* fighter_names[26] = {
    "Captain Falcon", "Donkey Kong", "Fox", "Mr Game and Watch", "Kirby", "Bowser",
    "Link", "Luigi", "Mario", "Marth", "Mewtwo", "Ness", "Peach", "Pikachu",
    "Ice Climbers", "Jigglypuff", "Samus", "Yoshi", "Zelda", "Sheik", "Falco",
    "Young Link", "Dr Mario", "Roy", "Pichu", "Ganondorf"
};
static const char* node_names[5] = { "MATCH", "ELITE", "BOSS", "SHOP", "REST" };
static const char* stage_names[6] = { "Battlefield", "Final Destination", "Dream Land", "Yoshi Story", "Fountain of Dreams", "Pokemon Stadium" };

static void draw(void)
{
    RogueRun* run = RogueDirector_Run();
    unsigned i, floor = run->floor < 15 ? run->floor : 14;
    char score_text[24];
    const char* phases[] = { "CHOOSE A FIGHTER", "CHOOSE AN UPGRADE", "CHOOSE YOUR ROUTE",
        "BATTLE", "SHOP", "REST", "RUN ENDED", "RUN COMPLETE", "EVENT" };
    if(!native_ui) RogueUi_Update(&ui, run, cursor, build_view || acquire_choice.active);
    else {
        unsigned show=run->phase==ROGUE_REWARD || build_view || acquire_choice.active;
        screen.native->hidden=!show;
        native_overlay_back.native->hidden=!show;
        native_overlay_back.native->box_size_y=(build_view || acquire_choice.active) ? 355 : 150;
        if(!show) return;
    }
    RogueText_Begin(&screen);
    if(RogueHrc_HasError() || RogueTargets_HasError() || RogueNativeRace_HasError()) {
        RogueText_Line(&screen,60,140,"Event interrupted: result was not verified");
        RogueText_Line(&screen,60,185,"No event points, gold or draft were granted.");
        RogueText_Line(&screen,60,230,"START: ABANDON RUN    Event retries disabled");
        RogueText_End(&screen); return;
    }
    if(RogueScore_HasError()) {
        RogueText_Line(&screen,60,140,"Result could not be verified");
        RogueText_Line(&screen,60,185,"A: retry encounter from its checkpoint");
        RogueText_Line(&screen,60,220,"Start: abandon without a payout");
        return;
    }
    if(acquire_choice.active) {
        static const char* slots[4]={"Neutral special", "Side special", "Up special", "Down special"};
        RogueText_Line(&screen,60,native_ui ? 145 : 105,"%s",RogueOffer_Name(acquire_choice.offers.ids[acquire_choice.card]));
        if(native_ui) RogueText_Style(&screen,0.8f,0xFFFFFF);
        RogueText_Line(&screen,60,native_ui ? 180 : 140,"Choose the special slot this upgrade follows");
        if(native_ui) RogueText_Style(&screen,0.68f,0xFFFFFF);
        for(i=0;i<4;++i) {
            const RogueSpecialDef* move=RogueSpecial_Find(run->specials[i]);
            RogueText_Line(&screen,60,native_ui ? 220+i*30 : 190+i*35,"%s %s: %s",choice_cursor==i ? ">" : " ",slots[i],move ? move->name : "Native");
            RogueText_Style(&screen,0.75f,choice_cursor==i ? 0xF6CD36 : 0xFFFFFF);
        }
        RogueText_Line(&screen,60,native_ui ? 350 : 360,"%u GOLD   PRICE %u",run->gold,acquire_choice.price);
        if(native_ui) RogueText_Style(&screen,0.8f,0xFFFFFF);
        RogueText_Line(&screen,60,native_ui ? 385 : 405,"A: CONFIRM SLOT    B: CANCEL");
        if(native_ui) RogueText_Style(&screen,0.7f,0xFFFFFF);
        RogueText_Line(&screen,60,native_ui ? 415 : 435,"Cancel keeps this offer and your gold unchanged.");
        if(native_ui) RogueText_Style(&screen,0.62f,0xFFFFFF);
        if(feedback) {
            RogueText_Line(&screen,60,native_ui ? 445 : 460,"%s",feedback);
            if(native_ui) RogueText_Style(&screen,0.6f,0xFFFFFF);
        }
        RogueText_End(&screen);
        return;
    }
    RoguePoints_Format(run->score,score_text);
    if(!native_ui) {
    for (i = 0; i < 15; ++i) {
        if (i == run->floor) {
            RogueText_Line(&screen, 33+i*39, 5, "NEXT");
            RogueText_Style(&screen, 0.50f, 0xF6CD36);
        }
        RogueText_Line(&screen, 43+i*39, 23, "%u", i+1);
        RogueText_Style(&screen, 0.60f, i == run->floor ? 0x171B30 : 0xE5E9F6);
        if (i % 5 == 4) {
            RogueText_Line(&screen, 33+i*39, 39, "BOSS");
            RogueText_Style(&screen, 0.50f, 0xAAB4CF);
        } else {
            static const char* symbols[6] = { "M", "E", "B", "S", "R", "?" };
            RogueText_Line(&screen, 33+i*39, 39, "%s/%s", symbols[run->route[i][0]], symbols[run->route[i][1]]);
            RogueText_Style(&screen, 0.50f, 0xAAB4CF);
        }
    }
    RogueText_Line(&screen, 24, 61, "ACT %u / 3     FLOOR %u / 15", run->act, floor+1);
    RogueText_Style(&screen, 0.80f, 0xFFFFFF);
    RogueText_Line(&screen, 330, 62, "SCORE %s", score_text);
    RogueText_Style(&screen, 0.55f, 0xDAE2FF);
    RogueText_Line(&screen, 471, 61, "GOLD %u", run->gold);
    RogueText_Style(&screen, 0.85f, 0xF6CD36);
    RogueText_Line(&screen, 212, 102, "%s", build_view ? "CURRENT BUILD" : phases[run->phase]);
    RogueText_Style(&screen, 0.85f, 0xDAE2FF);
    }
    if (build_view) {
        unsigned line = 0;
        const char* slots[4] = { "Neutral", "Side", "Up", "Down" };
        const char* airs[5] = { "Nair", "Fair", "Bair", "Uair", "Dair" };
        for (i = 0; i < 4; ++i) {
            const RogueSpecialDef* special = RogueSpecial_Find(run->specials[i]);
            RogueText_Line(&screen, 32, 133+i*23, "%s: %s", slots[i], special ? special->name : "Native");
            RogueText_Style(&screen, 0.8f, 0xF6CD36);
        }
        for (i = 0; i < 5; ++i) {
            const RogueAerialDef* aerial = RogueAerial_Find(run->aerials[i]);
            RogueText_Line(&screen, 338, 133+i*20, "%s: %s", airs[i], aerial ? fighter_names[aerial->character] : "Native");
            RogueText_Style(&screen, 0.70f, 0xDAE2FF);
        }
        for (i = 0; i < ROGUE_UPGRADES; ++i) if (run->stacks[i]) {
            RogueText_Line(&screen, 32 + (line / 8) * 198, 240 + (line % 8) * 19,
                "%s x%u", RogueUpgrade_Find(RogueUpgrade_StackId(i))->name, run->stacks[i]);
            RogueText_Style(&screen, 0.7f, 0xDFE5F7);
            line++;
        }
    } else {
        if (run->phase == ROGUE_REWARD || run->phase == ROGUE_SHOP) {
            RogueOfferState* offers = run->phase == ROGUE_SHOP ? &run->camp.general : &run->reward;
            for (i = 0; i < 3; ++i) {
                unsigned id = offers->ids[i], x = 20+i*204;
                RogueOfferRef ref = RogueOffer_Decode(id);
                const char* icon = ref.kind == ROGUE_OFFER_GOLD ? "G" : ref.kind == ROGUE_OFFER_AERIAL ? "A" : ref.kind == ROGUE_OFFER_SPECIAL ? "B" : "+";
                RogueText_Line(&screen, x+21, 146, "%s", icon);
                RogueText_Style(&screen, 1.2f, 0xF6CD36);
                RogueText_Line(&screen, x+49, 144, "%s", RogueOffer_Name(id));
                RogueText_Style(&screen, 0.53f, cursor == i ? 0xF6CD36 : 0xFFFFFF);
                if(ref.kind==ROGUE_OFFER_UPGRADE) {
                    static const char* rarities[3]={"COMMON","UNCOMMON","RARE"};
                    RogueText_Line(&screen,x+49,154,"PASSIVE / %s",rarities[RogueUpgrade_Find(ref.stable_id)->rarity]);
                } else RogueText_Line(&screen, x+49, 154, "%s", ref.kind == ROGUE_OFFER_GOLD ? "POOL EXHAUSTED" : ref.kind == ROGUE_OFFER_AERIAL ? "AERIAL / COMMON" : "SPECIAL / COMMON");
                RogueText_Style(&screen, 0.50f, 0xAEB9D5);
                if (ref.kind == ROGUE_OFFER_AERIAL) {
                    const RogueAerialDef* move = RogueAerial_Find(ref.stable_id);
                    const RogueAerialDef* previous = RogueAerial_Find(run->aerials[move->slot]);
                    RogueText_Line(&screen, x+12, 183, "%s", RogueOffer_Description(id));
                    RogueText_Style(&screen, 0.65f, 0xBAC3E0);
                    RogueText_Line(&screen, x+12, 199, "%s: %s", offers->sold[i] ? "Equipped" : "Replaces", previous ? fighter_names[previous->character] : "Native");
                    RogueText_Style(&screen, 0.52f, 0xAEB9D5);
                } else if(native_ui && ref.kind==ROGUE_OFFER_SPECIAL) {
                    const RogueSpecialDef* move=RogueSpecial_Find(ref.stable_id);
                    const RogueSpecialDef* previous=RogueSpecial_Find(run->specials[move->slot]);
                    RogueText_Line(&screen,x+12,180,"From: %s",fighter_names[move->character]);
                    RogueText_Style(&screen,0.52f,0xFFFFFF);
                    RogueText_Line(&screen,x+12,196,"%s",RogueOffer_Description(id));
                    RogueText_Style(&screen,0.52f,0xBAC3E0);
                    RogueText_Line(&screen,x+12,212,"Replaces: %s",previous ? previous->name : "Native");
                    RogueText_Style(&screen,0.48f,0xBAC3E0);
                } else RogueText_Wrap(&screen, x+12, 183, native_ui ? 20 : 25, RogueOffer_Description(id));
                if (run->phase == ROGUE_SHOP) {
                    if (offers->sold[i]) RogueText_Line(&screen, x+12, 217, "SOLD");
                    else RogueText_Line(&screen, x+12, 217, "%u GOLD", RogueOffer_Price(run,id));
                    RogueText_Style(&screen, 0.65f, offers->sold[i] ? 0x9299AA : run->gold < RogueOffer_Price(run,id) ? 0xD36B72 : 0xF6CD36);
                } else {
                    RogueText_Line(&screen, x+12, native_ui ? 232 : 217, "FREE CHOICE");
                    RogueText_Style(&screen, 0.60f, 0xF6CD36);
                }
            }
        } else if (run->phase == ROGUE_ROUTE) {
            for (i = 0; i < 2; ++i) {
                unsigned recipe = RogueNode_Combat(&run->preview[i])->recipe;
                RogueText_Line(&screen, 36+i*320, 146, "%s %s", cursor == i ? ">" : " ", node_names[run->route[floor][i]]);
                RogueText_Style(&screen, 1.0f, cursor == i ? 0xF6CD36 : 0xFFFFFF);
                RogueText_Wrap(&screen, 36+i*320, 190, 25, recipe ? rogue_recipes[recipe-1].name : "Recover and prepare");
            }
        } else {
            RogueText_Line(&screen, 42, 150, "%s", run->phase == ROGUE_DEAD && run->death_reason == 2 ? "RUN ABANDONED" : phases[run->phase]);
            if (run->phase == ROGUE_REST)
                RogueText_Line(&screen, 42, 187, "DAMAGE %u%%   RECOVER %u%%   START: REST", run->carried_percent, 20 + 10 * run->stacks[13]);
            else RogueText_Line(&screen, 42, 187, "SCORE %s   FIGHTS %u", score_text, run->combat_nodes_won);
            RogueText_Style(&screen, 0.8f, 0xBAC3E0);
        }
        if(!native_ui) {
            unsigned choice = run->phase == ROGUE_ROUTE ? cursor % 2 : 0;
            const RogueEncounter* encounter = (run->phase == ROGUE_DEAD || run->phase == ROGUE_COMPLETE) ? RogueNode_Combat(&run->current) : RogueNode_Combat(&run->preview[choice]);
            RogueText_Line(&screen, 36, 372, "%s", fighter_names[run->character]);
            RogueText_Style(&screen, 1.0f, 0xFFFFFF);
            RogueText_Line(&screen, 36, 268, "YOUR FIGHTER");
            RogueText_Style(&screen, 0.65f, 0x9DAED5);
            RogueText_Line(&screen, 302, 325, "VS");
            RogueText_Style(&screen, 1.1f, 0xE84956);
            RogueText_Line(&screen, 354, 267, "%s", encounter->recipe ? rogue_recipes[encounter->recipe-1].name : node_names[run->route[floor][choice]]);
            RogueText_Style(&screen, 0.9f, 0xFFFFFF);
            for (i = 0; i < encounter->enemy_count; ++i) {
                RogueText_Line(&screen, 350+i*84, 366, "%s", fighter_names[encounter->fighters[i]]);
                RogueText_Style(&screen, 0.55f, 0xCBD3EC);
            }
            if (encounter->stage) {
                RogueText_Line(&screen, 354, 377, "%s / THREAT %u", stage_names[encounter->stage-1], RogueEncounter_Threat(encounter));
                RogueText_Style(&screen, 0.65f, 0xBBA0E4);
                RogueText_Line(&screen, 354, 393, "DMG %u%%  GUARD %u%%  SPEED %u%%", encounter->damage, encounter->defense, encounter->speed);
                RogueText_Style(&screen, 0.52f, 0xCBD3EC);
                RogueText_Line(&screen, 354, 408, "%s", rogue_recipes[encounter->recipe-1].rule);
                RogueText_Style(&screen, 0.58f, 0xF6CD36);
            }
        }
    }
    if(native_ui) {
        if(build_view) RogueText_Line(&screen,24,450,"B: CLOSE BUILD");
        else RogueText_Line(&screen,24,250,"A: CHOOSE   B: BUILD   X: REROLL %u GOLD   GOLD %u",
            RogueRun_RerollCost(run,0),run->gold);
        RogueText_Style(&screen,0.55f,0xF6CD36);
        if(feedback) {
            RogueText_Line(&screen,24,265,"%s",feedback);
            RogueText_Style(&screen,0.5f,0xFFFFFF);
        }
        RogueText_End(&screen); return;
    }
    RogueText_Line(&screen, 24, 433, "B: %s   A: SELECT", build_view ? "CLOSE BUILD" : "CURRENT BUILD");
    RogueText_Style(&screen, 0.8f, 0xDEE5FB);
    if (run->phase == ROGUE_REWARD || run->phase == ROGUE_SHOP) {
        RogueText_Line(&screen, 389, 433, "X: REROLL  %u GOLD", RogueRun_RerollCost(run, run->phase == ROGUE_SHOP));
        RogueText_Style(&screen, 0.75f, run->gold < RogueRun_RerollCost(run, run->phase == ROGUE_SHOP) ? 0xD36B72 : 0xF6CD36);
    }
    if (feedback) RogueText_Line(&screen, 24, 447, "%s", feedback);
    else if (run->phase == ROGUE_DEAD || run->phase == ROGUE_COMPLETE)
        RogueText_Line(&screen, 24, 447, "A: RETRY    START: MAIN MENU    SEED %08X%08X", (unsigned) (run->seed >> 32), (unsigned) run->seed);
    else RogueText_Line(&screen, 24, 447, "LEFT / RIGHT: CHOOSE    START: CONTINUE    SEED %08X%08X", (unsigned) (run->seed >> 32), (unsigned) run->seed);
    RogueText_Style(&screen, 0.6f, 0x8A99BA);
    RogueText_End(&screen);
}

void RogueMode_NativeOverlayEnter(void)
{
    native_ui=1;
    cursor=build_view=choice_cursor=0;
    choice_release=1;
    feedback=NULL;
    RogueOffer_ChoiceCancel(&acquire_choice);
    RogueText_CreateSized(&native_overlay_back,4,0,128);
    RogueText_Begin(&native_overlay_back); RogueText_End(&native_overlay_back);
    native_overlay_back.native->font_size.x=native_overlay_back.native->font_size.y=1;
    native_overlay_back.native->pos_y=125;
    native_overlay_back.native->box_size_x=640;
    native_overlay_back.native->box_size_y=150;
    native_overlay_back.native->bg_color.r=native_overlay_back.native->bg_color.g=native_overlay_back.native->bg_color.b=10;
    native_overlay_back.native->bg_color.a=235;
    RogueText_CreateSized(&screen,4,0,4096);
#if ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE
    {
        RogueRun* run=RogueDirector_Run();
        const RogueRewardSource* source=&run->reward_source;
        if(run->phase==ROGUE_REWARD)
            OSReport("[rogue] reward_view kind=%u node=%u receipt=%u verified=%u donors=%u donor0=%u donor1=%u donor2=%u card0=%u card1=%u card2=%u gold=%u history=%u resources=%u\n",
                source->kind,source->node_id,source->receipt_id,source->provenance_verified,
                source->donor_count,source->donors[0],source->donors[1],source->donors[2],
                run->reward.ids[0],run->reward.ids[1],run->reward.ids[2],run->gold,run->history_count,
                RogueRuntime_Get()->match_resources);
    }
#endif
    draw();
}
void RogueMode_NativeOverlayEnd(void)
{
    if(!native_ui) return;
    /* SIS scene teardown frees these allocations after handles are dropped. */
    memset(&screen,0,sizeof(screen));
    memset(&native_overlay_back,0,sizeof(native_overlay_back));
    native_ui=0;
    RogueOffer_ChoiceCancel(&acquire_choice);
}

void RogueMode_Enter(void* data)
{
    (void) data;
    cursor = build_view = choice_cursor = choice_release = 0;
    RogueOffer_ChoiceCancel(&acquire_choice);
    feedback = NULL;
#if ROGUE_DEBUG && ROGUE_QA_CYCLES
    RogueDirector_Start(ROGUE_FIXTURE_SEED + qa_cycles, CKind_Fox);
    if (qa_cycles % 6 == 1) RogueRun_Shop(RogueDirector_Run());
    else if (qa_cycles % 6 == 2) RogueRun_ChooseUpgrade(RogueDirector_Run(),0);
    else if (qa_cycles % 6 == 3) { RogueDirector_Run()->phase=ROGUE_REST; RogueDirector_Run()->carried_percent=60; }
    else if (qa_cycles % 6 == 4) RogueDirector_Run()->phase = ROGUE_COMPLETE;
    else if (qa_cycles % 6 == 5) RogueDirector_Run()->phase = ROGUE_DEAD;
#endif
    RogueUi_Create(&ui);
#if ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE
    {
        RogueRun* run=RogueDirector_Run();
        if(run->phase==ROGUE_REWARD) {
            const RogueRewardSource* source=&run->reward_source;
            OSReport("[rogue] reward_view kind=%u node=%u receipt=%u verified=%u donors=%u donor0=%u donor1=%u donor2=%u card0=%u card1=%u card2=%u gold=%u history=%u resources=%u\n",
                source->kind,source->node_id,source->receipt_id,source->provenance_verified,
                source->donor_count,source->donors[0],source->donors[1],source->donors[2],
                run->reward.ids[0],run->reward.ids[1],run->reward.ids[2],run->gold,run->history_count,
                RogueRuntime_Get()->match_resources);
        }
    }
#endif
    draw();
}

void RogueMode_Exit(void* data)
{
    (void) data;
    RogueOffer_ChoiceCancel(&acquire_choice);
    choice_release=0;
    RogueUi_Destroy(&ui);
}

static int ui_vendor_action(RogueRun* run, unsigned slot, int reroll)
{
    int accepted;
#if ROGUE_DEBUG
    unsigned gold=run->gold, history=run->history_count;
    unsigned id=run->camp.general.ids[slot];
    unsigned price=reroll ? RogueRun_RerollCost(run,1) : run->camp.general_prices[slot];
#endif
    accepted=reroll ? RogueRun_Reroll(run,1) : RogueRun_Buy(run,slot);
#if ROGUE_DEBUG
    OSReport("[rogue] vendor_action reroll=%u accepted=%u slot=%u id=%u after_id=%u price=%u before=%u after=%u sold=%u history_before=%u history_after=%u resources=%u\n",
        reroll!=0,accepted,slot,id,run->camp.general.ids[slot],price,gold,run->gold,
        run->camp.general.sold[slot],history,run->history_count,RogueRuntime_Get()->match_resources);
#endif
    return accepted;
}

/* Only value-owned preview descriptors cross this selection boundary. */
void RogueMode_NativeRouteFrame(void)
{
    RogueRun* run=RogueDirector_Run();
    u64 input=mn_80229624(run_port);
    if(run->phase!=ROGUE_ROUTE || build_view || acquire_choice.active) {
        RogueMode_Frame(); return;
    }
    RogueNativeIntro_Focus(native_route_cursor);
    if(native_route_release || choice_release) {
        if(!HSD_PadMasterStatus[run_port].button && !input) native_route_release=choice_release=0;
        return;
    }
    if(input & MenuInput_Back) { build_view=1; choice_release=1; draw(); return; }
    if(input & MenuInput_Left) { native_route_cursor=0; sfxMove(); }
    else if(input & MenuInput_Right) { native_route_cursor=1; sfxMove(); }
    RogueNativeIntro_Focus(native_route_cursor);
    if(input & MenuInput_XButton) {
        RogueNodeDesc* node=&run->preview[native_route_cursor];
        if(node->kind==ROGUE_NODE_EVENT && RogueRun_SelectEventKit(run,native_route_cursor,
                !node->payload.event.native_kit_assist)) {
            native_route_release=1;RogueNativeIntro_KitChanged(native_route_cursor);sfxMove();
            OSReport("[rogue] event_kit_select node=%u assist=%u resources=%u\n",
                node->node_id,node->payload.event.native_kit_assist,RogueRuntime_Get()->match_resources);
        }
        return;
    }
    if(input & MenuInput_AButton) {
        unsigned node=run->preview[native_route_cursor].node_id;
        if(!RogueRun_ChooseRoute(run,native_route_cursor)) return;
        sfxForward(); native_route_release=1;
        OSReport("[rogue] native_route_commit side=%u node=%u phase=%u\n",
            native_route_cursor,node,run->phase);
        if(run->phase==ROGUE_FIGHT) RogueNativeIntro_Commit(native_route_cursor);
        else change_state(run->phase==ROGUE_EVENT ? event_state() : run->phase==ROGUE_SHOP || run->phase==ROGUE_REST ? 4 : 1);
    }
}

void RogueMode_Frame(void)
{
    RogueRun* run = RogueDirector_Run();
    u64 input = mn_80229624(run_port);
    unsigned count = run->phase == ROGUE_ROUTE ? 2 : 3;
    /* Native menu aggregation prioritizes A over B on a simultaneous edge.
     * Preserve cancel for the owned overlay before confirm is considered. */
    if(native_ui && (HSD_PadMasterStatus[run_port].trigger & HSD_PAD_B))
        input |= MenuInput_Back;
#if ROGUE_QA_NATIVE_CAMP
    if(run->phase==ROGUE_SHOP || run->phase==ROGUE_REST) { change_state(4); return; }
#endif
    if(RogueHrc_HasError() || RogueTargets_HasError() || RogueNativeRace_HasError()) {
        if(input & MenuInput_StartButton) { RogueHrc_Abandon(); RogueTargets_Abandon(); RogueNativeRace_Abandon(); }
        draw(); return;
    }
    if(run->phase==ROGUE_EVENT) { change_state(event_state()); return; }
#if ROGUE_DEBUG && ROGUE_QA_NATIVE_INTRO
    if(!ROGUE_QA_NATIVE_DEPARTURE || !native_ui) { change_state(3); return; }
#endif
    if(RogueScore_HasError()) {
        if(input & MenuInput_AButton) { RogueScore_Retry(); change_state(2); return; }
        if(input & MenuInput_StartButton) RogueScore_Abandon();
        draw(); return;
    }
#if ROGUE_DEBUG && ROGUE_QA_CYCLES
    if (++qa_frames == 2) { build_view = 1; draw(); }
    if (qa_frames == 3) { build_view = 0; draw(); }
    if (qa_frames >= 4) {
        qa_frames = 0;
        if (++qa_cycles >= ROGUE_QA_CYCLES) {
            gm_ChangeGameModeAfterCurrentScene(GM_MENU);
            gm_801A4B60();
        } else change_state(0);
    }
    return;
#endif
    { int qa = RogueAerialQa_Progression(); if (qa) { if (qa == 2) change_state(2); return; } }
    { int qa = RogueSpecialQa_Progression(); if (qa) { if (qa == 2) change_state(2); return; } }
    /* The controlled fixture exercises native result computation, not player skill. */
#if ROGUE_DEBUG && ROGUE_QA_MODE == 2
    if (++qa_frames < 60) return;
    qa_frames = 0;
    if (qa_matches >= ROGUE_QA_MATCHES || qa_failures) {
        OSReport("[rogue] match_qa_complete transitions=%u failures=%u phase=%u\n", qa_matches, qa_failures, run->phase);
        gm_ChangeGameModeAfterCurrentScene(GM_MENU);
        gm_801A4B60();
        return;
    }
    if (run->phase == ROGUE_COMPLETE) RogueDirector_Start(ROGUE_FIXTURE_SEED + qa_matches, CKind_Fox);
    if (run->phase == ROGUE_SHOP || run->phase == ROGUE_REST) RogueRun_LeaveService(run);
    if (run->phase == ROGUE_REWARD) RogueRun_ChooseUpgrade(run, 0);
    if (run->phase == ROGUE_ROUTE) RogueRun_ChooseRoute(run, 0);
    if (run->phase == ROGUE_FIGHT) {
        memset(run->specials, 0, sizeof(run->specials));
        run->specials[ROGUE_ABILITY_NEUTRAL] = Rogue_AbilityForOpponent(CKind_Falco, ROGUE_ABILITY_NEUTRAL);
        change_state(2);
    } else draw();
    return;
#endif
    if(choice_release) {
        if(!HSD_PadMasterStatus[run_port].button && !input) choice_release=0;
        return;
    }
    if(acquire_choice.active) {
        if(input & MenuInput_Back) {
            RogueOffer_ChoiceCancel(&acquire_choice); choice_release=1;
            feedback="Choice canceled"; sfxBack();
#if ROGUE_DEBUG
            OSReport("[rogue] choice_cancel gold=%u history=%u phase=%u resources=%u\n",
                run->gold,run->history_count,run->phase,RogueRuntime_Get()->match_resources);
#endif
        } else if(input & MenuInput_AButton) {
            int accepted=RogueOffer_ChoiceCommit(run,&acquire_choice,choice_cursor);
            choice_release=1;
            feedback=accepted ? "Added to your build" : "Offer changed; cancel and choose again";
#if ROGUE_DEBUG
            OSReport("[rogue] choice_commit accepted=%u slot=%u gold=%u history=%u phase=%u resources=%u\n",
                accepted,choice_cursor,run->gold,run->history_count,run->phase,RogueRuntime_Get()->match_resources);
#endif
        } else if(input & (MenuInput_Up | MenuInput_Left)) {
            choice_cursor=(choice_cursor+3)%4; sfxMove();
        } else if(input & (MenuInput_Down | MenuInput_Right)) {
            choice_cursor=(choice_cursor+1)%4; sfxMove();
        }
        if(input) draw();
        return;
    }
    if (run->phase == ROGUE_FIGHT) { change_state(2); return; }
#if !ROGUE_QA_MODE || ROGUE_QA_NATIVE_TARGET_ENTRY || ROGUE_QA_NATIVE_RACE_ENTRY || ROGUE_QA_NATIVE_EVENT_CAMP
    if (!native_ui && run->phase == ROGUE_ROUTE) { change_state(3); return; }
#endif
    if (!input) return;
    if(native_ui && (input & MenuInput_Back)) {
        build_view=!build_view; choice_release=1; sfxBack(); draw(); return;
    }
    feedback = NULL;
#if ROGUE_DEBUG
    OSReport("[rogue] ui_input phase=%u mask=%x cursor=%u build=%u\n", run->phase, (unsigned) input, cursor, build_view);
#endif
    if (input & MenuInput_Back) { build_view = !build_view; sfxBack(); }
    if (!build_view) {
        if (input & (MenuInput_Up | MenuInput_Left)) { cursor = (cursor + count - 1) % count; sfxMove(); }
        if (input & (MenuInput_Down | MenuInput_Right)) { cursor = (cursor + 1) % count; sfxMove(); }
        if ((input & MenuInput_XButton) && (run->phase == ROGUE_REWARD || run->phase == ROGUE_SHOP))
            feedback = (run->phase==ROGUE_SHOP ? ui_vendor_action(run,cursor,1) : RogueRun_Reroll(run,0)) ? "New offers ready" : "Reroll unavailable: check gold or sold slots";
        if (input & MenuInput_AButton) {
            sfxForward();
            if(RogueOffer_ChoiceBegin(run,cursor,&acquire_choice)) {
                choice_cursor=0; choice_release=1;
#if ROGUE_DEBUG
                OSReport("[rogue] choice_open gold=%u history=%u phase=%u resources=%u\n",
                    run->gold,run->history_count,run->phase,RogueRuntime_Get()->match_resources);
#endif
                draw(); return;
            }
            if (run->phase == ROGUE_REWARD) {
#if ROGUE_DEBUG
                unsigned id=run->reward.ids[cursor], before=run->gold;
                unsigned history=run->history_count;
                RogueOfferRef ref=RogueOffer_Decode(id);
                const RogueSpecialDef* special=ref.kind==ROGUE_OFFER_SPECIAL ? RogueSpecial_Find(ref.stable_id) : NULL;
#endif
                int accepted=RogueRun_ChooseUpgrade(run,cursor);
                if(accepted) choice_release=1;
#if ROGUE_DEBUG
                OSReport("[rogue] reward_claim accepted=%u card=%u id=%u before=%u after=%u history_before=%u history_after=%u phase=%u equipped=%u resources=%u\n",
                    accepted,cursor,id,before,run->gold,history,run->history_count,run->phase,
                    special ? run->specials[special->slot] : 0,RogueRuntime_Get()->match_resources);
#endif
            }
            else if (run->phase == ROGUE_ROUTE) RogueRun_ChooseRoute(run, cursor % 2);
            else if (run->phase == ROGUE_SHOP)
                feedback = ui_vendor_action(run, cursor,0) ? "Added to your build" : "Offer unavailable: check gold or sold status";
            else if (run->phase == ROGUE_DEAD || run->phase == ROGUE_COMPLETE) {
                RogueDirector_Start((RogueSeed) OSGetTime(), run->character);
                plan_native_events();
                cursor = 0;
            }
        }
        if (input & MenuInput_StartButton) {
            if (run->phase == ROGUE_SHOP || run->phase == ROGUE_REST) RogueRun_LeaveService(run);
            else if (run->phase == ROGUE_DEAD || run->phase == ROGUE_COMPLETE) {
                gm_ChangeGameModeAfterCurrentScene(GM_MENU);
                gm_801A4B60();
                return;
            }
        }
        if (run->phase == ROGUE_FIGHT) { change_state(2); return; }
    }
#if !ROGUE_QA_MODE
    if(!native_ui && run->phase==ROGUE_ROUTE) { change_state(3); return; }
#endif
    draw();
}

bool RogueMode_CharacterUnlocked(u8 kind)
{
    if (gm_GetCurrentGameMode() == GM_ROGUE) return kind < RogueCapabilities_Get()->roster_count;
    return gm_IsCKindUnlocked(kind);
}

void RogueMode_MatchFrame(void)
{
#if ROGUE_DEBUG && !ROGUE_QA_MODE
    if(RogueRuntime_IsActive() && RogueRuntime_Get()->scene==GS_VS && gm_GetDbPauseFlag(1)) {
        static unsigned pause_observe_frames;
        if(++pause_observe_frames%15==1) {
            VsSceneController* controller=gmVs_GetSceneController();
            OSReport("[rogue] pause_input tick=%u held=%x master=%x quit=%u lock=%u timer=%u\n",
                pause_observe_frames,(unsigned)gm_GetButtonsPressed(run_port),
                HSD_PadMasterStatus[run_port].button,controller->start.x3_4,
                controller->state.unk_3,controller->state.pause_timer);
        }
    }
#endif
    RogueHrc_Frame();
    RogueTargets_Frame();
    RogueNativeRace_Frame();
    if (gm_GetDbPauseFlag(1) || gm_GetDbPauseFlag(2)) return;
    if(RogueCampScene_Active()) { RogueCampScene_Frame(); return; }
    /* Service and event adapters own their frames; the combat union is invalid there. */
    if(RogueRuntime_IsActive() && RogueDirector_Run()->phase!=ROGUE_FIGHT) return;
    RogueSpecialQa_Frame();
    RogueAerialQa_Frame();
    if (RogueRuntime_IsActive() && RogueRuntime_Get()->scene == GS_VS) {
        RogueRun* run = RogueDirector_Run();
        ++encounter_frames;
#if (ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE) && (ROGUE_QA_NATIVE_TARGET_ENTRY || ROGUE_QA_NATIVE_RACE_ENTRY || ROGUE_QA_NATIVE_HRC)
        if(encounter_frames==1) {
            Fighter_GObj* object=Player_GetEntity(0);
            Fighter* observed=object ? object->user_data : NULL;
            OSReport("[rogue] event_combat_start expected_damage=%u native_damage=%d fighter_damage=%d stocks=%d motion=%u port=%u present=%u\n",
                run->carried_percent,Player_GetDamage(0),observed ? (int)observed->dmg.x1830_percent : -1,
                Player_GetStocks(0),observed ? observed->motion_id : 0,run_port,observed!=NULL);
        }
#endif
#if (ROGUE_DEBUG && (!ROGUE_QA_MODE || ROGUE_QA_NATIVE_TARGET_ENTRY || ROGUE_QA_NATIVE_RACE_ENTRY || ROGUE_QA_NATIVE_EVENT_CAMP)) || (ROGUE_QA_EVENT_RELEASE && (ROGUE_QA_NATIVE_TARGET_ENTRY || ROGUE_QA_NATIVE_RACE_ENTRY || ROGUE_QA_NATIVE_EVENT_CAMP))
        /* Read-only telemetry for controller-driven native acceptance. This
         * never changes inputs, fighter state, stocks, or result ownership. */
        if(encounter_frames%15==0) {
            unsigned actor;
            for(actor=0;actor<=run->current.payload.combat.enemy_count;++actor) {
                Fighter_GObj* object=Player_GetEntity(actor);
                Fighter* observed=object ? object->user_data : NULL;
                if(observed) OSReport("[rogue] observe frame=%u actor=%u x=%d y=%d facing=%d air=%u damage=%d stocks=%d motion=%u\n",
                    encounter_frames,actor,(int)observed->cur_pos.x,(int)observed->cur_pos.y,
                    (int)observed->facing_dir,observed->ground_or_air,
                    Player_GetDamage(actor),Player_GetStocks(actor),observed->motion_id);
            }
        }
#endif
#if ROGUE_DEBUG && ROGUE_QA_PASSIVES
        if (encounter_frames == 150) RoguePassiveQa_Check();
#endif
        if ((run->current.payload.combat.tags & 64) && encounter_frames % 180 == 0) {
            Fighter_GObj* entity = Player_GetEntity(1);
            Fighter* fighter = entity ? entity->user_data : NULL;
            if (fighter && fighter->ground_or_air == GA_Ground &&
                fighter->motion_id >= ftCo_MS_Wait && fighter->motion_id <= ftCo_MS_WalkFast &&
                ftData_SpecialN[fighter->kind])
                ftData_SpecialN[fighter->kind](entity);
        }
    }
#if ROGUE_DEBUG && ROGUE_QA_MODE == 2
    unsigned i;
    if (!RogueRuntime_IsActive() || RogueRuntime_Get()->scene != GS_VS) return;
    ++qa_frames;
    if (qa_frames == 180 || qa_frames == 360) {
        Fighter_GObj* entity = Player_GetEntity(0);
        Fighter* fighter = entity ? entity->user_data : NULL;
        if (fighter) {
            if (qa_frames == 360) {
                fighter->cur_pos.y += 30.0f;
                ftCommon_8007D5D4(fighter);
                ftCo_Fall_Enter(entity);
            }
            OSReport("[rogue] special_qa id=%u air=%d entered=%d\n",
                RogueDirector_Run()->specials[0], qa_frames == 360,
                Rogue_TrySpecial(entity, ROGUE_ABILITY_NEUTRAL, qa_frames == 360));
        }
    }
    if (qa_frames == 450) {
        unsigned present = 0;
        for (i = 0; i <= RogueDirector_Run()->current.payload.combat.enemy_count; ++i)
            if (Player_GetEntity(i)) present++;
        OSReport("[rogue] match_qa frames=450 fighters=%u expected=%u\n", present,
            RogueDirector_Run()->current.payload.combat.enemy_count + 1);
        if (present != RogueDirector_Run()->current.payload.combat.enemy_count + 1) qa_failures++;
        if (qa_matches + 1 == ROGUE_QA_MATCHES) Player_SetStocks(0, 0);
        else for (i = 1; i <= RogueDirector_Run()->current.payload.combat.enemy_count; ++i) Player_SetStocks(i, 0);
    }
#endif
}
