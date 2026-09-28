#ifndef MEX_H_COLLISION
#define MEX_H_COLLISION

#include "structs.h"
#include "datatypes.h"
#include "obj.h"

// ECB Flags
#define ECB_GROUND 0x18000
#define ECB_CEIL 0x6000
#define ECB_WALLLEFT 0xfc0
#define ECB_WALLRIGHT 0x3f

// Line Directions
#define LINE_GROUND 1
#define LINE_CEIL 2
#define LINE_WALLRIGHT 4
#define LINE_WALLLEFT 8

/*** Enums ***/
enum LineDirection
{
    LINEDIR_GROUND = 1 << 0,
    LINEDIR_CEIL = 1 << 1,
    LINEDIR_LEFTWALL = 1 << 2,
    LINEDIR_RIGHTWALL = 1 << 3,
};

/*** Structs ***/

struct ECBSize
{
    float topY;
    float botY;
    Vec2 left;
    Vec2 right;
};

struct CollData /* native twin, generated */
{
    union {
        char _mex_native_size[456];
        struct { GOBJ *gobj; };
        struct { char _p1501[8]; Vec3 topN_Curr; };
        struct { char _p1502[20]; Vec3 topN_CurrCorrect; };
        struct { char _p1503[32]; Vec3 topN_Prev; };
        struct { char _p1504[44]; Vec3 topN_Proj; };
        struct { char _p1505[56]; int flags1; };
        struct { char _p1506[60]; int coll_test; };
        struct { char _p1507[64]; int ignore_line; };
        struct { char _p1508[68]; int ledge_left; };
        struct { char _p1509[72]; int ledge_right; };
        struct { char _p1510[76]; int ignore_group; };
        struct { char _p1511[80]; int check_group; };
        struct { char _p1512[84]; float weight; };
        struct { char _p1513[88]; float cliffgrab_width; };
        struct { char _p1514[92]; float cliffgrab_y_offset; };
        struct { char _p1515[96]; float cliffgrab_height; };
        struct { char _p1516[100]; int x60; };
        struct { char _p1517[104]; int x64; };
        struct { char _p1518[108]; int x68; };
        struct { char _p1519[112]; int x6c; };
        struct { char _p1520[116]; int x70; };
        struct { char _p1521[120]; int x74; };
        struct { char _p1522[124]; int x78; };
        struct { char _p1523[128]; int x7c; };
        struct { char _p1524[132]; int x80; };
        struct { char _p1525[136]; Vec2 ecbCurr_top; };
        struct { char _p1526[144]; Vec2 ecbCurr_bot; };
        struct { char _p1527[152]; Vec2 ecbCurr_right; };
        struct { char _p1528[160]; Vec2 ecbCurr_left; };
        struct { char _p1529[168]; Vec2 ecbCurrCorrect_top; };
        struct { char _p1530[176]; Vec2 ecbCurrCorrect_bot; };
        struct { char _p1531[184]; Vec2 ecbCurrCorrect_right; };
        struct { char _p1532[192]; Vec2 ecbCurrCorrect_left; };
        struct { char _p1533[200]; Vec2 ecbPrev_top; };
        struct { char _p1534[208]; Vec2 ecbPrev_bot; };
        struct { char _p1535[216]; Vec2 ecbPrev_right; };
        struct { char _p1536[224]; Vec2 ecbPrev_left; };
        struct { char _p1537[232]; Vec2 ecbProj_top; };
        struct { char _p1538[240]; Vec2 ecbProj_bot; };
        struct { char _p1539[248]; Vec2 ecbProj_right; };
        struct { char _p1540[256]; Vec2 ecbProj_left; };
        struct { char _p1541[264]; int is_use_joints; };
        struct { char _p1542[272]; JOBJ *joint_1; };
        struct { char _p1543[280]; JOBJ *joint_2; };
        struct { char _p1544[288]; JOBJ *joint_3; };
        struct { char _p1545[296]; JOBJ *joint_4; };
        struct { char _p1546[304]; JOBJ *joint_5; };
        struct { char _p1547[312]; JOBJ *joint_6; };
        struct { char _p1548[320]; JOBJ *joint_7; };
        struct { char _p1549[328]; int x124; };
        struct { char _p1550[332]; int x128; };
        struct { char _p1551[336]; int x12c; };
        struct { char _p1552[344]; int flags; };
        struct { char _p1553[348]; int envFlags; };
        struct { char _p1554[352]; int envFlags_prev; };
        struct { char _p1555[356]; int x13c; };
        struct { char _p1556[360]; Vec2 coll_pos; };
        struct { char _p1557[368]; int x148; };
        struct { char _p1558[372]; int ground_index; };
        struct { char _p1559[376]; u8 ground_info; };
        struct { char _p1560[377]; u8 ground_unk; };
        struct { char _p1561[378]; u8 ground_type; };
        struct { char _p1562[379]; u8 ground_mat; };
        struct { char _p1563[380]; Vec3 ground_slope; };
        struct { char _p1564[392]; int rightwall_index; };
        struct { char _p1565[396]; u8 rightwall_info; };
        struct { char _p1566[397]; u8 rightwall_unk; };
        struct { char _p1567[398]; u8 rightwall_type; };
        struct { char _p1568[399]; u8 rightwall_mat; };
        struct { char _p1569[400]; Vec3 rightwall_slope; };
        struct { char _p1570[412]; int leftwall_index; };
        struct { char _p1571[416]; u8 leftwall_info; };
        struct { char _p1572[417]; u8 leftwall_unk; };
        struct { char _p1573[418]; u8 leftwall_type; };
        struct { char _p1574[419]; u8 leftwall_mat; };
        struct { char _p1575[420]; Vec3 leftwall_slope; };
        struct { char _p1576[432]; int ceil_index; };
        struct { char _p1577[436]; u8 ceil_info; };
        struct { char _p1578[437]; u8 ceil_unk; };
        struct { char _p1579[438]; u8 ceil_type; };
        struct { char _p1580[439]; u8 ceil_mat; };
        struct { char _p1581[440]; Vec3 ceil_slope; };
    };
};

