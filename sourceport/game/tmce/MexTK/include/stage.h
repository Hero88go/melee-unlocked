#ifndef MEX_H_STAGE
#define MEX_H_STAGE

#include "color.h"
#include "structs.h"
#include "datatypes.h"
#include "hsd.h"
#include "obj.h"

// MapDesc Flags
#define map_isCObj 0x20000000
#define map_isBG 0x40000000
#define map_isUnk 0x80000000

typedef enum GrInternal
{
    GRKIND_DUMMY,
    GRKIND_TEST,
    GRKIND_CASTLE,
    GRKIND_RCRUISE,
    GRKIND_KONGO,
    GRKIND_GARDEN,
    GRKIND_GREATBAY,
    GRKIND_SHRINE,
    GRKIND_ZEBES,
    GRKIND_KRAID,
    GRKIND_STORY,
    GRKIND_YOSTER,
    GRKIND_IZUMI,
    GRKIND_GREENS,
    GRKIND_CORNERIA,
    GRKIND_VENOM,
    GRKIND_PSTAD,
    GRKIND_PURA,
    GRKIND_MUTECITY,
    GRKIND_BIGBLUE,
    GRKIND_ONETT,
    GRKIND_FOURSIDE,
    GRKIND_ICEMT,
    GRKIND_ICETOP,
    GRKIND_MK1,
    GRKIND_MK2,
    GRKIND_AKANEIA,
    GRKIND_FLATZONE,
    GRKIND_OLDPU,
    GRKIND_OLDSTORY,
    GRKIND_OLDKONGO,
    GRKIND_ADVKRAID,
    GRKIND_ADVSHRINE,
    GRKIND_ADVZR,
    GRKIND_ADVBR,
    GRKIND_ADVTE,
    GRKIND_BATTLE,
    GRKIND_FD,
    // target test stages in between this
    GRKIND_ALLSTARHEAL = 66,
    GRKIND_HOMERUN,
    GRKIND_TROPHY1,
    GRKIND_TROPHY2,
    GRKIND_TROPHY3,
} GrInternal;
typedef enum GrExternal
{
    GRKINDEXT_DUMMY,
    GRKINDEXT_TEST,
    GRKINDEXT_IZUMI,
    GRKINDEXT_PSTAD,
    GRKINDEXT_CASTLE,
    GRKINDEXT_KONGO,
    GRKINDEXT_ZEBES,
    GRKINDEXT_CORNERIA,
    GRKINDEXT_STORY,
    GRKINDEXT_ONETT,
    GRKINDEXT_MUTECITY,
    GRKINDEXT_RCRUISE,
    GRKINDEXT_GARDEN,
    GRKINDEXT_GREATBAY,
    GRKINDEXT_SHRINE,
    GRKINDEXT_KRAID,
    GRKINDEXT_YOSTER,
    GRKINDEXT_GREENS,
    GRKINDEXT_FOURSIDE,
    GRKINDEXT_MK1,
    GRKINDEXT_MK2,
    GRKINDEXT_AKANEIA,
    GRKINDEXT_VENOM,
    GRKINDEXT_PURA,
    GRKINDEXT_BIGBLUE,
    GRKINDEXT_ICEMT,
    GRKINDEXT_ICETOP,
    GRKINDEXT_FLATZONE,
    GRKINDEXT_OLDPU,
    GRKINDEXT_OLDSTORY,
    GRKINDEXT_OLDKONGO,
    GRKINDEXT_BATTLE,
    GRKINDEXT_FD,
} GrExternal;

typedef enum MapRenderKind
{
    MAP_RENDERKIND_FG,      // foreground, rendered third
    MAP_RENDERKIND_FGTRANS, // foreground with transparency, rendered second
    MAP_RENDERKIND_BG,      // background, rendered first
    MAP_RENDERKIND_HIGHPRI, // rendered above everything (fighters effects stage etc)
    MAP_RENDERKIND_NUM,
} MapRenderKind;

/*** Structs ***/

