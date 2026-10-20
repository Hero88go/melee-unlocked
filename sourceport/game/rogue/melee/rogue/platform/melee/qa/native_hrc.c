#include "native_hrc.h"
#include "../debug_launch.h"
#include "../melee_fighter.h"
#include "../../../director/rogue_director.h"
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gm_1B03.h>
#include <melee/gm/gmregclear.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/mn/types.h>
#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <melee/lb/lbaudio_ax.h>
#include <dolphin/os.h>

#if (ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE) && ROGUE_QA_NATIVE_HRC
#include "event_records.h"
static unsigned persistent_records[ROGUE_EVENT_RECORD_FIELDS];
static unsigned active, finalized, last_second, last_ready;
static unsigned last_position_state;
static int personal_before[27], session_before[27];
#endif

void RogueHrcProbe_Reset(void)
{
#if (ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE) && ROGUE_QA_NATIVE_HRC
    active=finalized=0;
#endif
}

void RogueHrcProbe_Observe(int raw, int display, unsigned us, unsigned ready,
    unsigned mode)
{
#if (ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE) && ROGUE_QA_NATIVE_HRC
    unsigned second;
    Fighter_GObj* object;
    Fighter* player;
    if(!active || !RogueRuntime_IsActive()) return;
    second=gm_8016AEEC();
    object=Player_GetEntity(0); player=object ? object->user_data : NULL;
    if(player) {
        unsigned state=(player->ground_or_air ? 1U : 0U) |
            (player->cur_pos.y<35.0f ? 2U : 0U);
        if(state!=last_position_state) {
            last_position_state=state;
            OSReport("[rogue] hrc_probe_position sec=%u sub=%u x=%d y=%d air=%u stocks=%d\n",
                second,gm_8016AEFC(),(int)player->cur_pos.x,
                (int)player->cur_pos.y,player->ground_or_air!=0,Player_GetStocks(0));
        }
    }
    if(second==last_second && ready==last_ready) return;
    last_second=second; last_ready=ready;
    object=Player_GetEntity(0); player=object ? object->user_data : NULL;
    OSReport("[rogue] hrc_probe_observe sec=%u sub=%u raw=%d display=%d us=%u ready=%u mode=%u x=%d motion=%u item=%u y=%d stocks=%d\n",
        second,gm_8016AEFC(),raw,display,us,ready,mode,
        player ? (int)player->cur_pos.x : 0,player ? player->motion_id : 0,
        player && player->item_gobj ? 1 : 0,
        player ? (int)player->cur_pos.y : 0,Player_GetStocks(0));
#else
    (void)raw; (void)display; (void)us; (void)ready; (void)mode;
#endif
}

void RogueHrcProbe_Finalized(unsigned mode, int raw)
{
#if (ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE) && ROGUE_QA_NATIVE_HRC
    if(!active || !RogueRuntime_IsActive()) return;
    ++finalized;
    OSReport("[rogue] hrc_probe_finalized count=%u mode=%u raw=%d\n",finalized,mode,raw);
#else
    (void)mode; (void)raw;
#endif
}

void RogueHrcProbe_Prepare(StartMeleeData* match, unsigned port)
{
#if (ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE) && ROGUE_QA_NATIVE_HRC
    RogueRun* run=RogueDirector_Run();
    unsigned i;
    active=1; finalized=0; last_second=last_ready=~0U;
    last_position_state=~0U;
    RogueEventRecords_Read(persistent_records);
    for(i=0;i<27;++i) {
        unsigned index=gm_CKindToSelKind(i);
        personal_before[i]=*gmMainLib_8015D06C(index);
        session_before[i]=*gmMainLib_8015D084(index);
    }
    OSReport("[rogue] hrc_probe_prepare stage=84 timer=10 player=%u port=%u sandbag=%u reward=0\n",
        run->character,port,match->players[1].ckind);
#else
    (void)match; (void)port;
#endif
}

void RogueHrcProbe_Finish(void)
{
#if (ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE) && ROGUE_QA_NATIVE_HRC
    unsigned i,changed=0;
    for(i=0;i<27;++i) {
        unsigned index=gm_CKindToSelKind(i);
        changed+=(personal_before[i]!=*gmMainLib_8015D06C(index));
        changed+=(session_before[i]!=*gmMainLib_8015D084(index));
    }
    OSReport("[rogue] hrc_probe_records checked=54 changed=%u finalized=%u\n",changed,finalized);
    RogueEventRecords_Check(persistent_records,"hrc");
    /* Do not call gm_801B9DD8 or gm_80180BA0: both write stock records.
     * Raw getter is diagnostic only until finalization and units are verified. */
    OSReport("[rogue] hrc_probe_exit raw=%d reward=0\n",gm_80180AE4());
    active=0;
#endif
}
