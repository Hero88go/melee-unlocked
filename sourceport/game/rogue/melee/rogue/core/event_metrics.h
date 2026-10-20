#ifndef ROGUE_EVENT_METRICS_H
#define ROGUE_EVENT_METRICS_H
#include "settlement.h"
#include "event_route.h"
/* Value-only availability for implemented adapters, not runtime certification. */
void RogueEvent_NativeAvailability(unsigned base_character,
    RogueEventAvailability available[ROGUE_EVENT_TEMPLATES]);

/* Pinned gm_80180AE4 returns native centimeters, quantized to 10 cm.
 * Verified against three US native display outcomes; see WP8_NATIVE_EVENTS.
 * This converts the native measurement, not the rounded feet display. */
int RogueEvent_HomeRunMillimeters(RoguePoints native_centimeters, RoguePoints* millimeters);
/* Finalized native branches only: mode 0 is a positive launch; mode 2 is
 * native no-distance. No caller-visible writes on invalid measurements. */
int RogueEvent_HomeRunResult(RogueSeed run_id, unsigned node_id, unsigned incoming_percent,
    unsigned native_mode, RoguePoints raw_centimeters, unsigned display_value,
    unsigned display_unit, RogueNodeResult* result);
unsigned RogueTarget_Stage(unsigned base_character);
int RogueTarget_RemainingFrames(unsigned seconds, unsigned elapsed_subframe,
    unsigned* frames);
int RogueTarget_ResultValid(const RogueNodeResult* result);
int RogueEvent_EndValid(const RogueNodeResult* result);
int RogueEvent_HomeRunResultValid(const RogueNodeResult* result);

/* Canonical measured millimeters only. Native conversion and finalized-launch
 * validity belong to the platform adapter; this does not settle an attempt. */
int RogueEvent_HomeRunPoints(RoguePoints distance_mm, RoguePoints* points);

/* Counters sampled before native stage teardown. Completion is a separately
 * verified native goal, not inferred from a timer or a zero counter alone. */
int RogueEvent_TargetPoints(unsigned total, unsigned remaining,
    unsigned remaining_frames, unsigned completed, RoguePoints* points);

/* GrNPo.dat goal IDs 0x99..0xAC, read by grPushOn_80219204. A platform
 * adapter must verify native completion and ordered spatial checkpoints;
 * this value-only calculation cannot establish that a goal was reached. */
typedef struct RogueRaceScore {
    unsigned native_goal_score;
    RoguePoints checkpoints, goal, time, total;
} RogueRaceScore;
int RogueEvent_RacePoints(unsigned checkpoints, unsigned native_goal,
    unsigned remaining_frames, unsigned limit_frames, unsigned completed,
    RogueRaceScore* out);
/* Revisits are harmless; future/out-of-order checkpoints are rejected. */
int RogueEvent_RaceCheckpoint(unsigned* count, unsigned checkpoint);
unsigned RogueRace_LimitFrames(unsigned base_character);
int RogueRace_ResultValid(const RogueNodeResult* result);

#endif