struct __attribute__((scalar_storage_order("big-endian"))) MapDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int onCreation;
    unsigned int onUnk;
    unsigned int onFrame;
    unsigned int onDeletion;
    unsigned char is_lobj : 1;
    unsigned char is_fog : 1;
    unsigned char is_cobj : 1;
};

struct __attribute__((scalar_storage_order("big-endian"))) MapData /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    int x0;
    unsigned int gobj;
    unsigned int post_anim_cb;
    int xC;
    unsigned char flagx80 : 1;
    unsigned char flagx40 : 1;
    unsigned char is_fog : 1;
    unsigned char flagx10 : 1;
    unsigned char flagx8 : 1;
    unsigned char is_check_shadow : 1;
    unsigned char flagx2 : 1;
    unsigned char flagx1 : 1;
    unsigned char render_kind : 3;
    unsigned char flag2x10 : 1;
    unsigned char flag2x08 : 1;
    unsigned char flag2x04 : 1;
    unsigned char flag2x02 : 1;
    unsigned char flag2x01 : 1;
    int index;
    unsigned int camera_gobj;
    unsigned int OnDestroyCB;
    int live_sfx[8];
    struct __attribute__((scalar_storage_order("big-endian"))) {
        int timer;
        int pri;
        unsigned int ptr1;
        int loop;
        unsigned int ptr2;
        int x14;
        unsigned int alloc;
        int x1c;
        int x20;
        int x24;
        int colanim;
        struct __attribute__((scalar_storage_order("big-endian"))) {
            unsigned char r;
            unsigned char g;
            unsigned char b;
            unsigned char a;
        } hex;
        float color_red;
        float color_green;
        float color_blue;
        float color_alpha;
        float colorblend_red;
        float colorblend_green;
        float colorblend_blue;
        float colorblend_alpha;
        struct __attribute__((scalar_storage_order("big-endian"))) {
            unsigned char r;
            unsigned char g;
            unsigned char b;
            unsigned char a;
        } light_color;
        float light_red;
        float light_green;
        float light_blue;
        float light_alpha;
        float lightblend_red;
        float lightblend_green;
        float lightblend_blue;
        float lightblend_alpha;
        float light_angle;
        float light_unk;
        unsigned char color_enable : 1;
        unsigned char flag2 : 1;
        unsigned char light_enable : 1;
        unsigned char flag4 : 1;
        unsigned char flag5 : 1;
        unsigned char flag6 : 1;
        unsigned char flag7 : 1;
        unsigned char flag8 : 1;
    } color;
    int xc0;
    int xc4;
    int xc8;
    int xcc;
    int xd0;
    int xd4;
    int xd8;
    int xdc;
    struct __attribute__((scalar_storage_order("big-endian"))) {
        int mapVar0;
        int mapVar1;
        int mapVar2;
        int mapVar3;
        int mapVar4;
        int mapVar5;
        int mapVar6;
        int mapVar7;
        int x100;
        int x104;
        int x108;
        int x10c;
        int x110;
        int x114;
        int x118;
        int x11c;
        int x120;
        int x124;
        int x128;
        int x12c;
        int x130;
        int x134;
        int x138;
        int x13c;
        int x140;
        int x144;
        int x148;
        int x14c;
        int x150;
        int x154;
        int x158;
        int x15c;
        int x160;
        int x164;
        int x168;
        int x16c;
        int x170;
        int x174;
        int x178;
        int x17c;
        int x180;
        int x184;
        int x188;
        int x18c;
        int x190;
        int x194;
        int x198;
        int x19c;
        int x1a0;
        int x1a4;
        int x1a8;
        int x1ac;
        int x1b0;
        int x1b4;
        int x1b8;
        int x1bc;
        int x1c0;
        int x1c4;
        int x1c8;
        int x1cc;
        int x1d0;
        int x1d4;
        int x1d8;
        int x1dc;
        int x1e0;
        int x1e4;
        int x1e8;
        int x1ec;
        int x1f0;
        int x1f4;
        int x1f8;
        int x1fc;
        int x200;
    } map_var;
};

