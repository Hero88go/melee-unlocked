#ifndef MEX_H_CSS
#define MEX_H_CSS

#include "structs.h"
#include "datatypes.h"
#include "fighter.h"
#include "scene.h"

#define MENU_BUTTON_UP 0x1
#define MENU_BUTTON_DOWN 0x2
#define MENU_BUTTON_LEFT 0x4
#define MENU_BUTTON_RIGHT 0x8
#define MENU_BUTTON_A 0x10
#define MENU_BUTTON_B 0x20
#define MENU_BUTTON_L 0x40
#define MENU_BUTTON_R 0x80
#define MENU_BUTTON_START 0x100
#define MENU_BUTTON_X 0x400
#define MENU_BUTTON_Y 0x800

enum CSSKind
{
    SLCHRKIND_VS,              // regular vs
    SLCHRKIND_SNAPSHOT,        // camera mode
    SLCHRKIND_STAMINA,         //
    SLCHRKIND_SUDDEN,          //
    SLCHRKIND_GIANT,           //
    SLCHRKIND_TINY,            //
    SLCHRKIND_INVISIBLE,       //
    SLCHRKIND_FIXEDCAM,        //
    SLCHRKIND_SNGLBTN,         //
    SLCHRKIND_FAST,            //
    SLCHRKIND_SLOW,            //
    SLCHRKIND_CLASSIC,         //
    SLCHRKIND_ADV,             //
    SLCHRKIND_ALLSTAR,         //
    SLCHRKIND_EVENT,           //
    SLCHRKIND_TARGET,          //
    SLCHRKIND_HOMERUN,         //
    SLCHRKIND_MULTIMAN10,      //
    SLCHRKIND_MULTIMAN100,     //
    SLCHRKIND_MULTIMAN3MIN,    //
    SLCHRKIND_MULTIMAN15MIN,   //
    SLCHRKIND_MULTIMANENDLESS, //
    SLCHRKIND_MULTIMANCRUEL,   //
    SLCHRKIND_TRAINING,        //
};

enum CSSExitKind
{
    CSSEXIT_NONE,        // can be used to identify LRStart to go back to main menu
    CSSEXIT_SSS,         //
    CSSEXIT_MAINMENU,    //
    CSSEXIT_RULES,       //
    CSSEXIT_NAME,        // name entry
    CSSEXIT_SUBMENUOPEN, // in rules / name entry
};

/*** Structs ***/
struct CSSBackup
{
    u8 c_kind;  // 0x0
    u8 costume; // 0x1
    u8 nametag; // 0x2
    u8 event;   // 0x3
    u8 port;    // 0x4
    u8 x5;      // 0x5
    u8 x6;      // 0x6
    u8 x7;      // 0x7
    u8 x8;      // 0x8
};

struct MnSelectChrDataTable /* native twin, generated */
{
    union {
        char _mex_native_size[160];
    };
};

enum CSSCursorState
{
    SLCHRCUR_POINT,
    SLCHRCUR_HOLD,
    SLCHRCUR_OPEN,
    SLCHRCUR_HIDDEN,
};
struct CSSCursor /* native twin, generated */
{
    union {
        char _mex_native_size[24];
        struct { GOBJ *gobj; };
        struct { char _p4674[8]; u8 door_id; };
        struct { char _p4675[9]; u8 state; };
        struct { char _p4676[10]; u8 puck; };
        struct { char _p4677[11]; u8 x7; };
        struct { char _p4678[12]; u16 is_over_option; };
        struct { char _p4679[14]; u16 exit_timer; };
        struct { char _p4680[16]; Vec2 pos; };
    };
};

