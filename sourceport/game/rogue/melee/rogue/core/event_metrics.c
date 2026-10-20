#include "event_metrics.h"
#include <string.h>
#include "native_race_rules.h"

unsigned RogueRace_LimitFrames(unsigned character)
{
    return character<26 ? rogue_race_seconds[character]*60 : 0;
}

int RogueRace_ResultValid(const RogueNodeResult* r)
{
    RogueRaceScore score;
    unsigned reason,completed;
    if(!r) return 0;
    if(!r->race.present) {
        static const RogueRaceMetric empty={0};
        return !memcmp(&r->race,&empty,sizeof(r->race));
    }
    if(r->race.present!=1 || r->home_run.present || r->targets.present ||
        r->kind!=ROGUE_RESULT_SKILL || r->race.native_stage!=82 ||
        r->race.native_kit_assist>1 || !RogueRace_LimitFrames(r->race.base_character) ||
        r->race.limit_frames!=RogueRace_LimitFrames(r->race.base_character) ||
        r->race.remaining_frames>r->race.limit_frames || r->race.checkpoints>5 ||
        (r->race.native_goal && (r->race.native_goal<0x99 || r->race.native_goal>0xAC))) return 0;
    if(r->race.native_outcome==6 && r->race.native_goal) reason=ROGUE_EVENT_END_COMPLETE;
    else if(r->race.native_outcome==1 && !r->race.remaining_frames) reason=ROGUE_EVENT_END_TIMEOUT;
    else if(r->race.native_outcome==4) reason=ROGUE_EVENT_END_FALL;
    else if(r->race.native_outcome==7 && (r->event_end==ROGUE_EVENT_END_SKIP ||
        r->event_end==ROGUE_EVENT_END_QUIT)) reason=r->event_end;
    else return 0;
    completed=reason==ROGUE_EVENT_END_COMPLETE;
    if(!RogueEvent_RacePoints(r->race.checkpoints,completed ? r->race.native_goal : 0,
        r->race.remaining_frames,r->race.limit_frames,completed,&score)) return 0;
    if(reason==ROGUE_EVENT_END_QUIT) memset(&score,0,sizeof(score));
    return r->event_end==reason && r->outcome==(unsigned)(reason==ROGUE_EVENT_END_QUIT ?
        ROGUE_RESULT_QUIT : completed ? ROGUE_RESULT_WIN : ROGUE_RESULT_FAILURE) &&
        r->race.native_goal_score==score.native_goal_score &&
        r->race.checkpoint_points==score.checkpoints && r->race.goal_points==score.goal &&
        r->race.time_points==score.time && r->objective_points==score.total;
}

int RogueEvent_RaceCheckpoint(unsigned* count, unsigned checkpoint)
{
    if(!count || *count>5 || checkpoint>=5 || checkpoint>*count) return 0;
    if(checkpoint==*count) ++*count;
    return 1;
}

int RogueEvent_RacePoints(unsigned checkpoints, unsigned native_goal,
    unsigned remaining_frames, unsigned limit_frames, unsigned completed,
    RogueRaceScore* out)
{
    /* Pinned retail GrNPo.dat yakumono_param+0x1C, eight-byte entries.
     * Only the twenty actual goal joints are accepted; trailing placeholder
     * entries in the native table are not playable exits. */
    static const unsigned native_scores[20]={1000,2000,3000,5000,7500,
        10000,12500,15000,17500,20000,20000,20000,22500,22500,22500,
        25000,30000,35000,50000,80000};
    RogueRaceScore score;
    if(!out || checkpoints>5 || !limit_frames || remaining_frames>limit_frames ||
        completed>1 || (completed && (native_goal<0x99 || native_goal>0xAC)) ||
        (!completed && native_goal)) return 0;
    memset(&score,0,sizeof(score));
    score.checkpoints=(RoguePoints)checkpoints*1000;
    if(completed) {
        score.native_goal_score=native_scores[native_goal-0x99];
        score.goal=(RoguePoints)score.native_goal_score*5000/80000;
        score.time=(RoguePoints)remaining_frames*2500/limit_frames;
    }
    score.total=score.checkpoints+score.goal+score.time;
    *out=score;return 1;
}