struct StageOnGO
{
    StageOnGO *next;
    GOBJ *map_gobj;
    void *cb;
};

struct __attribute__((scalar_storage_order("big-endian"))) grGroundParam /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    float scale;
    unsigned char x4;
    unsigned char shadow_alpha;
    unsigned char x6;
    unsigned char x7;
    unsigned short fov;
    int cam_distance_min;
    int cam_distance_max;
    int tilt_scale;
    float y_rotation;
    float x_rotation;
    float fixedness;
    float bubble_mult;
    float cam_smoothness;
    unsigned short x2c;
    unsigned short x2e;
    int pause_min_z;
    int pause_default_z;
    int pause_max_z;
    float pause_tilt_max_up;
    float pause_tilt_max_down;
    float pause_tilt_max_left;
    float pause_tilt_max_right;
    float x4c;
    float cam_fixed_x;
    float cam_fixed_y;
    float cam_fixed_z;
    float cam_fixed_fov;
    float cam_fixed_angle_y;
    float cam_fixed_angle_x;
    unsigned short x68;
    unsigned short item_rates;
    unsigned int bgm_data;
    int bgm_num;
    int xb8;
    int xbc;
    int xc0;
    int xc4;
    int xc8;
    int xcc;
    int xd0;
    int xd4;
    int xd8;
};

