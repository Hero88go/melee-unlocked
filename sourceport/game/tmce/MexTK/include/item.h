#ifndef MEX_H_ITEM
#define MEX_H_ITEM

#include "structs.h"
#include "datatypes.h"
#include "obj.h"
#include "gx.h"
#include "color.h"
#include "dynamics.h"
#include <stdbool.h>

// Item IDs
enum ItemID
{
    ITEM_CAPSULE,
    ITEM_BOX,
    ITEM_BARREL,
    ITEM_EGG,
    ITEM_PARTYBALL,
    ITEM_BARRELCANNON,
    ITEM_BOBOMB,
    ITEM_MRSATURN,
    ITEM_HEARTCONTAINER,
    ITEM_MAXIMTOMATO,
    ITEM_STARMAN,
    ITEM_HOMERUNBAT,
    ITEM_BEAMSWORD,
    ITEM_PARASOL,
    ITEM_GREENSHELL,
    ITEM_REDSHELL,
    ITEM_RAYGUN,
    ITEM_FREEZIE,
    ITEM_FOOD,
    ITEM_MOTIONSENSORBOMB,
    ITEM_FLIPPER,
    ITEM_SUPERSCOPE,
    ITEM_STARROD,
    ITEM_LIPSSTICK,
    ITEM_FAN,
    ITEM_FIREFLOWER,
    ITEM_SUPERMUSHROOM,
    ITEM_POISONMUSHROOM,
    ITEM_HAMMER,
    ITEM_WARPSTAR,
    ITEM_SCREWATTACK,
    ITEM_BUNNYHOOD,
    ITEM_METALBOX,
    ITEM_CLOAKINGDEVICE,
    ITEM_POKEBALL,
    ITEM_RAYGUNUNK,
    ITEM_STARRODSTAR,
    ITEM_LIPSSTICKDUST,
    ITEM_SUPERSCOPEBEAM,
    ITEM_RAYGUNBEAM,
    ITEM_HAMMERHEAD,
    ITEM_FLOWER,
    ITEM_YOSHISEGG,
    ITEM_GOOMBA,
    ITEM_REDEAD,
    ITEM_OCTAROK,
    ITEM_OTTOSEA,
    ITEM_STONE,
    ITEM_MARIOFIRE,
    ITEM_DRMARIOPILL,
    ITEM_KIRBYCUTTER,
    ITEM_KIRBYHAMMER,
    ITEM_KIRBYABILITYSTAR,
    ITEM_53,
    ITEM_FOXLASER,
    ITEM_FALCOLASER,
    ITEM_FOXILLUSION,
    ITEM_FALCOPHANTASM,
    ITEM_LINKBOMB,
    ITEM_CLINKBOMB,
    ITEM_LINKBOOMERANG,
    ITEM_CLINKBOOMERANG,
    ITEM_LINKHOOKSHOT,
    ITEM_CLINKHOOKSHOT,
    ITEM_LINKARROW,
    ITEM_CLINKARROW,
    ITEM_NESSPKFIRE,
    ITEM_NESSPKFIREEXPLODE,
    ITEM_NESSPKFLASH,
    ITEM_NESSPKTHUNDER,
    ITEM_NESSPKTHUNDER1,
    ITEM_NESSPKTHUNDER2,
    ITEM_NESSPKTHUNDER3,
    ITEM_NESSPKTHUNDER4,
    ITEM_FOXGUN,
    ITEM_FALCOGUN,
    ITEM_LINKBOW,
    ITEM_CLINKBOW,
    ITEM_NESSPKFLASHEXPLODE,
    ITEM_SHEIKNEEDLETHROWN,
    ITEM_SHEIKNEEDLEHELD,
    ITEM_PIKACHUTHUNDER,
    ITEM_PICHUTHUNDER,
    ITEM_MARIOCAPE,
    ITEM_DRMARIOCAPE,
    ITEM_SHEIKSMOKE,
    ITEM_YOSHIEGGTHROWN,
    ITEM_87,
    ITEM_YOSHISTAR,
    ITEM_89,
    ITEM_90,
    ITEM_91,
    ITEM_92,
    ITEM_SAMUSBOMB,
    ITEM_SAMUSCHARGESHOT,
    ITEM_SAMUSMISSILE,
    ITEM_SAMUSGRAPPLE,
    ITEM_SHEIKCHAIN,
    ITEM_PEACHBOMBER,
    ITEM_PEACHTURNIP,
    ITEM_BOWSERFLAME,
    ITEM_NESSBAT,
    ITEM_NESSYOYO,
    ITEM_PEACHPARASOL,
    ITEM_PEACHTOAD,
    ITEM_LUIGIFIRE,
    ITEM_ICECLIMBERICE,
    ITEM_ICECLIMBERBLIZZARD,
    ITEM_ZELDAFIRE,
    ITEM_ZELDAFIREEXPLODE,
    ITEM_110,
    ITEM_PEACHTOADSPORE,
    ITEM_MEWTWOSHADOWBALL,
    ITEM_ICECLIMBERROPE,
    ITEM_GAWPESTICIDE,
    ITEM_GAWMANHOLE,
    ITEM_GAWFIRE,
    ITEM_GAWPARACHUTE,
    ITEM_GAWTURTLE,
    ITEM_GAWSPERKY,
    ITEM_GAWJUDGE,
    ITEM_119,
    ITEM_GAWSAUSAGE,
    ITEM_CLINKMILK,
    ITEM_GAWFIREFIGHTER,

    // pokemon
    ITEM_POKERANDOM = 160,
    ITEM_GOLDEEN,
    ITEM_CHICORITA,
    ITEM_SNORLAX,
    ITEM_BLASTOISE,
    ITEM_WEEZING,
    ITEM_CHARIZARD,
    ITEM_MOLTRES,
    ITEM_ZAPDOS,
    ITEM_ARCTICUNO,
    ITEM_WOBBUFFET,
    ITEM_SCIZOR,
    ITEM_UNOWN,
    ITEM_ENTEI,
    ITEM_RAIKOU,
    ITEM_SUICUNE,
    ITEM_BELLOSSOM,
    ITEM_ELECTRODE,
    ITEM_LUGIA,
    ITEM_HOOH,
    ITEM_DITTO,
    ITEM_CLEFAIRY,
    ITEM_TOGEPI,
    ITEM_MEW,
    ITEM_CELEBI,
    ITEM_STARYU,
    ITEM_CHANSEY,
    ITEM_PORYGON2,
    ITEM_CYNDAQUIL,
    ITEM_MARILL,
    ITEM_VENUSAUR,