enum CSSObjectKind
{
    SLCHROBJKIND_P1PUCK,
    SLCHROBJKIND_P2PUCK,
    SLCHROBJKIND_P3PUCK,
    SLCHROBJKIND_P4PUCK,
    SLCHROBJKIND_P1CPU,
    SLCHROBJKIND_P2CPU,
    SLCHROBJKIND_P3CPU,
    SLCHROBJKIND_P4CPU,
    SLCHROBJKIND_P1HANDICAP,
    SLCHROBJKIND_P2HANDICAP,
    SLCHROBJKIND_P3HANDICAP,
    SLCHROBJKIND_P4HANDICAP,
};
enum CSSPuckState
{
    SLCHRPUCK_0,
    SLCHRPUCK_1,
    SLCHRPUCK_2,
    SLCHRPUCK_3,
};
struct CSSPuck /* native twin, generated */
{
    union {
        char _mex_native_size[32];
        struct { GOBJ *gobj; };
        struct { char _p4681[8]; u8 door_id; };
        struct { char _p4682[9]; u8 state; };
        struct { char _p4683[10]; u8 kind; };
        struct { char _p4684[11]; u8 anim_timer; };
        struct { char _p4685[12]; Vec2 pos_proj; };
        struct { char _p4686[20]; Vec2 pos_correct; };
    };
};

struct MnSlChrIcon
{
    u8 ft_hudindex; // 0x0, used for getting combo count @ 8025c0c4
    u8 c_kind;      // 0x1, icons external ID
    u8 state;       // 0x2, Dictates whether icon can be chosen. 0x0 = Not Unlocked, 0x1 = Unlocked (temp value), 0x2 = Unlocked and Displayed
    u8 anim_timer;  // 0x3, is made to be 0xC when the character is chosen.
    u8 joint_id_vs; // 0x4, vs icon background jobj ID
    u8 joint_id_1p; // 0x5, 1p icon background jobj ID
    int sfx;        // 0x8,
    float bound_l;  // 0xC
    float bound_r;  // 0x10
    float bound_u;  // 0x14
    float bound_d;  // 0x18
};

struct MnSlChrDoor
{
    u8 emblem_joint;            // 0x0
    u8 costume_joint;           // 0x1
    u8 team_joint;              // 0x2
    u8 door_joint;              // 0x3
    u8 bg_joint;                // 0x4
    u8 player_indicator_joint;  // 0x5, nametag window joint id (to scroll and choose a name)
    u8 slidername_joint;        // 0x6, slider name joint
    u8 cpuslider_joint;         // 0x7, used when only CPU is showing
    u8 cpuslider2_joint;        // 0x8, used when handicap is also showing
    u8 selected_since_load;     // 0x9, used to determine when the player made a selection since the CSS loaded
    u8 team;                    // 0xa
    u8 p_kind;                  // 0xb, PlayerKind, 0x0 = HMN, 0x1 = CPU, 0x3 = Closed
    u8 p_kind_prev;             // 0xc
    u8 costume;                 // 0xd
    u8 sel_icon;                // 0xe, icon this player has selected
    u8 sel_icon_prev;           // 0xf
    u8 dooranim_timer;          // 0x10
    u8 slideranim_timer;        // 0x11
    u8 is_hold_cpu_slider;      // 0x12
    u8 is_hold_handicap_slider; // 0x13
    float togglebtn_left;       // 0x14, HMN button bound
    float togglebtn_right;      // 0x18, HMN button bound
    float teambtn_left;         // 0x1C, team button bound
    float teambtn_right;        // 0x20, team button bound
};

struct MnSlChrTagData
{
    Text *name;         // 0x0
    Text *namelist;     // 0x4, used when opening the tag window
    float x8;           // 0x8
    float scroll_amt;   // xC, Text Y Offset to scroll up each frame
    float scroll_force; // x10
    int timer;          // x14, state timer
    u8 next_tag;        // 0x18, index of next empty tag
    u8 port;
    u8 state;   // 0x1a, is nametag window open bool
    u8 use_tag; // 0x1b, is using a nametag bool
};

struct MnSlChrTag
{
    MnSlChrTagData *tag_data; // 0x0
    u8 x4;                    // 0x4
    u8 list_joint;            // 0x5
    u8 name_joint;            // 0x6
    u8 x7;                    // 0x7
    u8 kostartext_joint;      // 0x8
    u8 x9;
    u8 xa;
    u8 xb;
};

struct MnSlChrKOStar
{
    Text *text; // 0x0
    float x4;   // 0x4
    u8 joint;   // 0x8
    int xc;
    int x10;
    int x14;
    int x18;
    int x1c;
};

struct MnSlChrKindData
{
    u16 mode_ffa_frame;   // 0x0, anim frame used for the top left mode texture
    u16 mode_teams_frame; // 0x2, anim frame used for the top left mode texture
    int enter_sfx;        // 0x4, announcer sfx used when entering the CSS
};