struct Stage /* native twin, generated */
{
    union {
        char _mex_native_size[3256];
        struct { float cambound_left; };
        struct { char _p1684[4]; float cambound_right; };
        struct { char _p1685[8]; float cambound_top; };
        struct { char _p1686[12]; float cambound_bottom; };
        struct { char _p1687[16]; Vec2 cambound_offset; };
        struct { char _p1688[24]; float fov_d; };
        struct { char _p1689[28]; float fov_u; };
        struct { char _p1690[32]; float fov_r; };
        struct { char _p1691[36]; float fov_l; };
        struct { char _p1692[40]; float x28; };
        struct { char _p1693[44]; float x2c; };
        struct { char _p1694[48]; float x30; };
        struct { char _p1695[52]; float x34; };
        struct { char _p1696[56]; float x38; };
        struct { char _p1697[60]; float x3c; };
        struct { char _p1698[64]; float x40; };
        struct { char _p1699[68]; float x44; };
        struct { char _p1700[72]; float x48; };
        struct { char _p1701[76]; float x4c; };
        struct { char _p1702[80]; float x50; };
        struct { char _p1703[84]; float x54; };
        struct { char _p1704[88]; float x58; };
        struct { char _p1705[92]; Vec3 fixed_cam_pos; };
        struct { char _p1706[104]; float x68; };
        struct { char _p1707[108]; float x6c; };
        struct { char _p1708[112]; float x70; };
        struct { char _p1709[116]; float blastzoneLeft; };
        struct { char _p1710[120]; float blastzoneRight; };
        struct { char _p1711[124]; float blastzoneTop; };
        struct { char _p1712[128]; float blastzoneBottom; };
        struct { char _p1713[135]; u8 : 7; u8 x84_80 : 1; };
        struct { char _p1714[135]; u8 : 6; u8 x84_40 : 1; };
        struct { char _p1715[135]; u8 : 5; u8 x84_20 : 1; };
        struct { char _p1716[135]; u8 : 4; u8 x84_10 : 1; };
        struct { char _p1717[135]; u8 : 3; u8 x84_08 : 1; };
        struct { char _p1718[135]; u8 : 2; u8 x84_04 : 1; };
        struct { char _p1719[135]; u8 : 1; u8 x84_02 : 1; };
        struct { char _p1720[135]; u8 x84_01 : 1; };
        struct { char _p1721[133]; u8 x85; };
        struct { char _p1722[134]; u8 x86; };
        struct { char _p1723[132]; u8 : 7; u8 x87_80 : 1; };
        struct { char _p1724[132]; u8 : 6; u8 is_end_temple : 1; };
        struct { char _p1725[132]; u8 : 5; u8 is_end_targets : 1; };
        struct { char _p1726[132]; u8 : 4; u8 is_end_mush : 1; };
        struct { char _p1727[132]; u8 : 3; u8 x87_08 : 1; };
        struct { char _p1728[132]; u8 : 2; u8 x87_04 : 1; };
        struct { char _p1729[132]; u8 : 1; u8 x87_02 : 1; };
        struct { char _p1730[132]; u8 x87_01 : 1; };
        struct { char _p1731[136]; GrInternal kind; };
        struct { char _p1732[140]; u8 flags2x80 : 1; };
        struct { char _p1733[140]; u8 : 1; u8 flags2x40 : 1; };
        struct { char _p1734[140]; u8 : 2; u8 flags2x20 : 1; };
        struct { char _p1735[140]; u8 : 3; u8 flags2x10 : 1; };
        struct { char _p1736[140]; u8 : 4; u8 flags2x08 : 1; };
        struct { char _p1737[140]; u8 : 5; u8 flags2x04 : 1; };
        struct { char _p1738[140]; u8 : 6; u8 end_check_mush : 1; };
        struct { char _p1739[140]; u8 : 7; u8 end_check_temple : 1; };
        struct { char _p1740[144]; int (*OnEnterEndGame1Check)(Vec3 *f_pos, int genpoint_index); };
        struct { char _p1741[152]; int (*OnEnterEndGame2Check)(Vec3 *f_pos, int genpoint_index); };
        struct { char _p1742[160]; int hpsID; };
        struct { char _p1743[164]; int x9c; };
        struct { char _p1744[168]; int xa0; };
        struct { char _p1745[172]; int xa4; };
        struct { char _p1746[176]; int xa8; };
        struct { char _p1747[180]; int xac; };
        struct { char _p1748[184]; int xb0; };
        struct { char _p1749[188]; int xb4; };
        struct { char _p1750[192]; int xb8; };
        struct { char _p1751[196]; int xbc; };
        struct { char _p1752[200]; int xc0; };
        struct { char _p1753[204]; int xc4; };
        struct { char _p1754[208]; int xc8; };
        struct { char _p1755[212]; int xcc; };
        struct { char _p1756[216]; int xd0; };
        struct { char _p1757[220]; int xd4; };
        struct { char _p1758[224]; int xd8; };
        struct { char _p1759[228]; int xdc; };
        struct { char _p1760[232]; int xe0; };
        struct { char _p1761[236]; int xe4; };
        struct { char _p1762[240]; int xe8; };
        struct { char _p1763[244]; int xec; };
        struct { char _p1764[248]; int xf0; };
        struct { char _p1765[252]; int xf4; };
        struct { char _p1766[256]; int xf8; };
        struct { char _p1767[260]; int xfc; };
        struct { char _p1768[264]; int x100; };
        struct { char _p1769[268]; int x104; };
        struct { char _p1770[272]; int x108; };
        struct { char _p1771[276]; int x10c; };
        struct { char _p1772[280]; int x110; };
        struct { char _p1773[284]; int x114; };
        struct { char _p1774[288]; int x118; };
        struct { char _p1775[292]; int x11c; };
        struct { char _p1776[296]; int x120; };
        struct { char _p1777[300]; int x124; };
        struct { char _p1778[304]; int x128; };
        struct { char _p1779[320]; int x130; };
        struct { char _p1780[324]; int x134; };
        struct { char _p1781[328]; int x138; };
        struct { char _p1782[332]; int x13c; };
        struct { char _p1783[336]; int x140; };
        struct { char _p1784[340]; int x144; };
        struct { char _p1785[344]; int x148; };
        struct { char _p1786[348]; int x14c; };
        struct { char _p1787[352]; int x150; };
        struct { char _p1788[356]; int x154; };
        struct { char _p1789[360]; int x158; };
        struct { char _p1790[364]; int x15c; };
        struct { char _p1791[368]; int x160; };
        struct { char _p1792[372]; int x164; };
        struct { char _p1793[376]; int x168; };
        struct { char _p1794[380]; int x16c; };
        struct { char _p1795[384]; int x170; };
        struct { char _p1796[388]; int x174; };
        struct { char _p1797[400]; void (*OnShadowRender)(Vec3 *fighter_pos, int unk, JOBJ *stage_jobj); };
        struct { char _p1798[408]; GOBJ *map_gobjs[64]; };
        struct { char _p1799[3008]; COBJ *cobj[4]; };
        struct { char _p1800[3040]; StageOnGO *on_go; };
        struct { char _p1801[3048]; MapItemDesc **itemdata; };
        struct { char _p1802[3056]; CollDataStage *coll_data; };
        struct { char _p1803[3064]; grGroundParam *grGroundParam; };
        struct { char _p1804[3072]; int *ALDYakuAll; };
        struct { char _p1805[3080]; int *map_ptcl; };
        struct { char _p1806[3088]; int *map_texg; };
        struct { char _p1807[3096]; void *yakumono_param; };
        struct { char _p1808[3104]; int *map_plit; };
        struct { char _p1809[3112]; int *x6c8; };
        struct { char _p1810[3120]; void *quake_model_set; };
        struct { char _p1811[3128]; s16 x6d0; };
        struct { char _p1812[3130]; s16 targets_hit; };
        struct { char _p1813[3132]; s16 targets_left; };
        struct { char _p1814[3136]; int x6d8; };
        struct { char _p1815[3140]; int x6dc; };
        struct { char _p1816[3144]; int x6e0; };
        struct { char _p1817[3148]; int x6e4; };
        struct { char _p1818[3152]; int x6e8; };
        struct { char _p1819[3156]; int x6ec; };
        struct { char _p1820[3160]; int x6f0; };
        struct { char _p1821[3164]; int x6f4; };
        struct { char _p1822[3168]; int x6f8; };
        struct { char _p1823[3172]; int x6fc; };
        struct { char _p1824[3176]; int x700; };
        struct { char _p1825[3180]; int x704; };
        struct { char _p1826[3184]; int x708; };
        struct { char _p1827[3188]; float endgame1_boundwidth; };
        struct { char _p1828[3192]; float endgame1_boundheight; };
        struct { char _p1829[3196]; int endgame1_genpoint; };
        struct { char _p1830[3200]; float endgame2_boundwidth; };
        struct { char _p1831[3204]; float endgame2_boundheight; };
        struct { char _p1832[3208]; int endgame2_genpoint; };
        struct { char _p1833[3212]; int x724; };
        struct { char _p1834[3216]; int x728; };
        struct { char _p1837[3224]; struct {
            union {
                char _mex_span[24];
                struct { GOBJ *gobj; };
                struct { char _p1835[8]; Vec3 pos; };
                struct { char _p1836[20]; float x73c; };
            };
        } catch; };
        struct { char _p1838[3248]; int x740; };
    };
};