    // stage
    ITEM_OLDGOOMBA = 208,
    ITEM_TARGET,
    ITEM_BIRDOEGG = 236,
};

// ItemStateChange Flags
#define ITEMSTATE_UPDATEANIM 0x2
#define ITEMSTATE_GRAB 0x4
#define ITEMSTATE_KEEPHIT 0x10 // dont remove hitboxes on state change

// Item hold_kind definitions
enum ItHoldKind
{
    ITHOLD_NONE,      // no hand change
    ITHOLD_OPENIN,    // open palm, facing inwards (towards fighter)
    ITHOLD_SWORD,     // closed palm, holding thin long object
    ITHOLD_OPENDOWN,  // open palm, facing down
    ITHOLD_OPENFRONT, // open palm, facing forward
};
enum ItUnkKind
{
    ITUNK_HAND,  // held item, like a capsule
    ITUNK_HEAVY, // overhead item, like a crate
    ITUNK_2,
    ITUNK_3,
    ITUNK_4,
    ITUNK_5,
    ITUNK_6,
    ITUNK_7,
    ITUNK_NONE, // unable to be held
};

/*** Structs ***/

struct __attribute__((scalar_storage_order("big-endian"))) ItemModelDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int model;
    int bone_count;
    int bone_attach_id;
    int bit_field;
};

struct ItemStateDesc
{
    void *anim_joint;
    void *matanim_joint;
    void *param;
    void *script;
};

struct ItemDesc
{
    int *common_attributes;
    int *unqiue_attributes;
    int *hurtboxes;
    ItemStateDesc *states;
    ItemModelDesc *model;
    int *dynamics;
};

struct itCommonData
{
    int x00;
    int x04;
    int x08;
    int x0C;
    int x10;
    int x14;
    int x18;
    int x1C;
    int x20;
    int x24;
    int x28;
    int x2C;
    int x30;
    int x34;
    int x38;
    int x3C;
    int x40;
    int x44;
    float x48;
    float x4C;
    float x50;
    float x54;
    float x58;
    float x5C;
    float x60;
    float x64;
    float x68;
    float x6C;
    float x70;
    float x74;
    float x78;
    float x7C;
    float x80;
    float x84;
    float x88;
    float x8C;
    float x90;
    float x94;
    float x98;
    float x9C;
    float xA0;
    float xA4;
    float xA8;
    float xAC;
    float xB0;
    int xB4;
    float xB8;
    float xBC;
    float xC0;
    float xC4;
    float xC8;
    float xCC;
    float xD0;
    float xD4;
    int xD8;
    int xDC;
    float xE0;
    float xE4;
    float xE8;
    float xEC;
    float xF0;
    float xF4;
    float xF8;
    int xFC;
    int x100;
    int x104;
    int x108;
    int x10C;
    int x110;
    int x114;
    int x118;
    int x11C;
    int x120;
    int x124;
    int x128;
    int x12C;
    int x130;
    int x134;
    int x138;
    int x13C;
    int x140;
    float x144;
    int x148;
    float x14C;
    float x150;
    float x154;
    float x158;
    float x15C;
};

struct itPublicData
{
    itCommonData *common_data;
    ItemDesc **common_items;
    ItemDesc **adventure_items;
    ItemDesc **pokemon_items;
    int *x10;
    int *x14;
};

struct __attribute__((scalar_storage_order("big-endian"))) itData /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int param;
    unsigned int param_ext;
    unsigned int hurtboxes;
    unsigned int states;
    unsigned int model;
    unsigned int dynamics;
};

struct __attribute__((scalar_storage_order("big-endian"))) itCommonAttr /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    char is_heavy : 1;
    char x0_78 : 4;
    char hold_kind : 3;
    unsigned char x1_1 : 1;
    unsigned char x1_2 : 1;
    unsigned char x1_3 : 1;
    unsigned char x1_4 : 1;
    unsigned char x1_5 : 1;
    unsigned char cam_kind : 2;
    unsigned char x1_8 : 1;
    char flags4;
    float throw_speed_mult;
    int x8;
    float spin_speed;
    float fall_speed;
    float fall_speed_max;
    float x18;
    float dmg_mult;
    int x20;
    int x24;
    int x28;
    int x2c;
    int x30;
    float x34;
    struct __attribute__((scalar_storage_order("big-endian"))) {
        float X;
        float Y;
    } grab_range;
    float ecb_top;
    float ecb_bot;
    float ecb_right;
    float ecb_left;
    float weight;
    int x54;
    int x58;
    int x5c;
    float scale;
    int destroy_gfx;
    int x68;
    int x6c;
    int x70;
    int x74;
    int destroy_sfx;
    int x7c;
    int x80;
    int x84;
    int x88;
    int x8c;
    int x90;
    int x94;
    int x98;
    int x9c;
};

struct ItemState
{
    int state;
    void *animCallback;
    void *physCallback;
    void *collCallback;
};

struct SpawnItem
{
    GOBJ *parent_gobj;                  // 0x0
    GOBJ *parent_gobj2;                 // 0x4
    int it_kind;                        // 0x8, id of the item to spawn
    int hold_kind;                      // 0xC, defines the behavior of the item, such as thrown and pickup. 0 = capsule
    int unk2;                           // 0x10
    Vec3 pos;                           // 0x14
    Vec3 pos2;                          // 0x20
    Vec3 vel;                           // 0x2C
    float facing_direction;             // 0x38
    short damage;                       // 0x3C
    short unk5;                         // 0x3E
    int unk6;                           // 0x40, 1 = correct initial position
#ifdef MU_NATIVE
    unsigned char : 7; /* the game keeps this flag byte most significant bit first */
#endif
    unsigned char is_raycast_below : 1; // 0x44, 0x80 = perform initial collision check
    int is_spin;                        // 0x48, enables item spinning
};