void RogueEvent_NativeAvailability(unsigned character,
    RogueEventAvailability available[ROGUE_EVENT_TEMPLATES])
{
    unsigned course=RogueTarget_Stage(character);
    if(!available) return;
    memset(available,0,sizeof(*available)*ROGUE_EVENT_TEMPLATES);
    if(character<26) {
        available[ROGUE_EVENT_HOME_RUN-1].supported=1;
        available[ROGUE_EVENT_HOME_RUN-1].native_stage=84;
        available[ROGUE_EVENT_HOME_RUN-1].timer_frames=600;
    }
    if(course) {
        available[ROGUE_EVENT_TARGETS-1].supported=1;
        available[ROGUE_EVENT_TARGETS-1].native_stage=course;
        available[ROGUE_EVENT_TARGETS-1].timer_frames=2700;
    }
}

int RogueEvent_EndValid(const RogueNodeResult* r)
{
    if(!r || r->event_end>ROGUE_EVENT_END_NO_DISTANCE) return 0;
    if(!r->event_end) return 1; /* Legacy receipts have no invented cause. */
    if(r->kind!=ROGUE_RESULT_SKILL && r->kind!=ROGUE_RESULT_GAUNTLET) return 0;
    if(r->event_end==ROGUE_EVENT_END_INTERRUPTED) return 0; /* Not a measured resolution. */
    if(r->event_end==ROGUE_EVENT_END_COMPLETE) return r->outcome==ROGUE_RESULT_WIN;
    if(r->event_end==ROGUE_EVENT_END_QUIT)
        return r->outcome==ROGUE_RESULT_QUIT && !r->objective_points &&
            !r->upgrade_style_points && !r->effect_gold && !r->row_count &&
            !r->native_stage_delta && !r->native_input && !r->native_output;
    return r->outcome==ROGUE_RESULT_FAILURE;
}

int RogueEvent_HomeRunResultValid(const RogueNodeResult* r)
{
    const RogueHomeRunMetric* m;
    RoguePoints mm,objective,display;
    static const RogueHomeRunMetric empty={0};
    if(!r || !RogueEvent_EndValid(r)) return 0;
    m=&r->home_run;
    if(!m->present) return !memcmp(m,&empty,sizeof(*m));
    if(m->present!=1 || m->conversion_version!=1 || r->kind!=ROGUE_RESULT_SKILL ||
        (m->native_mode!=0 && m->native_mode!=2) ||
        (m->display_unit!=ROGUE_DISTANCE_TENTHS_FOOT && m->display_unit!=ROGUE_DISTANCE_DECIMETERS) ||
        m->raw_centimeters<0 || m->raw_centimeters>2147483640LL || m->raw_centimeters%10 ||
        (m->native_mode==0 && (!m->raw_centimeters || r->outcome!=ROGUE_RESULT_WIN)) ||
        (m->native_mode==2 && (m->raw_centimeters || r->outcome!=ROGUE_RESULT_FAILURE)) ||
        !RogueEvent_HomeRunMillimeters(m->raw_centimeters,&mm) || mm!=m->distance_mm ||
        !RogueEvent_HomeRunPoints(mm,&objective) || r->objective_points!=objective) return 0;
    if(r->event_end && ((m->native_mode==0 && r->event_end!=ROGUE_EVENT_END_COMPLETE) ||
        (m->native_mode==2 && r->event_end!=ROGUE_EVENT_END_TIMEOUT &&
         r->event_end!=ROGUE_EVENT_END_NO_DISTANCE))) return 0;
    /* Native display truncates decimeters using its authored conversion.
     * Integers are exact throughout the uncapped display range. */
    display=m->raw_centimeters/10;
    if(m->display_unit==ROGUE_DISTANCE_TENTHS_FOOT) display=display*1000000/304788;
    if(display>99999) display=99999;
    return m->display_value==(unsigned)display;
}

int RogueEvent_HomeRunResult(RogueSeed run_id, unsigned node_id, unsigned incoming_percent,
    unsigned native_mode, RoguePoints raw_centimeters, unsigned display_value,
    unsigned display_unit, RogueNodeResult* out)
{
    RogueNodeResult result;
    if(!out || !run_id || !node_id || node_id>30 || incoming_percent>999) return 0;
    memset(&result,0,sizeof(result));
    result.run_id=run_id; result.node_id=node_id; result.submatch_count=1;
    result.kind=ROGUE_RESULT_SKILL; result.measured=1;
    result.outcome=native_mode==0 ? ROGUE_RESULT_WIN : ROGUE_RESULT_FAILURE;
    result.carried_percent=incoming_percent;
    result.event_end=native_mode==0 ? ROGUE_EVENT_END_COMPLETE : ROGUE_EVENT_END_NO_DISTANCE;
    result.home_run.present=1; result.home_run.conversion_version=1;
    result.home_run.native_mode=native_mode; result.home_run.display_unit=display_unit;
    result.home_run.display_value=display_value; result.home_run.raw_centimeters=raw_centimeters;
    if(!RogueEvent_HomeRunMillimeters(raw_centimeters,&result.home_run.distance_mm) ||
        !RogueEvent_HomeRunPoints(result.home_run.distance_mm,&result.objective_points) ||
        !RogueEvent_HomeRunResultValid(&result)) return 0;
    *out=result; return 1;
}

