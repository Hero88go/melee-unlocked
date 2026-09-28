#ifndef MEX_H_EFFECTS
#define MEX_H_EFFECTS

#include "structs.h"
#include "datatypes.h"
#include "obj.h"

#define PTCL_LINKMAX 16

/*** Structs ***/

struct __attribute__((scalar_storage_order("big-endian"))) EffectModelDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    float frame_num;
    struct __attribute__((scalar_storage_order("big-endian"))) {
        unsigned int jobj;
        unsigned int animjoint;
        unsigned int matanimjoint;
        unsigned int shapeaninjoint;
    } jobjset;
};

struct __attribute__((scalar_storage_order("big-endian"))) PtclDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned short x0;
    unsigned short x2;
    int effect_idx_start;
    int gen_num;
    unsigned int gen[];
};

struct __attribute__((scalar_storage_order("big-endian"))) TexGDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    int num;
    unsigned int data[];
};

struct EffectDataTable // exists in the dat
{
    PtclDesc *particle;
    TexGDesc *texg;
    EffectModelDesc model_desc[];
};

struct Effect
{
    GOBJ *child;
    GOBJ *gobj;
    GOBJ *parent;
    int xc;
    void *callback;
    int x14;
    int x18;
    int x1c;
    float x20;
    short lifetime;
    char x26;
    char x27;
    char x28; // if this is == 2, the effect is not updated
    char x29;
};

struct ptclGenCallback
{
    void (*cbSpawnParticle)(Particle *);   // x00
    void (*cbDestroyParticle)(Particle *); // x04
};

struct ptclGen // allocated at 8039d9c8
{
    struct ptclGen *next;       // 0x0
    int kind;                   // x4
    float random;               // x8
    float num_to_spawn;         // xc
    JOBJ *joint;                // x10
    u16 genlife;                // x14
    u16 type;                   // x16
    u8 ef_file;                 // x18
    u8 link_no;                 // x19, r3 for 8039f05c
    u8 tex_group;               // x1a
    u8 x1b;                     // x1b
    u16 instance;               // x1c, is equal to idnum value in the particles it creates
    u16 life;                   // x1e
    void *cmdList;              // x20, pointer to track data
    Vec3 pos;                   // x24
    Vec3 vel;                   // x30
    float gravity;              // x3c
    float friction;             // x40
    float size;                 // x44
    float radius;               // x48
    float angle;                // x4c
    int particle_num;           // x50
    GeneratorAppSRT *appsrt;    // x54, points to an SRT mtx used for transforming the generator while attached to a joint
    ptclGenCallback *callbacks; // x58
    void *x5C;                  // x5C
    Vec3 x60;                   // x60
    Vec3 x6c;                   // x6C
    Vec3 x78;                   // x78
    Vec3 x84;                   // x84
    short rect_flags;           // x90
};

struct GeneratorAppSRT // allocated at 803a42b0
{
    int x0;     // x0
    int x4;     // x4
    Vec3 pos;   // x8
    Vec4 rot;   // x14
    Vec3 scale; // x24
    int x30;    // x30
    int x34;    // x34
    int x38;    // x38
    int x3c;    // x3c
    int x40;    // x40
    int x44;    // x44
    int x48;    // x48
    int x4c;    // x4c
    int x50;    // x50
    int x54;    // x54
    int x58;    // x58
    int x5c;    // x5c
    int x60;    // x60
    int x64;    // x64
    int x68;    // x68
    int x6c;    // x6c
    int x70;    // x70
    int x74;    // x74
    int x78;    // x78
    int x7c;    // x7c
    int x80;    // x80
    int x84;    // x84
    int x88;    // x88
    int x8c;    // x8c
    int x90;    // x90
    int x94;    // x94
    int x98;    // x98
    int x9c;    // x9c
    int xa0;    // xa0
    u16 xa2;
};