struct itHit /* native twin, generated */
{
    union {
        char _mex_native_size[520];
        struct { int active; };
        struct { char _p2274[4]; int x4; };
        struct { char _p2275[8]; int dmg; };
        struct { char _p2276[12]; float dmg_f; };
        struct { char _p2277[16]; Vec3 offset; };
        struct { char _p2278[28]; float size; };
        struct { char _p2279[32]; int angle; };
        struct { char _p2280[36]; int kb_growth; };
        struct { char _p2281[40]; int wdsk; };
        struct { char _p2282[44]; int kb; };
        struct { char _p2283[48]; int attribute; };
        struct { char _p2284[52]; int shield_dmg; };
        struct { char _p2285[56]; int hitsound_severity; };
        struct { char _p2286[60]; int hitsound_kind; };
        struct { char _p2287[64]; unsigned char x401 : 1; };
        struct { char _p2288[64]; unsigned char : 1; unsigned char x402 : 1; };
        struct { char _p2289[64]; unsigned char : 2; unsigned char hit_air : 1; };
        struct { char _p2290[64]; unsigned char : 3; unsigned char hit_ground : 1; };
        struct { char _p2291[65]; unsigned char : 3; unsigned char x405 : 1; };
        struct { char _p2292[65]; unsigned char : 2; unsigned char x406 : 1; };
        struct { char _p2293[65]; unsigned char : 1; unsigned char x407 : 1; };
        struct { char _p2294[65]; unsigned char x408 : 1; };
        struct { char _p2295[64]; unsigned char : 7; unsigned char x41_80 : 1; };
        struct { char _p2296[64]; unsigned char : 6; unsigned char x41_40 : 1; };
        struct { char _p2297[64]; unsigned char : 5; unsigned char x41_20 : 1; };
        struct { char _p2298[64]; unsigned char : 4; unsigned char x41_10 : 1; };
        struct { char _p2299[65]; unsigned char : 4; unsigned char timed_rehit_on_item : 1; };
        struct { char _p2300[65]; unsigned char : 5; unsigned char timed_rehit_on_fighter : 1; };
        struct { char _p2301[65]; unsigned char : 6; unsigned char timed_rehit_on_shield : 1; };
        struct { char _p2302[65]; unsigned char : 7; unsigned char can_reflect : 1; };
        struct { char _p2303[66]; unsigned char can_absorb : 1; };
        struct { char _p2304[66]; unsigned char : 1; unsigned char x42_40 : 1; };
        struct { char _p2305[66]; unsigned char : 2; unsigned char hit_facing : 1; };
        struct { char _p2306[66]; unsigned char : 3; unsigned char can_deflect : 1; };
        struct { char _p2307[66]; unsigned char : 4; unsigned char unk_reflect : 1; };
        struct { char _p2308[66]; unsigned char : 5; unsigned char no_hurt : 1; };
        struct { char _p2309[66]; unsigned char : 6; unsigned char ignore_ungrab_hurtbox : 1; };
        struct { char _p2310[66]; unsigned char : 7; unsigned char x42_01 : 1; };
        struct { char _p2311[67]; unsigned char hit_item : 1; };
        struct { char _p2312[67]; unsigned char : 1; unsigned char x432 : 1; };
        struct { char _p2313[67]; unsigned char : 2; unsigned char hit_all : 1; };
        struct { char _p2314[67]; unsigned char : 3; unsigned char x434 : 1; };
        struct { char _p2315[67]; unsigned char : 4; unsigned char x435 : 1; };
        struct { char _p2316[67]; unsigned char : 5; unsigned char x436 : 1; };
        struct { char _p2317[67]; unsigned char : 6; unsigned char x437 : 1; };
        struct { char _p2318[67]; unsigned char : 7; unsigned char x438 : 1; };
        struct { char _p2319[68]; int x44; };
        struct { char _p2320[72]; JOBJ *bone; };
        struct { char _p2321[80]; Vec3 pos; };
        struct { char _p2322[92]; Vec3 pos_prev; };
        struct { char _p2323[104]; Vec3 pos_coll; };
        struct { char _p2324[116]; float coll_distance; };
        struct { char _p2326[120]; struct {
            union {
                char _mex_span[16];
                struct { void *data; };
                struct { char _p2325[8]; int timer; };
            };
        } victims[24]; };
        struct { char _p2327[504]; int x134; };
    };
};

struct ItHurt /* native twin, generated */
{
    union {
        char _mex_native_size[72];
        struct { int hurt_status; };
        struct { char _p2343[4]; Vec3 hurt1_offset; };
        struct { char _p2344[16]; Vec3 hurt2_offset; };
        struct { char _p2345[28]; float scale; };
        struct { char _p2346[32]; JOBJ *jobj; };
        struct { char _p2347[40]; unsigned char is_updated : 1; };
        struct { char _p2348[40]; unsigned char : 1; unsigned char x24_2 : 1; };
        struct { char _p2349[40]; unsigned char : 2; unsigned char x24_3 : 1; };
        struct { char _p2350[40]; unsigned char : 3; unsigned char x24_4 : 1; };
        struct { char _p2351[40]; unsigned char : 4; unsigned char x24_5 : 1; };
        struct { char _p2352[40]; unsigned char : 5; unsigned char x24_6 : 1; };
        struct { char _p2353[40]; unsigned char : 6; unsigned char x24_7 : 1; };
        struct { char _p2354[40]; unsigned char : 7; unsigned char x24_8 : 1; };
        struct { char _p2355[44]; Vec3 hurt1_pos; };
        struct { char _p2356[56]; Vec3 hurt2_pos; };
        struct { char _p2357[68]; int bone_index; };
    };
};

struct ItDynamics
{
    int dynamics_num;            // 0x8 number of dynamic bonesets for this fighter
    DynamicsDesc *dynamics_desc; // 0x4 boneset data array (one for each boneset)
};

struct ItDynamicBoneset
{
    int apply_phys_num;     // if this is 256, dyanmics are not processed
    JOBJ *root_bone;        // 0x4, is referenced when adding aobjs @ 80268c24, im guessing to skip adding the anims for dyn bones
    DynamicBoneset boneset; // 0x8
};