struct MnSlChrData /* native twin, generated */
{
    union {
        char _mex_native_size[220];
        struct { u8 GaWName[0x1C]; };
        struct { char _p4695[28]; MnSlChrKindData kind_data[24]; };
    };
};

struct VSMinorData /* native twin, generated */
{
    union {
        char _mex_native_size[376];
        struct { u16 snglply_port; };
        struct { char _p4687[2]; u8 css_kind; };
        struct { char _p4688[3]; u8 exit_kind; };
        struct { char _p4689[8]; u8 *ko_data; };
        struct { char _p4694[16]; struct {
            union {
                char _mex_span[360];
                struct { u8 x8; };
                struct { char _p4690[1]; u8 x9; };
                struct { char _p4691[2]; u8 xa; };
                struct { char _p4692[4]; int xc; };
                struct { char _p4693[8]; MatchInit match_init; };
            };
        } vs_data; };
    };
};
struct SSSMinorData
{
    u8 x0; // is made 0 upon entry
    u8 x1;
    u8 x2;
    u8 auto_random_stage; // is -1 if random stage in rules is disabled
    u8 exit_kind;         //  0 = back, 1 = advance
    ScDataVS vs_data;
};

/*** Variables ***/
// static MnSlChrData *stc_css_data = (void *)0x803F0A48;         // 0x803f0a48
extern void *mu_tmce_ref_stc_css_data;
#define stc_css_data ((MnSlChrData *)((char *)mu_tmce_ref_stc_css_data + 0))
// static VSMinorData **stc_css_minorscene = (void *)0x804D6CB0;  // -0x49F0
extern void *mu_tmce_ref_stc_css_minorscene;
#define stc_css_minorscene ((VSMinorData * *)((char *)mu_tmce_ref_stc_css_minorscene + 0))
// static u8 *stc_css_regtagnum = (void *)0x804D6CF8;             // -0x49A8, number of registered tags
extern void *mu_tmce_ref_stc_css_regtagnum;
#define stc_css_regtagnum ((u8 *)((char *)mu_tmce_ref_stc_css_regtagnum + 0))
// static s8 *stc_css_name_ply = (void *)0x804D6CF9;              // -0x49A7, index of the player using the name entry menu
extern void *mu_tmce_ref_stc_css_name_ply;
#define stc_css_name_ply ((s8 *)((char *)mu_tmce_ref_stc_css_name_ply + 0))
// static HSD_Archive **stc_css_archive = (void *)0x804D6CD0;     // -0x49D0
extern void *mu_tmce_ref_stc_css_archive;
#define stc_css_archive ((HSD_Archive * *)((char *)mu_tmce_ref_stc_css_archive + 0))
// static HSD_Archive **stc_css_menuarchive = (void *)0x804D6CD4; // -0x49CC, ptr to MnMaExt archive
extern void *mu_tmce_ref_stc_css_menuarchive;
#define stc_css_menuarchive ((HSD_Archive * *)((char *)mu_tmce_ref_stc_css_menuarchive + 0))
// static u8 *stc_css_custom_rules = (void *)0x804D6CF4;          // -0x49AC
extern void *mu_tmce_ref_stc_css_custom_rules;
#define stc_css_custom_rules ((u8 *)((char *)mu_tmce_ref_stc_css_custom_rules + 0))
// static GOBJ **stc_css_menugobj = (void *)0x804D6CBC;           // -0x49E4
extern void *mu_tmce_ref_stc_css_menugobj;
#define stc_css_menugobj ((GOBJ * *)((char *)mu_tmce_ref_stc_css_menugobj + 0))
// static JOBJ **stc_css_menumodel = (void *)0x804D6CC0;          // -0x49E0
extern void *mu_tmce_ref_stc_css_menumodel;
#define stc_css_menumodel ((JOBJ * *)((char *)mu_tmce_ref_stc_css_menumodel + 0))
// static JOBJ **stc_css_trainingmodel = (void *)0x804D6CC4;      // -0x49DC
extern void *mu_tmce_ref_stc_css_trainingmodel;
#define stc_css_trainingmodel ((JOBJ * *)((char *)mu_tmce_ref_stc_css_trainingmodel + 0))
// static JOBJ **stc_css_highscoremodel = (void *)0x804D6CC8;     // -0x49D8
extern void *mu_tmce_ref_stc_css_highscoremodel;
#define stc_css_highscoremodel ((JOBJ * *)((char *)mu_tmce_ref_stc_css_highscoremodel + 0))
// static JOBJ **stc_css_cameramodel = (void *)0x804D6CCC;        // -0x49D4
extern void *mu_tmce_ref_stc_css_cameramodel;
#define stc_css_cameramodel ((JOBJ * *)((char *)mu_tmce_ref_stc_css_cameramodel + 0))
// static CSSCursor **stc_css_cursors = (void *)0x804A0BC0;       // 0x804a0bc0
extern void *mu_tmce_ref_stc_css_cursors;
#define stc_css_cursors ((CSSCursor * *)((char *)mu_tmce_ref_stc_css_cursors + 0))
// static CSSPuck **stc_css_pucks = (void *)0x804A0BD0;           // 0x804a0bd0
extern void *mu_tmce_ref_stc_css_pucks;
#define stc_css_pucks ((CSSPuck * *)((char *)mu_tmce_ref_stc_css_pucks + 0))

