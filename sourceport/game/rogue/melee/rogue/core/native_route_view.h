#ifndef ROGUE_NATIVE_ROUTE_VIEW_H
#define ROGUE_NATIVE_ROUTE_VIEW_H
#include "run.h"
#define ROGUE_NATIVE_ROUTE_SLOTS 5
#define ROGUE_ROUTE_UNSELECTED 2U
#define ROGUE_ROUTE_CHOICE 6U
enum RogueRouteVisualState {
    ROGUE_ROUTE_UPCOMING, ROGUE_ROUTE_CURRENT, ROGUE_ROUTE_CLEARED,
    ROGUE_ROUTE_FAILED
};
typedef struct RogueNativeRouteSlot {
    unsigned floor, native_slot, state, selected_side, kind;
    unsigned alternatives[2], events[2];
} RogueNativeRouteSlot;
typedef struct RogueNativeRouteView {
    unsigned act, floor, complete, native_stop, frame_start, frame_end;
    RogueNativeRouteSlot slots[ROGUE_NATIVE_ROUTE_SLOTS];
} RogueNativeRouteView;
/* Read-only display projection. No generation, history, RNG or native pointers.
 * Invalid inputs leave the output untouched. Floors/slot.floor are zero-based;
 * native_stop/native_slot are authored indices 1,3,6,9,11; act is one-based. */
int RogueNativeRoute_Build(const RogueRun* run, RogueNativeRouteView* out);
#endif