struct __attribute__((scalar_storage_order("big-endian"))) GeneralPoints /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned short jobj_index;
    unsigned short kind;
};

struct __attribute__((scalar_storage_order("big-endian"))) GeneralPointsInfo /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int jobj_desc;
    unsigned int general_point;
    int num;
};

struct __attribute__((scalar_storage_order("big-endian"))) MapHead /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int general_points_info;
    int general_points_num;
    unsigned int map_gobj_desc;
    int map_gobj_desc_num;
    unsigned int splines;
    int splines_num;
    unsigned int lights;
    int lights_num;
    unsigned int splines_desc;
    int splines_desc_num;
    unsigned int mobj;
    int mobj_num;
};

struct MapCollLink
{
    s16 coll_group;
    s16 unk;
    s16 jobj_index;
};

struct __attribute__((scalar_storage_order("big-endian"))) MapGObjDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    struct __attribute__((scalar_storage_order("big-endian"))) {
        unsigned int jobj;
        unsigned int animjoint;
        unsigned int matanimjoint;
        unsigned int shapeaninjoint;
    } jobjset;
    unsigned int cobj;
    unsigned int x14;
    unsigned int lobj;
    unsigned int fog_desc;
    unsigned int coll_links;
    int coll_links_num;
    unsigned int anim_behave;
    unsigned int coll_links2;
    int coll_links2_num;
};