struct ItemData /* native twin, generated */
{
    union {
        char _mex_native_size[5416];
        struct { char _p1073[8]; GOBJ *item; };
        struct { char _p1074[16]; int x8; };
        struct { char _p1075[20]; int spawn_kind; };
        struct { char _p1076[24]; int kind; };
        struct { char _p1077[28]; int x14; };
        struct { char _p1078[32]; int x18; };
        struct { char _p1079[36]; int x1c; };
        struct { char _p1080[40]; u8 team_id; };
        struct { char _p1081[44]; int state; };
        struct { char _p1082[48]; int x28; };
        struct { char _p1083[52]; float facing_direction; };
        struct { char _p1084[56]; int x30; };
        struct { char _p1085[60]; float spin_unk; };
        struct { char _p1086[64]; float scale; };
        struct { char _p1087[68]; int x3c; };
        struct { char _p1088[72]; Vec3 self_vel; };
        struct { char _p1089[84]; Vec3 pos; };
        struct { char _p1090[96]; Vec3 vel_unk; };
        struct { char _p1091[108]; Vec3 vel_unk2; };
        struct { char _p1092[120]; Vec3 vel_nudge; };
        struct { char _p1093[132]; Vec3 x7c; };
        struct { char _p1094[144]; Vec3 x88; };
        struct { char _p1095[156]; int x94; };
        struct { char _p1096[160]; int x98; };
        struct { char _p1097[164]; int x9c; };
        struct { char _p1098[168]; int xa0; };
        struct { char _p1099[172]; int xa4; };
        struct { char _p1100[176]; int xa8; };
        struct { char _p1101[180]; int xac; };
        struct { char _p1102[184]; int xb0; };
        struct { char _p1103[188]; int xb4; };
        struct { char _p1104[192]; struct 
{
  ItemState *item_states;
  void (*OnCreate)(GOBJ *item);
  void (*OnDestroy)(GOBJ *item);
  void (*OnPickup)(GOBJ *item);
  void (*OnDrop)(GOBJ *item);
  void (*OnThrow)(GOBJ *item);
  int (*OnGiveDamage)(GOBJ *item);
  int (*OnTakeDamage)(GOBJ *item);
  void (*OnEnterAir)(GOBJ *item);
  void (*OnReflect)(GOBJ *item);
  void (*x28)(GOBJ *item);
  void (*x2c)(GOBJ *item);
  int (*OnShieldBounce)(GOBJ *item);
  int (*OnShieldHit)(GOBJ *item);
  void (*x38)(GOBJ *item);
} *it_func; };
        struct { char _p1105[200]; ItemState *item_states; };
        struct { char _p1106[208]; int air_state; };
        struct { char _p1107[216]; itData *itData; };
        struct { char _p1108[224]; JOBJ *joint; };
        struct { char _p1109[232]; itCommonAttr *common_attr; };
        struct { char _p1116[248]; struct {
            union {
                char _mex_span[40];
                struct { int apply_phys_num; };
                struct { char _p1110[8]; JOBJ *root_bone; };
                struct { char _p1115[16]; struct {
                    union {
                        char _mex_span[24];
                        struct { DynamicBoneData *data; };
                        struct { char _p1111[8]; int bone_num; };
                        struct { char _p1112[12]; float x8; };
                        struct { char _p1113[16]; float xc; };
                        struct { char _p1114[20]; float x10; };
                    };
                } boneset; };
            };
        } dynamics_boneset[24]; };
        struct { char _p1117[1208]; int dynamics_num; };
        struct { char _p1118[1216]; CollData coll_data; };
        struct { char _p1119[1680]; GOBJ *fighter_gobj; };
        struct { char _p1120[1696]; CmSubject *camera_subject; };
        struct { char _p1121[1704]; int x524; };
        struct { char _p1122[1708]; int x528; };
        struct { char _p1123[1712]; int *script_parse; };
        struct { char _p1124[1720]; int x530; };
        struct { char _p1125[1728]; int x534; };
        struct { char _p1126[1752]; int x540; };
        struct { char _p1127[1756]; int x544; };
        struct { char _p1128[1760]; ColorOverlay color; };
        struct { char _p1129[1920]; int x5c8; };
        struct { char _p1130[1924]; float current_frame; };
        struct { char _p1131[1928]; float framerate; };
        struct { char _p1132[1936]; itHit hitbox[4]; };
        struct { char _p1133[4016]; int hit_exception_id; };
        struct { char _p1134[4020]; int hurt_num; };
        struct { char _p1135[4024]; ItHurt it_hurt[2]; };
        struct { char _p1140[4168]; struct {
            union {
                char _mex_span[20];
                struct { int xb54; };
                struct { char _p1136[4]; int xb58; };
                struct { char _p1137[8]; float x1; };
                struct { char _p1138[12]; float x2; };
                struct { char _p1139[16]; float y; };
            };
        } footstool; };
        struct { char _p1141[4188]; int dynamics_xb68; };
        struct { char _p1142[4192]; int xb6c; };
        struct { char _p1143[4196]; int xb70; };
        struct { char _p1144[4200]; int xb74; };
        struct { char _p1145[4204]; int xb78; };
        struct { char _p1146[4216]; int xb80; };
        struct { char _p1147[4220]; int xb84; };
        struct { char _p1148[4224]; int xb88; };
        struct { char _p1149[4228]; int xb8c; };
        struct { char _p1150[4232]; int xb90; };
        struct { char _p1151[4240]; int xb94; };
        struct { char _p1152[4244]; int xb98; };
        struct { char _p1153[4248]; int xb9c; };
        struct { char _p1154[4252]; int xba0; };
        struct { char _p1155[4264]; int xba8; };
        struct { char _p1156[4268]; int xbac; };
        struct { char _p1157[4272]; int xbb0; };
        struct { char _p1158[4276]; int xbb4; };
        struct { char _p1159[4280]; int xbb8; };
        struct { char _p1160[4288]; JOBJ **bones; };
        struct { char _p1171[4296]; struct {
            union {
                char _mex_span[46];
                struct { GOBJ *child; };
                struct { char _p1161[16]; int xc; };
                struct { char _p1162[24]; int x14; };
                struct { char _p1163[28]; int x18; };
                struct { char _p1164[32]; int x1c; };
                struct { char _p1165[36]; float x20; };
                struct { char _p1166[40]; short lifetime; };
                struct { char _p1167[42]; char x26; };
                struct { char _p1168[43]; char x27; };
                struct { char _p1169[44]; char x28; };
                struct { char _p1170[45]; char x29; };
            };
        } effect; };
        struct { char _p1172[4344]; int xbec; };
        struct { char _p1173[4348]; int xbf0; };
        struct { char _p1174[4352]; int xbf4; };
        struct { char _p1175[4356]; int xbf8; };
        struct { char _p1176[4360]; int xbfc; };
        struct { char _p1177[4364]; int xc00; };
        struct { char _p1178[4368]; int xc04; };
        struct { char _p1179[4372]; int xc08; };
        struct { char _p1180[4376]; int xc0c; };
        struct { char _p1181[4380]; int xc10; };
        struct { char _p1182[4384]; int xc14; };
        struct { char _p1183[4388]; int xc18; };
        struct { char _p1184[4392]; float ecb_top; };
        struct { char _p1185[4396]; float ecb_bottom; };
        struct { char _p1186[4400]; float ecb_right; };
        struct { char _p1187[4404]; float ecb_left; };
        struct { char _p1188[4408]; int xc2c; };
        struct { char _p1189[4412]; int xc30; };
        struct { char _p1237[4416]; struct {
            union {
                char _mex_span[208];
                struct { int dealt; };
                struct { char _p1190[4]; int xc38; };
                struct { char _p1191[8]; int xc3c; };
                struct { char _p1192[12]; int xc40; };
                struct { char _p1193[16]; int xc44; };
                struct { char _p1194[20]; int xc48; };
                struct { char _p1195[24]; int xc4c; };
                struct { char _p1196[28]; int xc50; };
                struct { char _p1197[32]; float shield_hit_angle; };
                struct { char _p1198[36]; float shield_hit_xc58; };
                struct { char _p1199[40]; float shield_hit_xc5c; };
                struct { char _p1200[44]; float shield_hit_xc60; };
                struct { char _p1201[48]; GOBJ *reflect; };
                struct { char _p1202[56]; float xc68; };
                struct { char _p1203[60]; int xc6c; };
                struct { char _p1204[64]; int xc70; };
                struct { char _p1205[68]; int xc74; };
                struct { char _p1206[72]; int xc78; };
                struct { char _p1207[76]; int xc7c; };
                struct { char _p1208[80]; int xc80; };
                struct { char _p1209[84]; int xc84; };
                struct { char _p1210[88]; int xc88; };
                struct { char _p1211[92]; int xc8c; };
                struct { char _p1212[96]; GOBJ *xc90; };
                struct { char _p1213[104]; int xc94; };
                struct { char _p1214[108]; int xc98; };
                struct { char _p1215[112]; int total; };
                struct { char _p1216[116]; int recent; };
                struct { char _p1217[120]; int xca4; };
                struct { char _p1218[124]; int xca8; };
                struct { char _p1219[128]; int angle; };
                struct { char _p1220[132]; int source_ply; };
                struct { char _p1221[136]; int xcb4; };
                struct { char _p1222[140]; float givedmg_direction; };
                struct { char _p1223[144]; float hitlag_frames; };
                struct { char _p1224[148]; int xcc0; };
                struct { char _p1225[152]; int xcc4; };
                struct { char _p1226[156]; float kb; };
                struct { char _p1227[160]; float takedmg_direction; };
                struct { char _p1228[164]; float xcd0; };
                struct { char _p1229[168]; float xcd4; };
                struct { char _p1230[172]; float xcd8; };
                struct { char _p1231[176]; float xcdc; };
                struct { char _p1232[180]; float xce0; };
                struct { char _p1233[184]; float xce4; };
                struct { char _p1234[188]; float xce8; };
                struct { char _p1235[192]; GOBJ *source_fighter; };
                struct { char _p1236[200]; GOBJ *source_item; };
            };
        } dmg; };
        struct { char _p1238[4624]; GOBJ *hit_fighter; };
        struct { char _p1239[4632]; GOBJ *detected_fighter; };
        struct { char _p1240[4648]; GOBJ *grabbed_fighter; };
        struct { char _p1241[4656]; GOBJ *attacker_item; };
        struct { char _p1242[4664]; u8 xd08; };
        struct { char _p1243[4665]; u8 xd09; };
        struct { char _p1244[4666]; u8 xd0A; };
        struct { char _p1245[4667]; u8 xd0B; };
        struct { char _p1246[4668]; int xd0c; };
        struct { char _p1247[4672]; int xd10; };
        struct { char _p1257[4680]; struct {
            union {
                char _mex_span[80];
                struct { void (*anim)(GOBJ *item); };
                struct { char _p1248[8]; void (*phys)(GOBJ *item); };
                struct { char _p1249[16]; void (*coll)(GOBJ *item); };
                struct { char _p1250[24]; void (*accessory)(GOBJ *item); };
                struct { char _p1251[32]; void (*on_detect)(GOBJ *item); };
                struct { char _p1252[40]; void (*on_enter_hitlag)(GOBJ *item); };
                struct { char _p1253[48]; void (*on_exit_hitlag)(GOBJ *item); };
                struct { char _p1254[56]; void *jumped_on; };
                struct { char _p1255[64]; void (*grabFt_onIt)(GOBJ *item); };
                struct { char _p1256[72]; void (*grabFt_onFt)(GOBJ *fighter, GOBJ *item); };
            };
        } cb; };
        struct { char _p1258[4760]; float spin_speed; };
        struct { char _p1259[4764]; int xd40; };
        struct { char _p1260[4768]; float lifetime; };
        struct { char _p1261[4772]; int xd48; };
        struct { char _p1262[4776]; int xd4c; };
        struct { char _p1263[4780]; int land_num; };
        struct { char _p1264[4784]; int throw_num; };
        struct { char _p1265[4788]; int xd58; };
        struct { char _p1266[4792]; int xd5c; };
        struct { char _p1267[4796]; int xd60; };
        struct { char _p1268[4800]; int xd64; };
        struct { char _p1269[4804]; int xd68; };
        struct { char _p1270[4808]; int xd6c; };
        struct { char _p1271[4812]; int xd70; };
        struct { char _p1272[4816]; int xd74; };
        struct { char _p1273[4820]; int xd78; };
        struct { char _p1274[4824]; int destroy_sfx; };
        struct { char _p1275[4828]; int xd80; };
        struct { char _p1276[4832]; int xd84; };
        struct { char _p1277[4836]; int atk_kind; };
        struct { char _p1278[4840]; u16 atk_instance; };
        struct { char _p1279[4844]; int xd90; };
        struct { char _p1280[4848]; int xd94; };
        struct { char _p1281[4852]; int xd98; };
        struct { char _p1282[4856]; float xd9c; };
        struct { char _p1283[4860]; int xda0; };
        struct { char _p1284[4867]; unsigned char : 7; unsigned char xda4_80 : 1; };
        struct { char _p1285[4867]; unsigned char : 6; unsigned char xda4_40 : 1; };
        struct { char _p1286[4867]; unsigned char : 5; unsigned char xda4_20 : 1; };
        struct { char _p1287[4867]; unsigned char : 4; unsigned char xda4_10 : 1; };
        struct { char _p1288[4867]; unsigned char : 3; unsigned char xda4_08 : 1; };
        struct { char _p1289[4867]; unsigned char : 2; unsigned char xda4_04 : 1; };
        struct { char _p1290[4867]; unsigned char : 1; unsigned char xda4_02 : 1; };
        struct { char _p1291[4867]; unsigned char xda4_01 : 1; };
        struct { char _p1292[4866]; unsigned char : 7; unsigned char xda5_80 : 1; };
        struct { char _p1293[4866]; unsigned char : 6; unsigned char xda5_40 : 1; };
        struct { char _p1294[4866]; unsigned char : 5; unsigned char xda5_20 : 1; };
        struct { char _p1295[4866]; unsigned char : 4; unsigned char xda5_10 : 1; };
        struct { char _p1296[4866]; unsigned char : 3; unsigned char xda5_08 : 1; };
        struct { char _p1297[4866]; unsigned char : 2; unsigned char xda5_04 : 1; };
        struct { char _p1298[4866]; unsigned char : 1; unsigned char xda5_02 : 1; };
        struct { char _p1299[4866]; unsigned char xda5_01 : 1; };
        struct { char _p1300[4866]; char xda6; };
        struct { char _p1301[4867]; char xda7; };
        struct { char _p1302[4868]; char xda8; };
        struct { char _p1303[4869]; char xda9; };
        struct { char _p1304[4870]; unsigned char : 7; unsigned char xdaa1 : 1; };
        struct { char _p1305[4870]; unsigned char : 6; unsigned char show_center_sphere : 1; };
        struct { char _p1306[4870]; unsigned char : 5; unsigned char show_item_pickup : 1; };
        struct { char _p1307[4870]; unsigned char : 4; unsigned char show_footstool : 1; };
        struct { char _p1308[4870]; unsigned char : 3; unsigned char xda8_x8 : 1; };
        struct { char _p1309[4870]; unsigned char : 2; unsigned char show_dynamics : 1; };
        struct { char _p1310[4870]; unsigned char : 1; unsigned char show_hit : 1; };
        struct { char _p1311[4870]; unsigned char show_model : 1; };
        struct { char _p1316[4872]; struct {
            union {
                char _mex_span[20];
                struct { int flag1; };
                struct { char _p1312[4]; int flag2; };
                struct { char _p1313[8]; int flag3; };
                struct { char _p1314[12]; int flag4; };
                struct { char _p1315[16]; int flag5; };
            };
        } itcmd_var; };
        struct { char _p1317[4892]; int xdc0; };
        struct { char _p1318[4896]; int xdc4; };
        struct { char _p1319[4901]; u8 xdc9_1 : 1; };
        struct { char _p1320[4901]; u8 : 1; u8 is_hitlag : 1; };
        struct { char _p1321[4901]; u8 : 2; u8 freeze : 1; };
        struct { char _p1322[4901]; u8 : 3; u8 xdc9_10 : 1; };
        struct { char _p1323[4902]; u16 : 1; u16 xdca1 : 1; };
        struct { char _p1324[4902]; u16 xdca2 : 1; };
        struct { char _p1325[4900]; u16 : 15; u16 xdca3 : 1; };
        struct { char _p1326[4902]; u16 : 3; u16 xdca4 : 1; };
        struct { char _p1327[4902]; u16 : 4; u16 xdca5 : 1; };
        struct { char _p1328[4902]; u16 : 5; u16 can_hold : 1; };
        struct { char _p1329[4902]; u16 : 6; u16 xdca7 : 1; };
        struct { char _p1330[4902]; u16 : 7; u16 rotate_axis : 2; };
        struct { char _p1331[4902]; u16 : 9; u16 rotate_axis_enable : 1; };
        struct { char _p1332[4902]; u16 : 10; u16 xdcb_x20 : 1; };
        struct { char _p1333[4902]; u16 : 11; u16 xdcb_x10 : 1; };
        struct { char _p1334[4902]; u16 : 12; u16 can_nudge : 1; };
        struct { char _p1335[4902]; u16 : 15; u16 xdcb_7 : 1; };
        struct { char _p1336[4904]; unsigned char xdcc1 : 1; };
        struct { char _p1337[4904]; unsigned char : 1; unsigned char xdcc2 : 1; };
        struct { char _p1338[4904]; unsigned char : 2; unsigned char xdcc3 : 1; };
        struct { char _p1339[4904]; unsigned char : 3; unsigned char isCheckBlastzone : 1; };
        struct { char _p1340[4904]; unsigned char : 7; unsigned char isCheckRightBlastzone : 1; };
        struct { char _p1341[4904]; unsigned char : 6; unsigned char isCheckLeftBlastzone : 1; };
        struct { char _p1342[4904]; unsigned char : 5; unsigned char isCheckUpBlastzone : 1; };
        struct { char _p1343[4904]; unsigned char : 4; unsigned char isCheckDownBlastzone : 1; };
        struct { char _p1344[4905]; unsigned char cam_kind : 2; };
        struct { char _p1345[4905]; unsigned char : 2; unsigned char no_play_hold_drop_throw_sfx : 1; };
        struct { char _p1346[4905]; unsigned char : 3; unsigned char xdcd4 : 1; };
        struct { char _p1347[4905]; unsigned char : 4; unsigned char xdcd5 : 1; };
        struct { char _p1348[4905]; unsigned char : 5; unsigned char xdcd6 : 1; };
        struct { char _p1349[4905]; unsigned char : 6; unsigned char xdcd7 : 1; };
        struct { char _p1350[4905]; unsigned char : 7; unsigned char xdcd8 : 1; };
        struct { char _p1351[4906]; unsigned char : 7; unsigned char xdce1 : 1; };
        struct { char _p1352[4906]; unsigned char : 6; unsigned char xdce2 : 1; };
        struct { char _p1353[4906]; unsigned char : 5; unsigned char xdce3 : 1; };
        struct { char _p1354[4906]; unsigned char : 4; unsigned char xdce4 : 1; };
        struct { char _p1355[4906]; unsigned char : 3; unsigned char xdce5 : 1; };
        struct { char _p1356[4906]; unsigned char : 2; unsigned char xdce6 : 1; };
        struct { char _p1357[4906]; unsigned char : 1; unsigned char is_detect : 1; };
        struct { char _p1358[4906]; unsigned char xdce8 : 1; };
        struct { char _p1359[4907]; unsigned char : 7; unsigned char xdcf1 : 1; };
        struct { char _p1360[4907]; unsigned char : 6; unsigned char xdcf2 : 1; };
        struct { char _p1361[4907]; unsigned char : 5; unsigned char xdcf3 : 1; };
        struct { char _p1362[4907]; unsigned char : 4; unsigned char xdcf4 : 1; };
        struct { char _p1363[4907]; unsigned char : 3; unsigned char xdcf5 : 1; };
        struct { char _p1364[4907]; unsigned char : 2; unsigned char xdcf6 : 1; };
        struct { char _p1365[4907]; unsigned char : 1; unsigned char is_hurt_by_fighter : 1; };
        struct { char _p1366[4907]; unsigned char xdcf8 : 1; };
        struct { char _p1367[4908]; unsigned char : 7; unsigned char is_footstool : 1; };
        struct { char _p1368[4908]; unsigned char : 6; unsigned char xdd0_x40 : 1; };
        struct { char _p1369[4908]; unsigned char : 5; unsigned char xdd0_x20 : 1; };
        struct { char _p1370[4908]; unsigned char : 4; unsigned char xdd0_x10 : 1; };
        struct { char _p1371[4908]; unsigned char : 3; unsigned char is_grabbable : 1; };
        struct { char _p1372[4908]; unsigned char : 2; unsigned char xdd0_x04 : 1; };
        struct { char _p1373[4908]; unsigned char : 1; unsigned char xdd0_x02 : 1; };
        struct { char _p1374[4908]; unsigned char xdd0_x01 : 1; };
        struct { char _p1500[4912]; struct {
            union {
                char _mex_span[504];
                struct { int var1; };
                struct { char _p1375[4]; int var2; };
                struct { char _p1376[12]; int var3; };
                struct { char _p1377[16]; int var4; };
                struct { char _p1378[16]; int var5; };
                struct { char _p1379[20]; int var6; };
                struct { char _p1380[24]; int var7; };
                struct { char _p1381[28]; int var8; };
                struct { char _p1382[32]; int var9; };
                struct { char _p1383[36]; int var10; };
                struct { char _p1384[40]; int var11; };
                struct { char _p1385[44]; int var12; };
                struct { char _p1386[48]; int xe04; };
                struct { char _p1387[52]; int xe08; };
                struct { char _p1388[56]; int xe0c; };
                struct { char _p1389[60]; int xe10; };
                struct { char _p1390[64]; int xe14; };
                struct { char _p1391[68]; int xe18; };
                struct { char _p1392[72]; int xe1c; };
                struct { char _p1393[76]; int xe20; };
                struct { char _p1394[80]; int xe24; };
                struct { char _p1395[84]; int xe28; };
                struct { char _p1396[88]; int xe2c; };
                struct { char _p1397[92]; int xe30; };
                struct { char _p1398[96]; int xe34; };
                struct { char _p1399[100]; int xe38; };
                struct { char _p1400[104]; int xe3c; };
                struct { char _p1401[108]; int xe40; };
                struct { char _p1402[112]; int xe44; };
                struct { char _p1403[116]; int xe48; };
                struct { char _p1404[120]; int xe4c; };
                struct { char _p1405[124]; int xe50; };
                struct { char _p1406[128]; int xe54; };
                struct { char _p1407[132]; int xe58; };
                struct { char _p1408[136]; int xe5c; };
                struct { char _p1409[140]; int xe60; };
                struct { char _p1410[144]; int xe64; };
                struct { char _p1411[148]; int xe68; };
                struct { char _p1412[152]; int xe6c; };
                struct { char _p1413[156]; int xe70; };
                struct { char _p1414[160]; int xe74; };
                struct { char _p1415[164]; int xe78; };
                struct { char _p1416[168]; int xe7c; };
                struct { char _p1417[172]; int xe80; };
                struct { char _p1418[176]; int xe84; };
                struct { char _p1419[180]; int xe88; };
                struct { char _p1420[184]; int xe8c; };
                struct { char _p1421[188]; int xe90; };
                struct { char _p1422[192]; int xe94; };
                struct { char _p1423[196]; int xe98; };
                struct { char _p1424[200]; int xe9c; };
                struct { char _p1425[204]; int xea0; };
                struct { char _p1426[208]; int xea4; };
                struct { char _p1427[212]; int xea8; };
                struct { char _p1428[216]; int xeac; };
                struct { char _p1429[220]; int xeb0; };
                struct { char _p1430[224]; int xeb4; };
                struct { char _p1431[228]; int xeb8; };
                struct { char _p1432[232]; int xebc; };
                struct { char _p1433[236]; int xec0; };
                struct { char _p1434[240]; int xec4; };
                struct { char _p1435[244]; int xec8; };
                struct { char _p1436[248]; int xecc; };
                struct { char _p1437[252]; int xed0; };
                struct { char _p1438[256]; int xed4; };
                struct { char _p1439[260]; int xed8; };
                struct { char _p1440[264]; int xedc; };
                struct { char _p1441[268]; int xee0; };
                struct { char _p1442[272]; int xee4; };
                struct { char _p1443[276]; int xee8; };
                struct { char _p1444[280]; int xeec; };
                struct { char _p1445[284]; int xef0; };
                struct { char _p1446[288]; int xef4; };
                struct { char _p1447[292]; int xef8; };
                struct { char _p1448[296]; int xefc; };
                struct { char _p1449[300]; int xf00; };
                struct { char _p1450[304]; int xf04; };
                struct { char _p1451[308]; int xf08; };
                struct { char _p1452[312]; int xf0c; };
                struct { char _p1453[316]; int xf10; };
                struct { char _p1454[320]; int xf14; };
                struct { char _p1455[324]; int xf18; };
                struct { char _p1456[328]; int xf1c; };
                struct { char _p1457[332]; int xf20; };
                struct { char _p1458[336]; int xf24; };
                struct { char _p1459[340]; int xf28; };
                struct { char _p1460[344]; int xf2c; };
                struct { char _p1461[348]; int xf30; };
                struct { char _p1462[352]; int xf34; };
                struct { char _p1463[356]; int xf38; };
                struct { char _p1464[360]; int xf3c; };
                struct { char _p1465[364]; int xf40; };
                struct { char _p1466[368]; int xf44; };
                struct { char _p1467[372]; int xf48; };
                struct { char _p1468[376]; int xf4c; };
                struct { char _p1469[380]; int xf50; };
                struct { char _p1470[384]; int xf54; };
                struct { char _p1471[388]; int xf58; };
                struct { char _p1472[392]; int xf5c; };
                struct { char _p1473[396]; int xf60; };
                struct { char _p1474[400]; int xf64; };
                struct { char _p1475[404]; int xf68; };
                struct { char _p1476[408]; int xf6c; };
                struct { char _p1477[412]; int xf70; };
                struct { char _p1478[416]; int xf74; };
                struct { char _p1479[420]; int xf78; };
                struct { char _p1480[424]; int xf7c; };
                struct { char _p1481[428]; int xf80; };
                struct { char _p1482[432]; int xf84; };
                struct { char _p1483[436]; int xf88; };
                struct { char _p1484[440]; int xf8c; };
                struct { char _p1485[448]; int xf90; };
                struct { char _p1486[448]; int xf94; };
                struct { char _p1487[452]; int xf98; };
                struct { char _p1488[456]; int xf9c; };
                struct { char _p1489[460]; int xfa0; };
                struct { char _p1490[464]; int xfa4; };
                struct { char _p1491[468]; int xfa8; };
                struct { char _p1492[472]; int xfac; };
                struct { char _p1493[476]; int xfb0; };
                struct { char _p1494[480]; int xfb4; };
                struct { char _p1495[484]; int xfb8; };
                struct { char _p1496[488]; int xfbc; };
                struct { char _p1497[492]; int xfc0; };
                struct { char _p1498[496]; int xfc4; };
                struct { char _p1499[500]; int xfc8; };
            };
        } item_var; };
    };
};