struct __attribute__((scalar_storage_order("big-endian"))) CollGroupDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned short floor_start;
    unsigned short floor_num;
    unsigned short ceil_start;
    unsigned short ceil_num;
    unsigned short rwall_start;
    unsigned short rwall_num;
    unsigned short lwall_start;
    unsigned short lwall_num;
    unsigned short dyn_start;
    unsigned short dyn_num;
    struct __attribute__((scalar_storage_order("big-endian"))) {
        float X;
        float Y;
    } area_min;
    struct __attribute__((scalar_storage_order("big-endian"))) {
        float X;
        float Y;
    } area_max;
    unsigned short vert_start;
    unsigned short vert_num;
};

struct CollGroup /* native twin, generated */
{
    union {
        char _mex_native_size[80];
        struct { CollGroup *next; };
        struct { char _p1965[8]; CollGroupDesc *desc; };
        struct { char _p1966[18]; u16 : 1; u16 x8 : 15; };
        struct { char _p1967[18]; u16 is_enabled : 1; };
        struct { char _p1968[18]; u16 xa; };
        struct { char _p1969[20]; u16 ray_id; };
        struct { char _p1970[24]; Vec2 area_min; };
        struct { char _p1971[32]; Vec2 area_max; };
        struct { char _p1972[40]; JOBJ *jobj; };
        struct { char _p1973[48]; void *cb_floor; };
        struct { char _p1974[56]; void *map_data_floor; };
        struct { char _p1975[64]; void *cb_ceil; };
        struct { char _p1976[72]; void *map_data_ceil; };
    };
};

struct __attribute__((scalar_storage_order("big-endian"))) CollLineDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    signed short vert_prev;
    signed short vert_next;
    signed short line_prev;
    signed short line_next;
    signed short line_prev_altgroup;
    signed short line_next_altgroup;
    unsigned char xc;
    unsigned char xd_1 : 1;
    unsigned char xd_2 : 1;
    unsigned char xd_3 : 1;
    unsigned char disabled : 1;
    unsigned char is_left : 1;
    unsigned char is_right : 1;
    unsigned char is_ceil : 1;
    unsigned char is_floor : 1;
    unsigned char xe_1 : 1;
    unsigned char xe_2 : 1;
    unsigned char xe_3 : 1;
    unsigned char xe_4 : 1;
    unsigned char xe_5 : 1;
    unsigned char is_drop : 1;
    unsigned char is_ledge : 1;
    unsigned char is_unk : 1;
    unsigned char material;
};

