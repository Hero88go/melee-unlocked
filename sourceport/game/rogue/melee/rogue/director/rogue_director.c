#include "rogue_director.h"
static RogueRun run;
static RogueAttemptIdSource id_source;
static RogueSeed last_id;
void RogueDirector_SetIdSource(RogueAttemptIdSource source) { id_source=source; }
RogueRun* RogueDirector_Run(void) { return &run; }
void RogueDirector_Start(RogueSeed seed, unsigned character)
{
    RogueSeed id=id_source ? id_source() : 0;
    if(id<=last_id) id=last_id+1;
    if(!id) { RogueRuntime_SetActive(0); return; } /* Exhausted, never reuse zero. */
    RogueRun_Init(&run, seed, character);
    if(!RogueRun_SetAttemptId(&run,id)) { RogueRuntime_SetActive(0); return; }
    last_id=id;
    RogueRuntime_SetActive(1);
}
void RogueDirector_End(void) { RogueRuntime_SetActive(0); }