struct __attribute__((scalar_storage_order("big-endian"))) StageFile /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int archive;
    unsigned int map_head;
    int xc;
};

struct __attribute__((scalar_storage_order("big-endian"))) GrDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    int internal_id;
    unsigned int map_desc;
    unsigned int filename;
    unsigned int onInit;
    unsigned int x10;
    unsigned int onLoad;
    unsigned int onGo;
    unsigned int x1c;
    unsigned int LineDamageCheck;
    unsigned int x24;
    int x28;
    unsigned int x2c;
    int x30;
};

struct GrExtLookup
{
    int internal_id;
    int x4;
    int x8;
};

struct __attribute__((scalar_storage_order("big-endian"))) LineHazardDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    int x0;
    int dmg;
    int angle;
    int kb_growth;
    int x10;
    int kb;
    int element;
    int x1c;
    int sfx;
};

struct LineRange
{
    struct
    {
        float top;
        float bottom;
        float left;
        float right;
    } unk;
    struct
    {
        float top;
        float bottom;
        float left;
        float right;
    } ground;
};

struct __attribute__((scalar_storage_order("big-endian"))) MapItemDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    int index;
    unsigned int desc;
};

extern char mu_mx_stage_info[] __asm__("stage_info");
static Stage *stc_stage = (void *)(mu_mx_stage_info + 0);
extern char mu_mx_ft_804D6578[] __asm__("ft_804D6578");
static int *ftchkdevice_windnum = (void *)(mu_mx_ft_804D6578 + 0);
extern char mu_mx_ftDevice_BuryThingCount[] __asm__("ftDevice_BuryThingCount");
static int *ftchkdevice_grabnum = (void *)(mu_mx_ftDevice_BuryThingCount + 0);
extern char mu_mx_ft_804D6570[] __asm__("ft_804D6570");
static int *ftchkdevice_dmgnum = (void *)(mu_mx_ft_804D6570 + 0);
extern char mu_mx_selected_stage[] __asm__("selected_stage");
static int *stc_gr_ext_cur = (void *)(mu_mx_selected_stage + 0);
extern char mu_mx_selected_stage[] __asm__("selected_stage");
static GrExtLookup *stc_gr_lookup_cur = (void *)(mu_mx_selected_stage + 4);
extern char mu_mx_mpLib_80458868[] __asm__("mpLib_80458868");
static LineRange *stc_line_range = (void *)(mu_mx_mpLib_80458868 + 0);
// static GOBJ **stc_stage_hud_gobj = (void *)0x804d6d80; // points to a gobj that gets rendered to the hud camera
extern void *mu_tmce_ref_stc_stage_hud_gobj;
#define stc_stage_hud_gobj ((GOBJ * *)((char *)mu_tmce_ref_stc_stage_hud_gobj + 0))