struct CollLine /* native twin, generated */
{
    union {
        char _mex_native_size[16];
        struct { CollLineDesc *desc; };
        struct { char _p1948[8]; u8 x4; };
        struct { char _p1949[10]; u8 : 7; u8 x5_x80 : 1; };
        struct { char _p1950[10]; u8 : 6; u8 x5_x40 : 1; };
        struct { char _p1951[10]; u8 : 5; u8 x5_x20 : 1; };
        struct { char _p1952[10]; u8 : 4; u8 x5_x10 : 1; };
        struct { char _p1953[10]; u8 : 3; u8 x5_x08 : 1; };
        struct { char _p1954[10]; u8 : 2; u8 x5_x04 : 1; };
        struct { char _p1955[10]; u8 : 1; u8 x5_x02 : 1; };
        struct { char _p1956[10]; u8 is_enabled : 1; };
        struct { char _p1957[10]; u8 x6; };
        struct { char _p1958[8]; u8 : 4; u8 x7 : 4; };
        struct { char _p1959[8]; u8 : 3; u8 is_rwall : 1; };
        struct { char _p1960[8]; u8 : 2; u8 is_lwall : 1; };
        struct { char _p1961[8]; u8 : 1; u8 is_ceil : 1; };
        struct { char _p1962[8]; u8 is_floor : 1; };
    };
};

struct CollVert /* native twin, generated */
{
    union {
        char _mex_native_size[24];
        struct { Vec2 pos_orig; };
        struct { char _p1963[8]; Vec2 pos_curr; };
        struct { char _p1964[16]; Vec2 pos_prev; };
    };
};

struct CollLineUnk
{
    int x0;
    s16 x4;
    s16 x6;
    Vec3 left;  // 0x8
    Vec3 right; // 0x14
};

struct CollDataStage /* native twin, generated */
{
    union {
        char _mex_native_size[48];
        struct { char _p1977[4]; int vert_num; };
        struct { char _p1978[12]; int line_num; };
        struct { char _p1979[16]; u16 floor_start; };
        struct { char _p1980[18]; u16 floor_num; };
        struct { char _p1981[20]; u16 ceil_start; };
        struct { char _p1982[22]; u16 ceil_num; };
        struct { char _p1983[24]; u16 rwall_start; };
        struct { char _p1984[26]; u16 rwall_num; };
        struct { char _p1985[28]; u16 lwall_start; };
        struct { char _p1986[30]; u16 lwall_num; };
        struct { char _p1987[32]; u16 dyn_start; };
        struct { char _p1988[34]; u16 dyn_num; };
        struct { char _p1989[40]; int group_num; };
    };
};

struct CollLineConnection // runtime struct, is created @ 8005a7cc, static array of these for each line direction @ 80458e88
{
    CollLineConnection *next; // 0x0
    int x4;                   // 0x4
    Vec3 start_pos;           // 0x8
    Vec3 end_pos;             // 0x14
    int x20;                  // 0x20
    int x24;                  // 0x24
};