// static s8 *stc_css_hmnport = (void *)0x804D6CF0;                      // -0x49B0
extern void *mu_tmce_ref_stc_css_hmnport;
#define stc_css_hmnport ((s8 *)((char *)mu_tmce_ref_stc_css_hmnport + 0))
// static s8 *stc_css_cpuport = (void *)0x804D6CF1;                      // -0x49AF
extern void *mu_tmce_ref_stc_css_cpuport;
#define stc_css_cpuport ((s8 *)((char *)mu_tmce_ref_stc_css_cpuport + 0))
// static u8 *stc_css_delay = (void *)0x804D6CF2;                        // -0x49AE
extern void *mu_tmce_ref_stc_css_delay;
#define stc_css_delay ((u8 *)((char *)mu_tmce_ref_stc_css_delay + 0))
// static u8 *stc_css_exitkind = (void *)0x804D6CF6;                     // -0x49AA
extern void *mu_tmce_ref_stc_css_exitkind;
#define stc_css_exitkind ((u8 *)((char *)mu_tmce_ref_stc_css_exitkind + 0))
// static u8 *stc_css_maxply = (void *)0x804D6CF5;                       // -0x49AB
extern void *mu_tmce_ref_stc_css_maxply;
#define stc_css_maxply ((u8 *)((char *)mu_tmce_ref_stc_css_maxply + 0))
// static u8 *stc_css_is_ready_timer = (void *)0x804D6CF7;               // -0x49A9
extern void *mu_tmce_ref_stc_css_is_ready_timer;
#define stc_css_is_ready_timer ((u8 *)((char *)mu_tmce_ref_stc_css_is_ready_timer + 0))
// static u8 *stc_css_singeplyport = (void *)0x804D6CF0;                 // -0x4DE0
extern void *mu_tmce_ref_stc_css_singeplyport;
#define stc_css_singeplyport ((u8 *)((char *)mu_tmce_ref_stc_css_singeplyport + 0))
// static u8 *stc_menu_singeplyport = (void *)0x804D6598;                // -0x4DB8
extern void *mu_tmce_ref_stc_menu_singeplyport;
#define stc_menu_singeplyport ((u8 *)((char *)mu_tmce_ref_stc_menu_singeplyport + 0))
// static Text **stc_css_ply1_combo_text = (void *)0x804D6CDC;           // -0x49C4
extern void *mu_tmce_ref_stc_css_ply1_combo_text;
#define stc_css_ply1_combo_text ((Text * *)((char *)mu_tmce_ref_stc_css_ply1_combo_text + 0))
// static Text **stc_css_ply2_combo_text = (void *)0x804D6CE0;           // -0x49C0
extern void *mu_tmce_ref_stc_css_ply2_combo_text;
#define stc_css_ply2_combo_text ((Text * *)((char *)mu_tmce_ref_stc_css_ply2_combo_text + 0))
// static Text **stc_css_ply3_combo_text = (void *)0x804D6CE4;           // -0x49BC
extern void *mu_tmce_ref_stc_css_ply3_combo_text;
#define stc_css_ply3_combo_text ((Text * *)((char *)mu_tmce_ref_stc_css_ply3_combo_text + 0))
// static Text **stc_css_ply4_combo_text = (void *)0x804D6CE8;           // -0x49B8
extern void *mu_tmce_ref_stc_css_ply4_combo_text;
#define stc_css_ply4_combo_text ((Text * *)((char *)mu_tmce_ref_stc_css_ply4_combo_text + 0))
// static int *stc_css_bgtimer = (void *)0x804D6CEC;                     // -0x49B4
extern void *mu_tmce_ref_stc_css_bgtimer;
#define stc_css_bgtimer ((int *)((char *)mu_tmce_ref_stc_css_bgtimer + 0))
// static u8 *stc_css_hasreleasedb = (void *)0x804D6CF3;                 // -0x49AD
extern void *mu_tmce_ref_stc_css_hasreleasedb;
#define stc_css_hasreleasedb ((u8 *)((char *)mu_tmce_ref_stc_css_hasreleasedb + 0))
// static MnSelectChrDataTable **stc_css_datatable = (void *)0x804D6CB4; // -0x49EC
extern void *mu_tmce_ref_stc_css_datatable;
#define stc_css_datatable ((MnSelectChrDataTable * *)((char *)mu_tmce_ref_stc_css_datatable + 0))
// static JOBJSet **stc_css_jobjsets = (void *)0x804D6CD8;               // -0x49C8
extern void *mu_tmce_ref_stc_css_jobjsets;
#define stc_css_jobjsets ((JOBJSet * *)((char *)mu_tmce_ref_stc_css_jobjsets + 0))
extern char mu_mx_MenMain_cam[] __asm__("MenMain_cam");
static COBJDesc **stc_css_cobjdesc = (void *)(mu_mx_MenMain_cam + 0);              // -0x4ADC
// static GOBJ **stc_css_camgobj = (void *)0x804D6CB8;                   // -0x49E8
extern void *mu_tmce_ref_stc_css_camgobj;
#define stc_css_camgobj ((GOBJ * *)((char *)mu_tmce_ref_stc_css_camgobj + 0))
extern char mu_mx_HSD_PadCopyStatus[] __asm__("HSD_PadCopyStatus");
static HSD_Pad *stc_css_pad = (void *)(mu_mx_HSD_PadCopyStatus + 0);                     // 0x804c20bc
// static u8 *stc_css_unkarr = (void *)0x804D50C8;                       // 0x804d50c8
extern void *mu_tmce_ref_stc_css_unkarr;
#define stc_css_unkarr ((u8 *)((char *)mu_tmce_ref_stc_css_unkarr + 0))