int RogueEvent_HomeRunMillimeters(RoguePoints native_centimeters, RoguePoints* millimeters)
{
    if(!millimeters || native_centimeters<0 || native_centimeters>ROGUE_POINTS_MAX/10)
        return 0;
    *millimeters=native_centimeters*10;
    return 1;
}

int RogueEvent_HomeRunPoints(RoguePoints distance_mm, RoguePoints* points)
{
    RoguePoints result;
    if(!points || distance_mm<0) return 0;
    /* Clamp before multiplication so even the largest recorded distance is
     * safe. Keep the caller's distance untouched when objective points cap. */
    if(distance_mm>=500000) result=12500;
    else if(distance_mm>=250000)
        result=6000+(distance_mm-250000)*6500/250000;
    else if(distance_mm>=100000)
        result=2000+(distance_mm-100000)*4000/150000;
    else result=distance_mm*2000/100000;
    *points=result;
    return 1;
}

int RogueEvent_TargetPoints(unsigned total, unsigned remaining,
    unsigned remaining_frames, unsigned completed, RoguePoints* points)
{
    RoguePoints result;
    unsigned seconds;
    if(!points || !total || remaining>total || completed>1 ||
        (completed && remaining)) return 0;
    result=(RoguePoints)(total-remaining)*500;
    if(completed) {
        seconds=remaining_frames/60;
        if(seconds>50) seconds=50;
        result+=5000+seconds*50;
    }
    *points=result;
    return 1;
}

unsigned RogueTarget_Stage(unsigned character)
{
    static const unsigned stages[26]={34,36,39,56,41,42,43,44,33,45,46,47,48,
        50,40,51,52,54,55,55,38,35,37,57,49,58};
    return character<26 ? stages[character] : 0;
}

int RogueTarget_RemainingFrames(unsigned seconds, unsigned elapsed_subframe,
    unsigned* frames)
{
    unsigned value;
    if(!frames || seconds>45 || elapsed_subframe>59) return 0;
    /* Native countdown decrements seconds when its UP-counting fractional
     * field rolls 59 -> 0. Initial 45:59 is exactly 2700 frames. */
    value=seconds*60+59-elapsed_subframe;
    if(value>2700) return 0;
    *frames=value;return 1;
}

int RogueTarget_ResultValid(const RogueNodeResult* r)
{
    static const RogueTargetMetric empty={0};
    const RogueTargetMetric* m;
    RoguePoints points;
    unsigned reason,completed;
    if(!r) return 0;
    m=&r->targets;
    if(!m->present) return !memcmp(m,&empty,sizeof(*m));
    if(m->present!=1 || r->home_run.present || r->kind!=ROGUE_RESULT_SKILL ||
        !RogueTarget_Stage(m->base_character) ||
        m->native_stage!=RogueTarget_Stage(m->base_character) || m->total!=10 ||
        m->remaining>10 || m->remaining_frames>2700 || m->native_goal>1 ||
        m->native_kit_assist>1 || (m->native_goal && m->remaining)) return 0;
    /* Preserve the native controller's timeout-before-goal decision. A zero
     * counter by itself is not proof that the native goal outcome won. */
    if(m->native_outcome==6 && m->native_goal && !m->remaining)
        reason=ROGUE_EVENT_END_COMPLETE;
    else if(m->native_outcome==1 && !m->remaining_frames)
        reason=ROGUE_EVENT_END_TIMEOUT;
    else if(m->native_outcome==4) reason=ROGUE_EVENT_END_FALL;
    else if(m->native_outcome==7 &&
        (r->event_end==ROGUE_EVENT_END_SKIP || r->event_end==ROGUE_EVENT_END_QUIT)) reason=r->event_end;
    else return 0;
    completed=reason==ROGUE_EVENT_END_COMPLETE;
    if(reason==ROGUE_EVENT_END_QUIT)
        return r->outcome==ROGUE_RESULT_QUIT && !r->objective_points;
    return r->event_end==reason && r->outcome==(unsigned)(completed ? ROGUE_RESULT_WIN : ROGUE_RESULT_FAILURE) &&
        RogueEvent_TargetPoints(10,m->remaining,m->remaining_frames,completed,&points) &&
        points==r->objective_points;
}