// courtesy of psilupan: https://pastebin.com/raw/yQdjypW0
struct Particle /* native twin, generated */
{
    union {
        char _mex_native_size[184];
        struct { struct Particle *next; };
        struct { char _p1839[8]; u32 kind; };
        struct { char _p1840[12]; u8 bank; };
        struct { char _p1841[13]; u8 texGroup; };
        struct { char _p1842[14]; u8 poseNum; };
        struct { char _p1843[15]; u8 palNum; };
        struct { char _p1844[16]; u16 sizeCount; };
        struct { char _p1845[18]; u16 primColCount; };
        struct { char _p1846[20]; u16 envColCount; };
        struct { char _p1847[22]; u8 primCol[4]; };
        struct { char _p1848[26]; u8 envCol[4]; };
        struct { char _p1849[30]; u16 cmdWait; };
        struct { char _p1850[32]; u8 loopCount; };
        struct { char _p1851[33]; u8 linkNo; };
        struct { char _p1852[34]; u16 idnum; };
        struct { char _p1853[40]; void *cmdList; };
        struct { char _p1854[48]; u16 cmdPtr; };
        struct { char _p1855[50]; u16 cmdMarkPtr; };
        struct { char _p1856[52]; u16 cmdLoopPtr; };
        struct { char _p1857[54]; u16 life; };
        struct { char _p1858[56]; Vec3 v; };
        struct { char _p1859[68]; float grav; };
        struct { char _p1860[72]; float fric; };
        struct { char _p1861[76]; Vec3 pos; };
        struct { char _p1862[88]; float size; };
        struct { char _p1863[92]; float rotate; };
        struct { char _p1864[96]; u16 aCmpCount; };
        struct { char _p1865[98]; u8 aCmpMode; };
        struct { char _p1866[99]; u8 aCmpParam1; };
        struct { char _p1867[100]; u8 aCmpParam2; };
        struct { char _p1868[101]; u8 pJObjOfs; };
        struct { char _p1869[102]; u16 matColCount; };
        struct { char _p1870[104]; u16 ambColCount; };
        struct { char _p1871[106]; u16 rotateCount; };
        struct { char _p1872[108]; float sizeTarget; };
        struct { char _p1873[112]; float rotateTarget; };
        struct { char _p1874[116]; float rotateAcc; };
        struct { char _p1875[120]; u16 primColRemain; };
        struct { char _p1876[122]; u16 envColRemain; };
        struct { char _p1877[124]; GXColor primColTarget; };
        struct { char _p1878[128]; GXColor envColTarget; };
        struct { char _p1879[132]; u16 matColRemain; };
        struct { char _p1880[134]; u16 ambColRemain; };
        struct { char _p1881[136]; u16 aCmpRemain; };
        struct { char _p1882[138]; u8 aCmpParam1Target; };
        struct { char _p1883[139]; u8 aCmpParam2Target; };
        struct { char _p1884[140]; u8 matRGB; };
        struct { char _p1885[141]; u8 matA; };
        struct { char _p1886[142]; u8 ambRGB; };
        struct { char _p1887[143]; u8 ambA; };
        struct { char _p1888[144]; float trail; };
        struct { char _p1889[152]; ptclGen *gen; };
        struct { char _p1890[160]; GeneratorAppSRT *appsrt; };
        struct { char _p1891[168]; void *userdata; };
        struct { char _p1892[176]; void *callback; };
    };
};

/*** Functions ***/

void Effect_LoadFile(int eff_file_idx);
Effect *Effect_SpawnSync(int gfx_id, ...);
void Effect_SpawnAsync(GOBJ *fighter, Effect *ptr, int type, int gfx_id, ...);
void Effect_SpawnAsyncLookup(GOBJ *gobj, int gfx_id, int bone, int unk, int destroy_on_leave, Vec3 *offset, Vec3 *range);
void Effect_SpawnItEffectLookup(GOBJ *gobj, int gfx_id, int bone, Vec3 *offset, Vec3 *scatter, int unk3);
void Effect_SpawnItEffect(GOBJ *gobj, int gfx_id);
void Effect_DestroyAll(GOBJ *fighter);
void Particle_DestroyAll(JOBJ *jobj);
void Effect_PauseAll(GOBJ *fighter);
void Effect_ResumeAll(GOBJ *fighter);
void Effect_CheckQueue(GOBJ *g, Effect **gfx);
void Particle_InitFile(void *ptcl, void *texg, int r5);
ptclGen *psCreateGeneratorID(int linkno, int bank_no, int ptcl_index);                 // 8039f05c
ptclGen *psCreateGeneratorIDJObj(int linkno, int bank_no, int ptcl_index, JOBJ *jobj); // 8039efac
void psInterpretParticles(u32 blacklist_link_nos);                                     // input is a bitfield, shifted left by 16 bits!
void psExecGenerator(u32 blacklist_link_nos);                                          // input is a bitfield, shifted left by 16 bits!
void psDispParticles(u32 whitelist_link_nos, int pass);                                // input is a bitfield
GeneratorAppSRT *psAddGeneratorAppSRT(ptclGen *ptcl_gen, int unk);
int psRemoveParticleAppSRT(Particle *ptcl);
void psDeletePntJObjwithParticle(Particle *ptcl);
ptclGen *psKillGenerator(ptclGen *gen, ptclGen *unk);
ptclGen *psKillGeneratorEZ(ptclGen *gen);
void psInitDataBanks(int bank_no, void *ptcl, void *texg, int r6, int r7);

// static u16 *stc_ptclnum = R13_OFFSET(-0x3DBE);      // number of pctls alive
extern void *mu_tmce_ref_stc_ptclnum;
#define stc_ptclnum ((u16 *)((char *)mu_tmce_ref_stc_ptclnum + 0))
extern char mu_mx_hsd_804D0908[] __asm__("hsd_804D0908");
static Particle **stc_ptcl = (void *)(mu_mx_hsd_804D0908 + 0);        // last created ptcl
extern char mu_mx_hsd_804D78FC[] __asm__("hsd_804D78FC");
static ptclGen **stc_ptclgen = (void *)(mu_mx_hsd_804D78FC + 0); // last created gen
// static ptclGen **stc_ptclgencurr = R13_OFFSET(-0x3DA8);
extern void *mu_tmce_ref_stc_ptclgencurr;
#define stc_ptclgencurr ((ptclGen * *)((char *)mu_tmce_ref_stc_ptclgencurr + 0))
extern char mu_mx_hsd_804D78E0[] __asm__("hsd_804D78E0");
static u16 *stc_ptclgennum = (void *)(mu_mx_hsd_804D78E0 + 0);

#endif