/*** Functions ***/
void Shield_CreateBubble(GOBJ *ft, ShieldDesc *desc, void *(on_hit)(GOBJ *fighter));
void Shield_HitShield(GOBJ *ft);
void GuardReflectInitIDK(GOBJ *ft);
void GuardOnInitIDK(GOBJ *ft);
void Animation_GuardAgain(GOBJ *ft);
void EnvironmentCollision_WaitLanding(GOBJ *ft);
void Coll_CopyPosToECBs(CollData *coll_data, Vec3 *pos);
void Coll_ECBCurrToPrev(CollData *coll_data);
void Coll_InitECB(CollData *coll_data);
void Coll_SetECBScale(CollData *coll_data, float scale1, float scale2, float scale3, float scale4);
int Coll_CheckLedge(CollData *coll_data);
float Coll_GetCurrGroundsFrictionMult(CollData *);
int ECB_CollGround_PassLedge(CollData *ecb, ECBSize *bones); // returns is touching ground bool
int ECB_CollGround3(CollData *ecb);
void ECB_CollAir(CollData *ecb, ECBSize *bones);
int ECB_CollAir2(CollData *ecb);
int ECB_CollAir3(CollData *ecb);
int ECB_CollAirCheckLedge(CollData *ecb);
int ECB_CollGround(CollData *ecb);
int ECB_StoreLedgeCheckDirection(CollData *ecb, int ledge_check_dir);
int GrColl_SearchLedgeLeft(CollData *coll_data, int *return_ledge_index);
int GrColl_SearchLedgeRight(CollData *coll_data, int *return_ledge_index);
void GrColl_GetGroundLineEndLeft(int floor_index, Vec3 *pos);                                                                                                                                    // returns the leftmost grounded coordinate of the inputted line index within its group                                                                                                                                 // this functon will crawl along the entire line sequence and find the end of the ledge
void GrColl_GetGroundLineEndRight(int floor_index, Vec3 *pos);                                                                                                                                   // returns the rightmost grounded coordinate of the inputted line index within its group
void GrColl_GetGroundLineEndLeft_AllGroups(int floor_index, Vec3 *pos);                                                                                                                          // returns the leftmost grounded coordinate of the inputted line index regardless of the group
void GrColl_GetGroundLineEndRight_AllGroups(int floor_index, Vec3 *pos);                                                                                                                         // returns the rightmost grounded coordinate of the inputted line index regardless of the group
int GrColl_RaycastGround(Vec3 *coll_pos, int *line_index, int *line_kind, Vec3 *unk1, int unk2, int unk3, int unk4, void *cb, float fromX, float fromY, float toX, float toY, float unk5); // make unk5
int GrColl_RaycastUnk(Vec3 *coll_pos, int *line_index, int *line_kind, Vec3 *direction, void *cb, void *unk2, float from_x, float from_y, float to_x, float to_y);                                   // unk = 0, unk2 = -1;
int GrColl_RaycastAll(Vec3 *coll_pos, int *line_index, int *line_kind, Vec3 *direction, void *cb, void *unk2, float from_x, float from_y, float to_x, float to_y);
int GrColl_CrawlGround(int line_index, Vec3 *pos, int *return_line, Vec3 *return_pos, int *return_flags, Vec3 *return_slope, float x_offset, float y_offset);                                    // returns bool for if position on line series exists
int GrColl_GetPosDifference(int line_index, Vec3 *pos, Vec3 *return_pos);
int GrColl_GetLineInfo(int line_index, Vec3 *r4, void *r5, int *flags, Vec3 *return_slope);
void GrColl_GetLineSlope(int line_index, Vec3 *return_slope);
int GrColl_CheckIfLineEnabled(int line_index);
_Bool LbColl_IsHitboxHittingHurtbox(
    void *hit_capsule, void *hurt_capsule,
    Mtx *mtx, _Bool hit_all,
    float hit_scale, float hurt_scale, float hurt_z_offset
);

extern char mu_mx_mpColl_804D64AC[] __asm__("mpColl_804D64AC");
static int *stc_colltest = (void *)(mu_mx_mpColl_804D64AC + 0);
// static CollGroup **stc_firstcollgroup = R13_OFFSET(-0x51DC);
extern void *mu_tmce_ref_stc_firstcollgroup;
#define stc_firstcollgroup ((CollGroup * *)((char *)mu_tmce_ref_stc_firstcollgroup + 0))
// static CollGroup **stc_collgroup = R13_OFFSET(-0x51E0);
extern void *mu_tmce_ref_stc_collgroup;
#define stc_collgroup ((CollGroup * *)((char *)mu_tmce_ref_stc_collgroup + 0))
// static CollLine **stc_collline = R13_OFFSET(-0x51E4);
extern void *mu_tmce_ref_stc_collline;
#define stc_collline ((CollLine * *)((char *)mu_tmce_ref_stc_collline + 0))
// static CollVert **stc_collvert = R13_OFFSET(-0x51E8);
extern void *mu_tmce_ref_stc_collvert;
#define stc_collvert ((CollVert * *)((char *)mu_tmce_ref_stc_collvert + 0))
// static CollDataStage **stc_colldata = R13_OFFSET(-0x51EC);
extern void *mu_tmce_ref_stc_colldata;
#define stc_colldata ((CollDataStage * *)((char *)mu_tmce_ref_stc_colldata + 0))
extern char mu_mx_mpIsland_80458E88[] __asm__("mpIsland_80458E88");
static CollLineConnection **stc_first_line_connect = (void *)(mu_mx_mpIsland_80458E88 + 0); // array of 9

#endif