/*** Functions ***/
void MainMenu_CamRotateThink(GOBJ *gobj);
int MainMenu_GetPadDown(int controller_index);  // returns HSD_BUTTON vals
u64 MainMenu_GetPadRapid(int controller_index); // returns HSD_BUTTON vals
int MainMenu_CheckForLRA();
void MainMenu_DestroyAllTextCanvases(); // destroys all SIS canvases
void CSS_FreeText();                    //
int CSS_GetNametagRumble(int player, u8 tag);
void CSS_InitPlayerData(PlayerData *player);
void CSS_MenuModelThink(GOBJ *gobj);
void CSS_CursorThink(GOBJ *gobj);
void CSS_PuckThink(GOBJ *gobj);
void CSS_TagThink(GOBJ *gobj);
void CSS_UpdateRulesText();
void CSS_UpdateKOStars(int ply, int unk);
void CSS_StartThink(GOBJ *gobj);
int CSS_GetHandicapValue(int ply, int nametag_id);
void CSS_SetModeTexture(int css_kind);
int CSS_ReturnPuck(int ply);
int CSS_SetRandomFighter(int ply, int unk);
void CSS_UpdateCSP(int ply);
int CSS_GetCostumeNum(int ext_id);
int CSS_GetCostumeRed(int c_kind);
int CSS_GetCostumeBlue(int c_kind);
int CSS_GetCostumeGreen(int c_kind);
void CSS_PlayFighterName(int ext_id);
void CSS_CostumeChange(int port, int button_down);
void CSS_UpdateCSPTexture(int port, int costume, int is_none);
#endif