/*** static reference ***/

extern char mu_mx_it_804D6D28[] __asm__("it_804D6D28");
static itPublicData **stc_itPublicData = (void *)(mu_mx_it_804D6D28 + 0);
extern char mu_mx_it_804D6D38[] __asm__("it_804D6D38");
static ItemDesc **stc_itdesc_enemies = (void *)(mu_mx_it_804D6D38 + 0);

/*** Functions ***/
void Item_IndexStageItem(ItemDesc *item_desc, int index);
void Item_Hold(GOBJ *item, GOBJ *fighter, int boneID);
void Item_Catch(GOBJ *fighter, int unk);
void Item_StoreItemDataToCharItemTable(int articleData, int articleID);
void Items_StoreTimeout(GOBJ *item, float timeout);
GOBJ *Item_CreateItem(SpawnItem *item_spawn); // sorry for confusion, use this one for best results
GOBJ *Item_CreateItem1(SpawnItem *item_spawn);
GOBJ *Item_CreateItem2(SpawnItem *item_spawn);
GOBJ *Item_CreateItem3(SpawnItem *item_spawn);
GOBJ *Item_CreateMapItem(int index, int initial_state, void *data, JOBJ *jobj, void *onHit, void *onHurt, void *onUnk);
void Item_Destroy(GOBJ *item);
int Item_CollGround_PassLedge(GOBJ *item, void *callback);
int Item_CollGround_StopLedge(GOBJ *item, void *callback);
int Item_CollAir_Bounce(GOBJ *item, void *callback);
int Item_CollAir_Land(GOBJ *item, void *callback);
int Item_CollAir_NoCB(GOBJ *item);
void Item_SetGroundedUpright(GOBJ *item);
void ItemStateChange(GOBJ *item, int stateID, int flags);
int ItemFrameTimer(GOBJ *item);
void Item_PlaceOnGroundBelow(GOBJ *item);
int Item_CheckIfTouchingWall(GOBJ *item, float *unk[]);
void Item_InitGrab(ItemData *item, int unk, void *OnItem, void *OnFighter);
void Item_ResetAllHitPlayers(ItemData *item);
int Item_CountActiveItems(int itemID);
void Item_CopyDevelopState(GOBJ *item, GOBJ *fighter);
void GXLink_Item(GOBJ *gobj, int pass);
void Item_UpdateSpin(GOBJ *item, float unk);
void Item_SetAirborne(ItemData *ip);
void Item_SetGrounded(ItemData *ip);
void Item_SetLifeTimer(GOBJ *item, float lifetime); // sets frames until item is destroyed
int Item_DecLifeTimer(GOBJ *item);                  // returns isEnd bool
JOBJ *Item_GetBoneJOBJ(GOBJ *item, int bone_index);
int Item_CheckIfEnabled(); // returns bool regarding if items are enabled for this match
int Item_GetGroundAirState(GOBJ *item);
void Item_UpdatePhysAndColl(GOBJ *item);
void Item_ProjectileVelocityCalculate(GOBJ *item, float fall_speed, float max_fall_speed);
void Item_PlayOnDestroySFXAgain(ItemData *, int sfxid, int volume, int panning);
void Item_UpdatePositionCollision(GOBJ *item);
void Item_ScaleToPlayerSize(GOBJ *item);
void Item_AnimateAndUpdateSubactions(GOBJ *item);
void Barrel_EnterBreak(GOBJ *item);
void Item_EnableHitlagFlag(GOBJ *item);
void Item_ReflectVelocity(GOBJ *item);
void Item_Throw(GOBJ *item, float unk, Vec3 *pos, Vec3 *vel);
JOBJ *Item_GetHeldBone(GOBJ *item);
void Item_BounceOffVictim(GOBJ *item);
void Item_BounceOffShield(GOBJ *item);
int Item_GenerateHitExceptionID();
int Item_CheckHeavy(GOBJ *item);
void Item_SetUngrabbable(GOBJ *item);
void Item_SetJobjHidden(GOBJ *item);
float Item_GetHitboxDamage(GOBJ *item);
void Item_SetHitboxDamage(itHit *hitbox, int damage, GOBJ *item);
void Item_RemoveAllHitboxes(GOBJ *item);
void Item_SetHurtboxTangibility(GOBJ *item, int tangibility);
void Item_CreateHurtbox(GOBJ *item, float x1, float y1, float z1, float x2, float y2, float z2, float size);
void Item_UpdateHurtboxes(GOBJ *item);
void Item_UpdateAnimationAndScriptTimers(GOBJ *item);
void Item_ClearVelocity(GOBJ *item);
void Item_ResetVelocity(GOBJ *item);
void Item_UpdateECBTopN(GOBJ *item);
int Item_GetWallCollFlags(GOBJ *item);
void Item_UpdateHitboxDamage(itHit *hit, int dmg, GOBJ *item);
GOBJ *Item_GiveOwnershipToAttacker(GOBJ *item);
char Item_GetHoldKind(GOBJ *item);
float Item_GetDistanceFromPointSquared(GOBJ *item, Vec3 *position);
void Item_DestroyAndRemovedGrabbed(GOBJ *item, int flag, float damage);
bool Item_RemoveFighterReference(GOBJ *item, GOBJ *fighter);
void Item_ClearHitlagFlag(GOBJ *item);

void Egg_Destroy(GOBJ *egg_gobj);
#endif
