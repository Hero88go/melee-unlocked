#include "native_route_view.h"
#include <string.h>

int RogueNativeRoute_Build(const RogueRun* run, RogueNativeRouteView* out)
{
    RogueNativeRouteView view;
    unsigned i,side,base,focus;
    static const unsigned native_slots[5]={1,3,6,9,11};
    if(!run || !out || run->floor>ROGUE_FLOORS ||
       run->phase>ROGUE_EVENT ||
       ((run->floor==ROGUE_FLOORS)!=(run->phase==ROGUE_COMPLETE))) return 0;
    memset(&view,0,sizeof(view));
    view.complete=run->floor==ROGUE_FLOORS;
    focus=view.complete ? ROGUE_FLOORS-1 : run->floor;
    view.floor=focus;
    view.act=focus/ROGUE_NATIVE_ROUTE_SLOTS+1;
    if(run->act!=view.act) return 0;
    base=(view.act-1)*ROGUE_NATIVE_ROUTE_SLOTS;
    view.native_stop=native_slots[focus-base];
    /* IrRdMap's verified tracks end at 600. A five-stop page requests at
     * most frame 549; logical floor fifteen is never sent as index fifteen. */
    view.frame_start=(view.native_stop-1)*50;
    view.frame_end=view.frame_start+49;
    for(i=0;i<ROGUE_NATIVE_ROUTE_SLOTS;++i) {
        RogueNativeRouteSlot* slot=&view.slots[i];
        const RogueScoreReceipt* receipt;
        unsigned committed;
        slot->floor=base+i; slot->native_slot=native_slots[i];
        receipt=&run->receipts[slot->floor];
        slot->selected_side=ROGUE_ROUTE_UNSELECTED;
        slot->state=slot->floor<run->floor ? ROGUE_ROUTE_CLEARED :
            slot->floor==run->floor ? ROGUE_ROUTE_CURRENT : ROGUE_ROUTE_UPCOMING;
        committed=slot->floor<run->floor || (slot->floor==run->floor &&
            run->current.node_id>=1+slot->floor*2 &&
            run->current.node_id<=2+slot->floor*2 &&
            (run->phase==ROGUE_FIGHT || run->phase==ROGUE_EVENT ||
             run->phase==ROGUE_SHOP || run->phase==ROGUE_REST || run->phase==ROGUE_DEAD));
        for(side=0;side<2;++side) {
            const RogueEventOpportunity* event=&run->event_opportunities[view.act-1];
            slot->alternatives[side]=run->route[slot->floor][side];
            if(slot->alternatives[side]>ROGUE_NODE_EVENT) return 0;
            if(slot->alternatives[side]==ROGUE_NODE_EVENT && event->present &&
               event->floor==slot->floor && event->side==side)
                slot->events[side]=event->event.template_id;
        }
        slot->kind=slot->alternatives[0]==slot->alternatives[1] ?
            slot->alternatives[0] : ROGUE_ROUTE_CHOICE;
        if(committed) {
            side=run->selected_route[slot->floor];
            if(side>1) return 0;
            slot->selected_side=side; slot->kind=slot->alternatives[side];
            if(receipt->committed && receipt->result.outcome!=ROGUE_RESULT_WIN)
                slot->state=ROGUE_ROUTE_FAILED;
            else if(slot->floor==run->floor && run->phase==ROGUE_DEAD)
                slot->state=ROGUE_ROUTE_FAILED;
        }
    }
    *out=view;
    return 1;
}