/*** Functions ***/
int Stage_GetRandomExternalID();
StageFile *Stage_GetStageFiles();                 // returns an array of StageFiles
StageFile *Stage_GetStageFile(int mapgobj_index); // returns the StageFile the ID belongs to
void Stage_AddFtChkDevice(GOBJ *map, int hazard_kind, void *check);
void Stage_SetChkDevicePos(float y_pos);
void Stage_GetChkDevicePos(float *y_pos, float *y_delta);
float Stage_GetScale();
int *Stage_GetYakumonoParam();
void Stage_SetMapJOBJAnim(GOBJ *map, int jobj_index, int flags, int anim_id, float start_frame, float rate);
void Stage_MapStateChange(GOBJ *map, int map_gobjID, int anim_id);
int Stage_CheckAnimEnd(GOBJ *map, int jobj_index, int flags);  // 0x1 = unk aobj, 0x2 = material aobj, 0x4 = unk aobj
int Stage_CheckAnimEnd2(GOBJ *map, int jobj_index, int flags); // 0x1 = unk aobj, 0x2 = material aobj, 0x4 = unk aobj
void Stage_PlaySFX(MapData *map, int live_index, int sfx_id);
int Stage_CheckSFX(MapData *map, int live_index);
GOBJ *Stage_CreateMapGObj(int mapgobjID);
GOBJ *Stage_CreateMapGObjDefineIndex(JOBJ *jobjdesc, int map_index);
void Stage_DestroyMapGObj(GOBJ *map_gobj);
void *GXLink_Stage(GOBJ *gobj, int pass);
GOBJ *Stage_GetMapGObj(int mapgobjID);
JOBJ *Stage_GetMapGObjJObj(GOBJ *mapgobj, int jointIndex);
int Stage_GetLinesGroup(int line);
int Stage_GetLinesUnk(int line);
int Stage_GetLinesDirection(int line);
void Stage_SetGroundCallback(int group, void *userdata, void (*OnStand)(MapData *mp, int group_index, CollData *coll_data, int weight, int time));
void Stage_ClearGroundCallback(int group);
void Stage_SetCeilingCallback(int group, void *userdata, void *callback);
void Stage_InitMovingColl(JOBJ *mapjoint, int mapgobjID);
void Stage_UpdateMovingColl(GOBJ *mapgobj);
void Stage_GetSpawnPosition(int spawn_id, Vec3 *pos);
Particle *Stage_SpawnEffectPos(int gfxID, int efFileID, Vec3 *pos);
Particle *Stage_SpawnEffectJointPos(int gfxID, int efFileID, JOBJ *pos);
Particle *Stage_SpawnEffectJointPos2(int gfxID, int efFileID, JOBJ *pos);
GOBJ *Zako_Create(int item_id, Vec3 *pos, JOBJ *jobj, Vec3 *velocity, int isMovingItem);
GOBJ *Stage_CreateMapItem(MapData *map_data, int takeDamageSFXKind, int state, JOBJ *joint, Vec3 *pos, int unk_bool, void *onGiveDamage, void *onTakeDamage); // this function creates an item of id 0xA0, its a generic ID used across multiple stages. its mainly used for giving a joint a hurtbox/hitbox and an onTakeDamage callback.
int Stage_CheckForNearbyFighters(Vec3 *pos, float radius);
ptclGen *Stage_CreatePtclGen(int ptcl_index, int bank_index, Vec3 *pos);
float Stage_GetBlastzoneRight();
float Stage_GetBlastzoneLeft();
float Stage_GetBlastzoneTop();
float Stage_GetBlastzoneBottom();
float Stage_GetCameraRight();
float Stage_GetCameraLeft();
float Stage_GetCameraTop();
float Stage_GetCameraBottom();
int Stage_GetGeneralPoint(int index, Vec3 *pos);
void Stage_EnableLineGroup(int index);
void Stage_DisableLineGroup(int index);
void Stage_AutoLinkLineGroups();
void Stage_LinkLineGroups(int group1, int group2);
void Stage_InitLines(CollDataStage *coll_data);
void Stage_InitCatchHazard(GOBJ *map, int unk, void *check_cb);
void Stage_InitMoveHazard(GOBJ *map, int unk, int (*check_cb)(GOBJ *m, GOBJ *f, Vec3 *queued_velocity)); // returns is_apply_velocity
void Stage_InitDamageHazard(GOBJ *map, int unk, void *check_cb);
void Stage_InitLineHazardDescUnk(void *unk, LineHazardDesc *hazard_desc); // 0x80008d30
void Stage_InitColAnim(JOBJ *map_jobj);
void Stage_ApplyColAnim(GOBJ *map, ColAnimDesc *colanim);
void Stage_DisableColAnim(GOBJ *map);
void Stages_MovingCollisionPointsUpdate(int moving_collision_idx);
int Stage_GetExternalID();
int Stage_ExternalToInternal(int ext_id);
void Stage_GetLeftOfLineCoordinates(int ledge_id, Vec3 *pos_out);
void Stage_GetRightOfLineCoordinates(int ledge_id, Vec3 *pos_out);
#endif

