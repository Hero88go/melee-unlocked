/* Run the production bank-removal path with overlapping IDs and hash collisions.
 * Static allocations let us inspect freed-node reachability without dereferencing
 * released storage. Only the allocator and diagnostic sink are substituted. */
#include "sysdolphin/baselib/synth.c"

extern int printf(const char*, ...);
static void* freed[8];
static int free_count, failures;
void mu_native_free(void* p) { freed[free_count++] = p; }
void OSReport(char* fmt, ...) { (void) fmt; }

static void check(int condition, const char* why)
{
    if (!condition) { printf("FAIL: %s\n", why); failures++; }
}

static void scenario(int remove_newest)
{
    struct SfxEntryHost older[2] = { 0 }, newer[2] = { 0 }, collision = { 0 };
    struct SfxBankNode first = { 0 }, second = { 0 };
    int i;
    memset(hsd_SynthSFXDataHash, 0, sizeof(hsd_SynthSFXDataHash));
    memset(HSD_Synth_804C2AE0, 0, sizeof(HSD_Synth_804C2AE0));
    free_count = 0;
    first.entrynum = 101; first.base = 295; first.count = 2; first.entries = older;
    second.entrynum = 102; second.base = 295; second.count = 2; second.entries = newer;
    first.next = &second;
    HSD_Synth_804C2AE0[3] = &first;
    collision.unk4 = 327;
    hsd_SynthSFXDataHash[327 & 31] = &collision;
    for (i = 0; i < 2; i++) {
        int id = 295 + i;
        older[i].unk4 = newer[i].unk4 = id;
        older[i].next = hsd_SynthSFXDataHash[id & 31];
        newer[i].next = &older[i];
        hsd_SynthSFXDataHash[id & 31] = &newer[i];
    }
    HSD_SynthSFXGroupDataRemove(remove_newest ? 102 : 101);
    for (i = 0; i < 2; i++) {
        struct SfxEntryHost* survivor = remove_newest ? &older[i] : &newer[i];
        check(hsd_SynthSFXDataHash[(295 + i) & 31] == survivor,
              "removing a bank preserves the other bank's same-ID entry");
        check(survivor->next == (i ? NULL : &collision),
              "no freed bank entry remains reachable; collision survives");
    }
    check(free_count == 2 && freed[0] == (void*)(remove_newest ? newer : older),
          "removed bank releases its own entries and descriptor");
    HSD_SynthSFXGroupDataRemove(remove_newest ? 101 : 102);
    check(free_count == 4 && HSD_Synth_804C2AE0[3] == NULL,
          "both descriptors removed exactly once");
    check(hsd_SynthSFXDataHash[295 & 31] == &collision &&
          hsd_SynthSFXDataHash[296 & 31] == NULL,
          "all bank entries removed with collision retained");
    HSD_SynthSFXDataUnlink(327);
    check(hsd_SynthSFXDataHash[327 & 31] == NULL,
          "explicit unlink by ID retains its original behavior");
}

int main(void)
{
    scenario(0);
    scenario(1);
    printf("native sound banks: %d failures\n", failures);
    return failures ? 1 : 0;
}
