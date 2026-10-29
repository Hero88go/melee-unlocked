/* Executes the exact production CPU damage-command decision after native adaptation. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef int s32;
enum { Ft_Kind_GKoops = 31 };
struct CpuFighter { int kind, level, x18; bool xF9_b0, xFA_b1; };
typedef struct Fighter { int kind; bool x221A_b3; struct CpuFighter cpu; } Fighter;
static int resets, draws;
static float random_value;
static float HSD_Randf(void) { ++draws; return random_value; }
static void ftCo_800B4A78(Fighter* fp) { (void)fp; ++resets; }
static struct CpuFighter* ftCo_800ADE48_inline1(Fighter* fp) { return &fp->cpu; }
static bool decide(Fighter* fp)
{
    s32 switch_cmd;
    struct CpuFighter* data = &fp->cpu;
    struct CpuFighter* data2;
#include "native_cpu_hitlag_branch.h"
    return false;
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void)
{
    for (int kind = 0; kind < 33; ++kind)
        for (int cpu = 0; cpu < 30; ++cpu)
            for (int hitlag = 0; hitlag < 2; ++hitlag) {
                Fighter fp;
                memset(&fp,0,sizeof fp);
                fp.kind = kind; fp.cpu.kind = cpu; fp.cpu.level = 0;
                fp.cpu.x18 = 1; fp.x221A_b3 = hitlag;
                resets = draws = 0; random_value = 0.5f;
                const bool expected = hitlag && kind != Ft_Kind_GKoops && cpu != 0 && cpu != 15;
                CHECK(decide(&fp) == expected);
                CHECK(fp.cpu.x18 == (expected ? 18 : 1));
                CHECK(resets == expected && draws == expected);
                CHECK(!fp.cpu.xFA_b1); /* level-zero Nana stays neutral during hitlag */
            }
    Fighter fp = {11,true,{24,9,1,true,false}};
    resets = draws = 0; random_value = 0.5f;
    CHECK(decide(&fp) && fp.cpu.xFA_b1 && !fp.cpu.xF9_b0);
    CHECK(resets == 1 && draws == 1);
    /* Already in the damage command: do not reset it or draw another random number. */
    CHECK(decide(&fp) && resets == 1 && draws == 1);
    puts("CPU hitlag production branch: all checks passed");
    return 0;
}
