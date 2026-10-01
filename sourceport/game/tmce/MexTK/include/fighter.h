#ifndef MEX_H_FIGHTER
#define MEX_H_FIGHTER

#include "structs.h"
#include "datatypes.h"
#include "obj.h"
#include "gx.h"
#include "color.h"
#include "effects.h"
#include "match.h"
#include "collision.h"
#include "dynamics.h"

// Internal IDs
typedef enum FighterKind
{
    FTKIND_MARIO,
    FTKIND_FOX,
    FTKIND_FALCON,
    FTKIND_DK,
    FTKIND_KIRBY,
    FTKIND_BOWSER,
    FTKIND_LINK,
    FTKIND_SHEIK,
    FTKIND_NESS,
    FTKIND_PEACH,
    FTKIND_POPO,
    FTKIND_NANA,
    FTKIND_PIKACHU,
    FTKIND_SAMUS,
    FTKIND_YOSHI,
    FTKIND_JIGGLYPUFF,
    FTKIND_MEWTWO,
    FTKIND_LUIGI,
    FTKIND_MARTH,
    FTKIND_ZELDA,
    FTKIND_YOUNGLINK,
    FTKIND_DRMARIO,
    FTKIND_FALCO,
    FTKIND_PICHU,
    FTKIND_GAW,
    FTKIND_GANONDORF,
    FTKIND_ROY,
    FTKIND_MASTERHAND,
    FTKIND_CRAZYHAND,
    FTKIND_BOY,
    FTKIND_GIRL,
    FTKIND_GIGABOWSER,
    FTKIND_SANDBAG,
} FighterKind;

// External IDs
typedef enum CharacterKind
{
    CKIND_FALCON,
    CKIND_DK,
    CKIND_FOX,
    CKIND_GAW,
    CKIND_KIRBY,
    CKIND_BOWSER,
    CKIND_LINK,
    CKIND_LUIGI,
    CKIND_MARIO,
    CKIND_MARTH,
    CKIND_MEWTWO,
    CKIND_NESS,
    CKIND_PEACH,
    CKIND_PIKACHU,
    CKIND_ICECLIMBERS,
    CKIND_JIGGLYPUFF,
    CKIND_SAMUS,
    CKIND_YOSHI,
    CKIND_ZELDA,
    CKIND_SHEIK,
    CKIND_FALCO,
    CKIND_YOUNGLINK,
    CKIND_DRMARIO,
    CKIND_ROY,
    CKIND_PICHU,
    CKIND_GANONDORF,
    CKIND_MASTERHAND,
    CKIND_BOY,
    CKIND_GIRL,
    CKIND_GIGABOWSER,
    CKIND_CRAZYHAND,
    CKIND_SANDBAG,
    CKIND_POPO,
} CharacterKind;
typedef enum PlayerKind
{
    PKIND_HMN,
    PKIND_CPU,
    PKIND_DEMO,
    PKIND_NONE,
} PlayerKind;
typedef enum TeamKind
{
    TEAMKIND_RED,
    TEAMKIND_BLUE,
    TEAMKIND_GREEN,
    TEAMKIND_CPUTRAIN, // cpu in training mode is on team 4 o_o
    TEAMKIND_5,
    TEAMKIND_6,
    TEAMKIND_NUM,
} TeamKind;
enum CPUType
{
    CPTP_STAY,
    CPTP_WALK,
    CPTP_ESCAPE,
    CPTP_JUMP,
    CPTP_NORMAL,
    CPTP_MANUAL,
    CPTP_NANA,
    CPTP_DEFENSIVE,
    CPTP_STRUGGLE,
    CPTP_FREAK,
    CPTP_COOPERATE,
    CPTP_SPLWLINK,
    CPTP_SPLWSAMUS,
    CPTP_ONLYITEM,
    CPTP_EVZELDA,
    CPTP_NOACT,
    CPTP_AIR,
    CPTP_ITEM,
};

// fighter callback priorities
enum FtPri
{
    FTPRI_HITLAG,
    FTPRI_ANIM,
    FTPRI_CPU,
    FTPRI_IASA,
    FTPRI_PHYS,
    FTPRI_5, // nothing
    FTPRI_ENVCOLL,
    FTPRI_IK,
    FTPRI_ACCESSORY,
    FTPRI_GFX,
    FTPRI_10,
    FTPRI_11,
    FTPRI_GRABCOLL,
    FTPRI_HITCOLL,
    FTPRI_DMGAPPLY,
    FTPRI_15,
    FTPRI_DYNAMICS,
    FTPRI_17,
    FTPRI_CAM,
    FTPRI_19,
    FTPRI_20,
    FTPRI_21,
    FTPRI_STATS,
};

// action state flags
#define FIGHTER_FASTFALL_PRESERVE 0x1
#define FIGHTER_GFX_PRESERVE 0x2
#define FIGHTER_HITSTATUS_COLANIM_PRESERVE 0x4 // Preserve full body collision state //
#define FIGHTER_HIT_NOUPDATE 0x8               // Keep hitboxes
#define FIGHTER_MODEL_NOUPDATE 0x10            // Ignore model state change (?)
#define FIGHTER_ANIMVEL_NOUPDATE 0x20
#define FIGHTER_UNK_0x40 0x40
#define FIGHTER_MATANIM_NOUPDATE 0x80          // Ignore switching to character's "hurt" textures (?) //
#define FIGHTER_THROW_EXCEPTION_NOUPDATE 0x100 // Resets thrower GObj pointer to NULL if false? //
#define FIGHTER_SFX_PRESERVE 0x200
#define FIGHTER_PARASOL_NOUPDATE 0x400 // Ignore Parasol state change //
#define FIGHTER_RUMBLE_NOUPDATE 0x800  // Ignore rumble update? //
#define FIGHTER_COLANIM_NOUPDATE 0x1000
#define FIGHTER_ACCESSORY_PRESERVE 0x2000 // Keep respawn platform? //
#define FIGHTER_CMD_UPDATE 0x4000         // Run all Subaction Events up to the current animation frame //
#define FIGHTER_NAMETAGVIS_NOUPDATE 0x8000
#define FIGHTER_PART_HITSTATUS_COLANIM_PRESERVE 0x10000 // Assume this is for individual bones? //
#define FIGHTER_SWORDTRAIL_PRESERVE 0x20000
#define FIGHTER_ITEMVIS_NOUPDATE 0x40000 // Used by Ness during Up/Down Smash, I suppose this is what the flag does //
#define FIGHTER_SKIP_UNK_0x2222 0x80000  // Skips updating bit 0x20 of 0x2222? //
#define FIGHTER_PHYS_UNKUPDATE 0x100000
#define FIGHTER_FREEZESTATE 0x200000 // Sets anim rate to 0x and some other stuff
#define FIGHTER_MODELPART_VIS_NOUPDATE 0x400000
#define FIGHTER_METALB_NOUPDATE 0x800000
#define FIGHTER_UNK_0x1000000 0x1000000
#define FIGHTER_ATTACKCOUNT_NOUPDATE 0x2000000
#define FIGHTER_MODEL_FLAG_NOUPDATE 0x4000000
#define FIGHTER_UNK_0x2227 0x8000000
#define FIGHTER_HITSTUN_FLAG_NOUPDATE 0x10000000
#define FIGHTER_ANIM_NOUPDATE 0x20000000  // Keeps current fp animation?
#define FIGHTER_UNK_0x40000000 0x40000000 // Unused?
#define FIGHTER_UNK_0x80000000 0x80000000 // Unused?

enum FtCommonBoneNames
{
    TopN,
    TransN,
    XRotN,
    YRotN,
    HipN,
    WaistN,
    LLegJA,
    LLegJ,
    LKneeJ,
    LFootJA,
    LFootJ,
    RLegJA,
    RLegJ,
    RKneeJ,
    RFootJA,
    RFootJ,
    WaistN2,
    BustN,
    LShoulderN,
    LShoulderJA,
    LShoulderJ,
    LArmJ,
    LHandN,
    L1stNa,
    L1stNb,
    L2ndNa,
    L2ndNb,
    L3rdNa,
    L3rdNb,
    L4thNa,
    L4thNb,
    LHaveN,
    LThumbNa,
    LThumbNb,
    NeckN,
    HeadN,
    RShoulderN,
    RShoulderJA,
    RShoulderJ,
    RArmJ,
    RHandN,
    R1stNa,
    R1stNb,
    R2ndNa,
    R2ndNb,
    R3rdNa,
    R3rdNb,
    R4thNa,
    R4thNb,
    RHaveN,
    RThumbNa,
    RThumbNb,
    ThrowN,
    Extra
};

// Fighter States
enum FtStateNames
{
    ASID_DEADDOWN,
    ASID_DEADLEFT,
    ASID_DEADRIGHT,
    ASID_DEADUP,
    ASID_DEADUPSTAR,
    ASID_DEADUPSTARICE,
    ASID_DEADUPFALL,
    ASID_DEADUPFALLHITCAMERA,
    ASID_DEADUPFALLHITCAMERAFLAT,
    ASID_DEADUPFALLICE,
    ASID_DEADUPFALLHITCAMERAICE,
    ASID_SLEEP,
    ASID_REBIRTH,
    ASID_REBIRTHWAIT,
    ASID_WAIT,
    ASID_WALKSLOW,
    ASID_WALKMIDDLE,
    ASID_WALKFAST,
    ASID_TURN,
    ASID_TURNRUN,
    ASID_DASH,
    ASID_RUN,
    ASID_RUNDIRECT,
    ASID_RUNBRAKE,
    ASID_KNEEBEND,
    ASID_JUMPF,
    ASID_JUMPB,
    ASID_JUMPAERIALF,
    ASID_JUMPAERIALB,
    ASID_FALL,
    ASID_FALLF,
    ASID_FALLB,
    ASID_FALLAERIAL,
    ASID_FALLAERIALF,
    ASID_FALLAERIALB,
    ASID_FALLSPECIAL,
    ASID_FALLSPECIALF,
    ASID_FALLSPECIALB,
    ASID_DAMAGEFALL,
    ASID_SQUAT,
    ASID_SQUATWAIT,
    ASID_SQUATRV,
    ASID_LANDING,
    ASID_LANDINGFALLSPECIAL,
    ASID_ATTACK11,
    ASID_ATTACK12,
    ASID_ATTACK13,
    ASID_ATTACK100START,
    ASID_ATTACK100LOOP,
    ASID_ATTACK100END,
    ASID_ATTACKDASH,
    ASID_ATTACKS3HI,
    ASID_ATTACKS3HIS,
    ASID_ATTACKS3S,
    ASID_ATTACKS3LWS,
    ASID_ATTACKS3LW,
    ASID_ATTACKHI3,
    ASID_ATTACKLW3,
    ASID_ATTACKS4HI,
    ASID_ATTACKS4HIS,
    ASID_ATTACKS4S,
    ASID_ATTACKS4LWS,
    ASID_ATTACKS4LW,
    ASID_ATTACKHI4,
    ASID_ATTACKLW4,
    ASID_ATTACKAIRN,
    ASID_ATTACKAIRF,
    ASID_ATTACKAIRB,
    ASID_ATTACKAIRHI,
    ASID_ATTACKAIRLW,
    ASID_LANDINGAIRN,
    ASID_LANDINGAIRF,
    ASID_LANDINGAIRB,
    ASID_LANDINGAIRHI,
    ASID_LANDINGAIRLW,
    ASID_DAMAGEHI1,
    ASID_DAMAGEHI2,
    ASID_DAMAGEHI3,
    ASID_DAMAGEN1,
    ASID_DAMAGEN2,
    ASID_DAMAGEN3,
    ASID_DAMAGELW1,
    ASID_DAMAGELW2,
    ASID_DAMAGELW3,
    ASID_DAMAGEAIR1,
    ASID_DAMAGEAIR2,
    ASID_DAMAGEAIR3,
    ASID_DAMAGEFLYHI,
    ASID_DAMAGEFLYN,
    ASID_DAMAGEFLYLW,
    ASID_DAMAGEFLYTOP,
    ASID_DAMAGEFLYROLL,
    ASID_LIGHTGET,
    ASID_HEAVYGET,
    ASID_LIGHTTHROWF,
    ASID_LIGHTTHROWB,
    ASID_LIGHTTHROWHI,
    ASID_LIGHTTHROWLW,
    ASID_LIGHTTHROWDASH,
    ASID_LIGHTTHROWDROP,
    ASID_LIGHTTHROWAIRF,
    ASID_LIGHTTHROWAIRB,
    ASID_LIGHTTHROWAIRHI,
    ASID_LIGHTTHROWAIRLW,
    ASID_HEAVYTHROWF,
    ASID_HEAVYTHROWB,
    ASID_HEAVYTHROWHI,
    ASID_HEAVYTHROWLW,
    ASID_LIGHTTHROWF4,
    ASID_LIGHTTHROWB4,
    ASID_LIGHTTHROWHI4,
    ASID_LIGHTTHROWLW4,
    ASID_LIGHTTHROWAIRF4,
    ASID_LIGHTTHROWAIRB4,
    ASID_LIGHTTHROWAIRHI4,
    ASID_LIGHTTHROWAIRLW4,
    ASID_HEAVYTHROWF4,
    ASID_HEAVYTHROWB4,
    ASID_HEAVYTHROWHI4,
    ASID_HEAVYTHROWLW4,
    ASID_SWORDSWING1,
    ASID_SWORDSWING3,
    ASID_SWORDSWING4,
    ASID_SWORDSWINGDASH,
    ASID_BATSWING1,
    ASID_BATSWING3,
    ASID_BATSWING4,
    ASID_BATSWINGDASH,
    ASID_PARASOLSWING1,
    ASID_PARASOLSWING3,
    ASID_PARASOLSWING4,
    ASID_PARASOLSWINGDASH,
    ASID_HARISENSWING1,
    ASID_HARISENSWING3,
    ASID_HARISENSWING4,
    ASID_HARISENSWINGDASH,
    ASID_STARRODSWING1,
    ASID_STARRODSWING3,
    ASID_STARRODSWING4,
    ASID_STARRODSWINGDASH,
    ASID_LIPSTICKSWING1,
    ASID_LIPSTICKSWING3,
    ASID_LIPSTICKSWING4,
    ASID_LIPSTICKSWINGDASH,
    ASID_ITEMPARASOLOPEN,
    ASID_ITEMPARASOLFALL,
    ASID_ITEMPARASOLFALLSPECIAL,
    ASID_ITEMPARASOLDAMAGEFALL,
    ASID_LGUNSHOOT,
    ASID_LGUNSHOOTAIR,
    ASID_LGUNSHOOTEMPTY,
    ASID_LGUNSHOOTAIREMPTY,
    ASID_FIREFLOWERSHOOT,
    ASID_FIREFLOWERSHOOTAIR,
    ASID_ITEMSCREW,
    ASID_ITEMSCREWAIR,
    ASID_DAMAGESCREW,
    ASID_DAMAGESCREWAIR,
    ASID_ITEMSCOPESTART,
    ASID_ITEMSCOPERAPID,
    ASID_ITEMSCOPEFIRE,
    ASID_ITEMSCOPEEND,
    ASID_ITEMSCOPEAIRSTART,
    ASID_ITEMSCOPEAIRRAPID,
    ASID_ITEMSCOPEAIRFIRE,
    ASID_ITEMSCOPEAIREND,
    ASID_ITEMSCOPESTARTEMPTY,
    ASID_ITEMSCOPERAPIDEMPTY,
    ASID_ITEMSCOPEFIREEMPTY,
    ASID_ITEMSCOPEENDEMPTY,
    ASID_ITEMSCOPEAIRSTARTEMPTY,
    ASID_ITEMSCOPEAIRRAPIDEMPTY,
    ASID_ITEMSCOPEAIRFIREEMPTY,
    ASID_ITEMSCOPEAIRENDEMPTY,
    ASID_LIFTWAIT,
    ASID_LIFTWALK1,
    ASID_LIFTWALK2,
    ASID_LIFTTURN,
    ASID_GUARDON,
    ASID_GUARD,
    ASID_GUARDOFF,
    ASID_GUARDSETOFF,
    ASID_GUARDREFLECT,
    ASID_DOWNBOUNDU,
    ASID_DOWNWAITU,
    ASID_DOWNDAMAGEU,
    ASID_DOWNSTANDU,
    ASID_DOWNATTACKU,
    ASID_DOWNFOWARDU,
    ASID_DOWNBACKU,
    ASID_DOWNSPOTU,
    ASID_DOWNBOUNDD,
    ASID_DOWNWAITD,
    ASID_DOWNDAMAGED,
    ASID_DOWNSTANDD,
    ASID_DOWNATTACKD,
    ASID_DOWNFOWARDD,
    ASID_DOWNBACKD,
    ASID_DOWNSPOTD,
    ASID_PASSIVE,
    ASID_PASSIVESTANDF,
    ASID_PASSIVESTANDB,
    ASID_PASSIVEWALL,
    ASID_PASSIVEWALLJUMP,
    ASID_PASSIVECEIL,
    ASID_SHIELDBREAKFLY,
    ASID_SHIELDBREAKFALL,
    ASID_SHIELDBREAKDOWNU,
    ASID_SHIELDBREAKDOWND,
    ASID_SHIELDBREAKSTANDU,
    ASID_SHIELDBREAKSTANDD,
    ASID_FURAFURA,
    ASID_CATCH,
    ASID_CATCHPULL,
    ASID_CATCHDASH,
    ASID_CATCHDASHPULL,
    ASID_CATCHWAIT,
    ASID_CATCHATTACK,
    ASID_CATCHCUT,
    ASID_THROWF,
    ASID_THROWB,
    ASID_THROWHI,
    ASID_THROWLW,
    ASID_CAPTUREPULLEDHI,
    ASID_CAPTUREWAITHI,
    ASID_CAPTUREDAMAGEHI,
    ASID_CAPTUREPULLEDLW,
    ASID_CAPTUREWAITLW,
    ASID_CAPTUREDAMAGELW,
    ASID_CAPTURECUT,
    ASID_CAPTUREJUMP,
    ASID_CAPTURENECK,
    ASID_CAPTUREFOOT,
    ASID_ESCAPEF,
    ASID_ESCAPEB,
    ASID_ESCAPE,
    ASID_ESCAPEAIR,
    ASID_REBOUNDSTOP,
    ASID_REBOUND,
    ASID_THROWNF,
    ASID_THROWNB,
    ASID_THROWNHI,
    ASID_THROWNLW,
    ASID_THROWNLWWOMEN,
    ASID_PASS,
    ASID_OTTOTTO,
    ASID_OTTOTTOWAIT,
    ASID_FLYREFLECTWALL,
    ASID_FLYREFLECTCEIL,
    ASID_STOPWALL,
    ASID_STOPCEIL,
    ASID_MISSFOOT,
    ASID_CLIFFCATCH,
    ASID_CLIFFWAIT,
    ASID_CLIFFCLIMBSLOW,
    ASID_CLIFFCLIMBQUICK,
    ASID_CLIFFATTACKSLOW,
    ASID_CLIFFATTACKQUICK,
    ASID_CLIFFESCAPESLOW,
    ASID_CLIFFESCAPEQUICK,
    ASID_CLIFFJUMPSLOW1,
    ASID_CLIFFJUMPSLOW2,
    ASID_CLIFFJUMPQUICK1,
    ASID_CLIFFJUMPQUICK2,
    ASID_APPEALR,
    ASID_APPEALL,
    ASID_SHOULDEREDWAIT,
    ASID_SHOULDEREDWALKSLOW,
    ASID_SHOULDEREDWALKMIDDLE,
    ASID_SHOULDEREDWALKFAST,
    ASID_SHOULDEREDTURN,
    ASID_THROWNFF,
    ASID_THROWNFB,
    ASID_THROWNFHI,
    ASID_THROWNFLW,
    ASID_CAPTURECAPTAIN,
    ASID_CAPTUREYOSHI,
    ASID_YOSHIEGG,
    ASID_CAPTUREKOOPA,
    ASID_CAPTUREDAMAGEKOOPA,
    ASID_CAPTUREWAITKOOPA,
    ASID_THROWNKOOPAF,
    ASID_THROWNKOOPAB,
    ASID_CAPTUREKOOPAAIR,
    ASID_CAPTUREDAMAGEKOOPAAIR,
    ASID_CAPTUREWAITKOOPAAIR,
    ASID_THROWNKOOPAAIRF,
    ASID_THROWNKOOPAAIRB,
    ASID_CAPTUREKIRBY,
    ASID_CAPTUREWAITKIRBY,
    ASID_THROWNKIRBYSTAR,
    ASID_THROWNCOPYSTAR,
    ASID_THROWNKIRBY,
    ASID_BARRELWAIT,
    ASID_BURY,
    ASID_BURYWAIT,
    ASID_BURYJUMP,
    ASID_DAMAGESONG,
    ASID_DAMAGESONGWAIT,
    ASID_DAMAGESONGRV,
    ASID_DAMAGEBIND,
    ASID_CAPTUREMEWTWO,
    ASID_CAPTUREMEWTWOAIR,
    ASID_THROWNMEWTWO,
    ASID_THROWNMEWTWOAIR,
    ASID_WARPSTARJUMP,
    ASID_WARPSTARFALL,
    ASID_HAMMERWAIT,
    ASID_HAMMERWALK,
    ASID_HAMMERTURN,
    ASID_HAMMERKNEEBEND,
    ASID_HAMMERFALL,
    ASID_HAMMERJUMP,
    ASID_HAMMERLANDING,
    ASID_KINOKOGIANTSTART,
    ASID_KINOKOGIANTSTARTAIR,
    ASID_KINOKOGIANTEND,
    ASID_KINOKOGIANTENDAIR,
    ASID_KINOKOSMALLSTART,
    ASID_KINOKOSMALLSTARTAIR,
    ASID_KINOKOSMALLEND,
    ASID_KINOKOSMALLENDAIR,
    ASID_ENTRY,
    ASID_ENTRYSTART,
    ASID_ENTRYEND,
    ASID_DAMAGEICE,
    ASID_DAMAGEICEJUMP,
    ASID_CAPTUREMASTERHAND,
    ASID_CAPTUREDAMAGEMASTERHAND,
    ASID_CAPTUREWAITMASTERHAND,
    ASID_THROWNMASTERHAND,
    ASID_CAPTUREKIRBYYOSHI,
    ASID_KIRBYYOSHIEGG,
    ASID_CAPTURELEADEAD,
    ASID_CAPTURELIKELIKE,
    ASID_DOWNREFLECT,
    ASID_CAPTURECRAZYHAND,
    ASID_CAPTUREDAMAGECRAZYHAND,
    ASID_CAPTUREWAITCRAZYHAND,
    ASID_THROWNCRAZYHAND,
    ASID_BARRELCANNONWAIT,
};
enum FtAuxillaryAnim
{
    FTAUXANIM_WIN1,
    FTAUXANIM_WIN2,
    FTAUXANIM_WIN3,
    FTAUXANIM_3,
    FTAUXANIM_4,
    FTAUXANIM_INTROLEFT,
    FTAUXANIM_INTRORIGHT,
    FTAUXANIM_7,
    FTAUXANIM_WAIT,
};
enum FtScriptCmd
{
    FTSCRIPT_END,
    FTSCRIPT_SYNCTIMER,
    FTSCRIPT_ASYNCTIMER,
    FTSCRIPT_3,
    FTSCRIPT_4,
    FTSCRIPT_5,
    FTSCRIPT_6,
    FTSCRIPT_7,
    FTSCRIPT_8,
    FTSCRIPT_9,
    FTSCRIPT_GFX,
    FTSCRIPT_HIT,
    FTSCRIPT_HITDMG,
    FTSCRIPT_HITSIZE,
    FTSCRIPT_HITINTERACTION,
    FTSCRIPT_HITCLEAR,
    FTSCRIPT_HITCLEARALL,
    FTSCRIPT_SFX,
    FTSCRIPT_SFXSMASH,
    FTSCRIPT_FLAG,
    FTSCRIPT_FLAG2,
    FTSCRIPT_FLAG3,
    FTSCRIPT_FLAG4,
    FTSCRIPT_FLAG5,
    FTSCRIPT_FLAG6,
    FTSCRIPT_SET,
    FTSCRIPT_VULNALL,
    FTSCRIPT_VULNUNK,
    FTSCRIPT_VULNPART,
    FTSCRIPT_JABFLAG,
    FTSCRIPT_JABFLAG2,
    FTSCRIPT_VISSET,
    FTSCRIPT_VISCLEAR,
    FTSCRIPT_VISRESET,
    FTSCRIPT_THROW,
    FTSCRIPT_ITEMVIS,
    FTSCRIPT_ITEMVIS2,
    FTSCRIPT_INVISIBLE,
    FTSCRIPT_SFXRANDOM,
    FTSCRIPT_39,
    FTSCRIPT_EYE,
    FTSCRIPT_PARTANIM,
    FTSCRIPT_42,
    FTSCRIPT_RUMBLE,
    FTSCRIPT_44,
    FTSCRIPT_45,
    FTSCRIPT_COLANIMAPPLY,
    FTSCRIPT_COLANIMCLEAR,
    FTSCRIPT_48,
    FTSCRIPT_AFTERIMAGE,
    FTSCRIPT_DYNAMICS,
    FTSCRIPT_DMGSELF,
    FTSCRIPT_IK,
    FTSCRIPT_53,
    FTSCRIPT_STEPSOUND,
    FTSCRIPT_SFXGFX,
    FTSCRIPT_SMASH,
    FTSCRIPT_57,
    FTSCRIPT_WIND,
};

enum Ft_AttackKind
{
    ATKKIND_0,
    ATKKIND_NONE,
    ATKKIND_JAB1,
    ATKKIND_JAB2,
    ATKKIND_JAB3,
    ATKKIND_JAB4,
    ATKKIND_DASH,
    ATKKIND_FTILT,
    ATKKIND_UTILT,
    ATKKIND_DTILT,
    ATKKIND_FSMASH,
    ATKKIND_USMASH,
    ATKKIND_DSMASH,
    ATKKIND_NAIR,
    ATKKIND_FAIR,
    ATKKIND_BAIR,
    ATKKIND_UAIR,
    ATKKIND_DAIR,
    ATKKIND_SPECIALN,
    ATKKIND_SPECIALS,
    ATKKIND_SPECIALHI,
    ATKKIND_SPECIALLW,
    ATKKIND_22,
    ATKKIND_23,
    ATKKIND_24,
    ATKKIND_25,
    ATKKIND_26,
    ATKKIND_27,
    ATKKIND_28,
    ATKKIND_29,
    ATKKIND_30,
    ATKKIND_31,
    ATKKIND_32,
    ATKKIND_33,
    ATKKIND_34,
    ATKKIND_35,
    ATKKIND_36,
    ATKKIND_37,
    ATKKIND_38,
    ATKKIND_39,
    ATKKIND_40,
    ATKKIND_41,
    ATKKIND_42,
    ATKKIND_43,
    ATKKIND_44,
    ATKKIND_45,
    ATKKIND_46,
    ATKKIND_47,
    ATKKIND_48,
    ATKKIND_49,
    ATKKIND_DOWNATTACKU,
    ATKKIND_DOWNATTACKD,
    ATKKIND_PUMMEL,
    ATKKIND_FTHROW,
    ATKKIND_BTHROW,
    ATKKIND_UPTHROW,
    ATKKIND_DTHROW,
    ATKKIND_57,
    ATKKIND_58,
    ATKKIND_59,
    ATKKIND_60,
    ATKKIND_61,
    ATKKIND_62,
    ATKKIND_63,
    ATKKIND_64,
    ATKKIND_65,
    ATKKIND_66,
    ATKKIND_67,
    ATKKIND_68,
    ATKKIND_69,
    ATKKIND_70,
    ATKKIND_71,
    ATKKIND_72,
    ATKKIND_73,
    ATKKIND_74,
    ATKKIND_75,
    ATKKIND_76,
    ATKKIND_77,
    ATKKIND_78,
    ATKKIND_79,
    ATKKIND_80,
    ATKKIND_81,
    ATKKIND_82,
    ATKKIND_83,
    ATKKIND_84,
    ATKKIND_85,
    ATKKIND_86,
    ATKKIND_87,
};
typedef enum FtStateKind
{
    FTSTATEKIND_FREE,        // generally actionable states, like wait, run, jump
    FTSTATEKIND_DEFENSE,     // shield, roll, airdodge
    FTSTATEKIND_ATTACK,      // common attacks like jabs, tilts, and ledge attacks
    FTSTATEKIND_SPECIAL,     // any special state
    FTSTATEKIND_4,           //
    FTSTATEKIND_DAMAGE,      // damage states, including, mewtwo confuse, shield break,
    FTSTATEKIND_DOWNED,      // downed animations, incuding downbound, downback/forward, downattack,
    FTSTATEKIND_PASSIVE,     // tech animations, such as in place, left, right, walltech, and ceiling tech
    FTSTATEKIND_REFLECTWALL, // but also passing through a platform??
    FTSTATEKIND_LEDGE,       //
    FTSTATEKIND_CATCH,       // any grab/throw related state, like catch, catchdash, catchwait, throwf
    FTSTATEKIND_CAPTURE,     // any state related to being grabbed, like capture, thrown, etc
    FTSTATEKIND_12,          // entry animation, respawn animation, damagesong, bury
    FTSTATEKIND_DEAD,        // dead states, including star and screen ko
} FtStateKind;
typedef enum FtHurtKind
{
    FTHURTKIND_VULN,       // vulnerable
    FTHURTKIND_INTANGIBLE, // cannot be hit
    FTHURTKIND_INVINCIBLE, // can be hit, does not take damage
} FtHurtKind;

/*** Structs ***/
struct Playerblock /* native twin, generated */
{
    union {
        char _mex_native_size[3744];
        struct { int state; };
        struct { char _p2358[4]; int c_kind; };
        struct { char _p2359[8]; int p_kind; };
        struct { char _p2360[12]; u8 isTransformed[2]; };
        struct { char _p2361[16]; Vec3 tagPos; };
        struct { char _p2362[28]; Vec3 spawnPos; };
        struct { char _p2363[40]; Vec3 respawnPos; };
        struct { char _p2364[52]; int x34; };
        struct { char _p2365[56]; int x38; };
        struct { char _p2366[60]; int x3C; };
        struct { char _p2367[64]; float initialFacing; };
        struct { char _p2368[68]; u8 costume; };
        struct { char _p2369[69]; u8 color_accent; };
        struct { char _p2370[70]; u8 tint; };
        struct { char _p2371[71]; u8 team; };
        struct { char _p2372[72]; u8 controller; };
        struct { char _p2373[73]; u8 cpuLv; };
        struct { char _p2374[74]; u8 cpuKind; };
        struct { char _p2375[75]; u8 handicap; };
        struct { char _p2376[76]; u8 x4c; };
        struct { char _p2377[77]; u8 kirby_copy; };
        struct { char _p2378[78]; u8 x4e; };
        struct { char _p2379[79]; u8 x4f; };
        struct { char _p2380[80]; float attack; };
        struct { char _p2381[84]; float defense; };
        struct { char _p2382[88]; float scale; };
        struct { char _p2383[92]; u16 damage; };
        struct { char _p2384[94]; u16 initialDamage; };
        struct { char _p2385[96]; u16 stamina; };
        struct { char _p2386[100]; int falls[2]; };
        struct { char _p2387[132]; int match_frame_count; };
        struct { char _p2388[136]; u16 selfDestructs; };
        struct { char _p2389[138]; u8 stocks; };
        struct { char _p2390[140]; int coins_curr; };
        struct { char _p2391[144]; int coins_total; };
        struct { char _p2392[148]; int x98; };
        struct { char _p2393[152]; int x9c; };
        struct { char _p2394[156]; int stickSmashes[2]; };
        struct { char _p2395[164]; int tag; };
        struct { char _p2404[168]; struct {
            union {
                char _mex_span[4];
                struct { char _p2396[3]; u8 : 7; u8 b0 : 1; };
                struct { char _p2397[3]; u8 : 6; u8 is_multispawn : 1; };
                struct { char _p2398[3]; u8 : 5; u8 b2 : 1; };
                struct { char _p2399[3]; u8 : 4; u8 b3 : 1; };
                struct { char _p2400[3]; u8 : 3; u8 b4 : 1; };
                struct { char _p2401[3]; u8 : 2; u8 is_metal : 1; };
                struct { char _p2402[3]; u8 : 1; u8 b6 : 1; };
                struct { char _p2403[3]; u8 b7 : 1; };
            };
        } flags; };
        struct { char _p2405[169]; u8 xad; };
        struct { char _p2406[170]; u8 xae; };
        struct { char _p2407[171]; u8 xaf; };
        struct { char _p2408[184]; void *cb_subft_init; };
        struct { char _p2410[192]; struct {
            union {
                char _mex_span[28];
                struct { char _p2409[8]; s16 queue[10]; };
            };
        } stale_moves; };
        struct { char _p2416[220]; struct {
            union {
                char _mex_span[3364];
                struct { u8 x0[0xcb4]; };
                struct { char _p2411[3252]; int killer_ply; };
                struct { char _p2412[3256]; u8 xcbc[0x60]; };
                struct { char _p2415[3352]; struct {
                    union {
                        char _mex_span[12];
                        struct { int running; };
                        struct { char _p2413[4]; int airborne; };
                        struct { char _p2414[8]; int grounded; };
                    };
                } time; };
            };
        } stats; };
        struct { char _p2417[176]; GOBJ *mu_gobj0; };
        struct { char _p2418[184]; GOBJ *mu_gobj1; };
    };
};
struct PlayerData /* native twin, generated */
{
    union {
        char _mex_native_size[36];
        struct { u8 c_kind; };
        struct { char _p4745[1]; u8 p_kind; };
        struct { char _p4746[2]; u8 stocks; };
        struct { char _p4747[3]; u8 costume; };
        struct { char _p4748[4]; u8 portNumberOverride; };
        struct { char _p4749[5]; u8 spawnPointOverride; };
        struct { char _p4750[6]; u8 facingDirection; };
        struct { char _p4751[7]; u8 subcolor; };
        struct { char _p4752[8]; u8 handicap; };
        struct { char _p4753[9]; u8 team; };
        struct { char _p4754[10]; u8 nametag; };
        struct { char _p4755[11]; u8 xb; };
        struct { char _p4756[12]; unsigned char isRumble : 1; };
        struct { char _p4757[12]; unsigned char : 1; unsigned char isEntry : 1; };
        struct { char _p4758[13]; unsigned char xd_80 : 1; };
        struct { char _p4759[13]; unsigned char : 1; unsigned char is_black_stock : 1; };
        struct { char _p4760[13]; unsigned char : 2; unsigned char xd_20 : 1; };
        struct { char _p4761[13]; unsigned char : 3; unsigned char always_show_indicator : 1; };
        struct { char _p4762[14]; u8 cpuKind; };
        struct { char _p4763[15]; u8 cpuLevel; };
        struct { char _p4764[16]; u16 damage_spawn; };
        struct { char _p4765[18]; u16 damage_respawn; };
        struct { char _p4766[20]; u16 stamina_spawn; };
        struct { char _p4767[24]; float attack; };
        struct { char _p4768[28]; float defense; };
        struct { char _p4769[32]; float scale; };
    };
};

struct FtCreateDesc //
{
    int ft_kind;  // 0x0
    u8 ply;       // 0x4
    u8 x5;        // 0x5
    u8 ms;        // 0x6
    int x8;       // 0x8
    u8 xc_80 : 1; // 0xc
    u8 xc_40 : 1; // 0xc
};

struct FighterBone
{
    JOBJ *joint;          // 0x0
    JOBJ *joint2;         // 0x4, used for interpolation
    int is_dynamic : 1;   // 0x8, 0x80
    int is_active : 1;    // 0x8, 0x40
    int x8_20 : 1;        // 0x8, 0x20, related to it being a runtime bone? is checked at 8006f544
    int is_unk : 1;       // 0x8, 0x10, function 80074ee8 sets this flag on TransN, XRotN, YRotN, ThrowN
    int is_unk_2 : 1;     // 0x8, 0x08, function 80074ee8 sets this flag on TransN, Extra
    int no_anim : 1;      // 0x8, 0x04, will not animate the bone when set
    int x8_03FFFFFF : 26; // 0x8, unk
    int depth : 8;        // 0xC, 0xFF000000, number of parents to get to root
    int dobj_start : 7;   // 0xC, 0x00FE0000, first dobj belonging to this joint. (most every dobj belongs to joint 0, so this is usually equal to the total number of dobjs for subsequent joints)
    int xC_0001 : 1;      // 0xC, 0x00010000
    int xC_0000FFFF : 16; // 0xC, 0x0000FFFF, unk
};

struct DynamicsDesc
{
    int root_bone; // bone index;
    void *params;  // dynamics params;
    int num;       // number of children bones to make dynamic
    float xc;
    float x10;
    float x14;
};

struct DynamicsHitDesc
{
    int bone; // bone index
    int x4;   // unk
    Vec3 x8;  // unk
};

struct DynamicsBehave
{
    int num; // number of dynamic bones to animate in the boneset
};

struct IKParam
{
    u8 legr_index;    // 0x0
    u8 legl_index;    // 0x1
    float leg_param;  // 0x4
    u8 kneer_index;   // 0x8
    u8 kneel_index;   // 0x9
    float knee_param; // 0xC
    u8 footr_index;   // 0x10
    u8 footl_index;   // 0x11
    float foot_param1;
    float foot_param2;
    u8 shoulderr_index;
    u8 shoulderl_index;
    float shoulder_param;
    u8 armr_index;
    u8 arml_index;
    float arm_param1;
    float arm_param2;
};

struct FtVis
{
    s8 prev_value; // value before changed, used to restore original visibility
    s8 index;      // displays the index's dobjs. -1 = hides all dobjs in this table

    /*
    Fighter Visibility Update @ 80074b6c
    - Clears hidden flag on all dobjs in the tables
    - Sets hidden flag on all dobjs in all non-current tables
    */
};

struct FtDynamicBoneset
{
    int apply_anim_num;     // if this is 256, dyanmics are not processed
    DynamicBoneset boneset; // 0x4
};

struct FtSFXArr
{
    int num;
    int *sfx_ids;
};

struct FtSFX
{
    FtSFXArr *smash;     // 0x0
    int death;           // 0x4
    int metal_box;       // 0x8
    int star_ko;         // 0xc
    int jump;            // 0x10
    int jump_air;        // 0x14
    int escape;          // 0x18
    FtSFXArr *light_hit; // 0x1c
    FtSFXArr *heavy_hit; // 0x20
    int tech;            // 0x24
    int cliffcatch;      // 0x28
    int heavy_lift;      // 0x2c
    int item_catch;      // 0x30
    int cheer;           // 0x34
};

struct FtCollDesc
{
    u16 ecb_bone_top;         // 0x0
    u16 ecb_bone_arm_left;    // 0x2
    u16 ecb_bone_arm_right;   // 0x4
    u16 ecb_bone_leg_left;    // 0x6
    u16 ecb_bone_leg_right;   // 0x8
    u16 ecb_bone_center;      // 0xA
    float ecb_size_mult;      // 0xC
    float cliffgrab_width;    // 0x10
    float cliffgrab_y_offset; // 0x14
    float cliffgrab_height;   // 0x18
};

struct __attribute__((scalar_storage_order("big-endian"))) ftData /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int common_attr;
    unsigned int ext_attr;
    unsigned int modelLookup;
    unsigned int ftaction;
    int animDynamics;
    int x14;
    int x18;
    int x1C;
    int x20;
    int x24;
    int x28;
    unsigned int dynamics;
    int hurtbox;
    unsigned int center_bubble;
    int x38;
    int x3C;
    int x40;
    unsigned int coll;
    unsigned int items;
    unsigned int sfx;
    int x50;
    int x54;
    unsigned int ik_param;
};

struct ftChkDevice // 80459a68
{
    int x0;
    int x4;
    int x8;
    int xc;
    int x10;
    int x14;
    int x18;
    int x1c;
    int x20;
    GOBJ *gobj;
    int hazard_kind;
    void *check;
};

struct FtState
{
    int action_id;
    int flags;
    char move_id;
    char bitflags1;
    void *animation_callback;
    void *iasa_callback;
    void *physics_callback;
    void *collision_callback;
    void *camera_callback;
};

struct __attribute__((scalar_storage_order("big-endian"))) FtAction /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int anim_symbol;
    int anim_offset;
    int anim_size;
    unsigned int script;
    int flags;
    unsigned int anim_data;
};

struct __attribute__((scalar_storage_order("big-endian"))) Figatree /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    int kind;
    int x4;
    float frame_num;
    unsigned int nodes;
    unsigned int tracks;
};

struct ReflectDesc
{
    int bone;
    int max_damage;
    Vec3 pos;
    float radius;
    float damage_mult;
    float velocity_mult;
    int flags;
};

struct ShieldDesc
{
    int bone;
    int max_damage;
    Vec3 pos;
    float radius;
    float damage_mult;
    float velocity_mult;
    int flags;
};

struct AbsorbDesc
{
    int bone;     // x00
    Vec3 pos;     // x04
    float radius; // x10
};

struct AfterImageDesc
{
    float x0;            // 0x0
    float x4;            // 0x4
    u8 start_alpha;      // 0x8
    u8 end_alpha;        // 0x8
    u8 in_col[4];        // 0xA
    u8 out_col[4];       // 0xE
    u8 x12;              // 0x12
    u8 x13;              // 0x13
    int bone_index;      // 0x14
    float offset_bottom; // 0x18
    float offset_top;    // 0x1C
};
struct FtDmgLog
{
    float direction;     // 0x1844, 0x0
    int kb_angle;        // 0x1848, 0x4
    int hurt_kind;       // 0x184c, 0x8. 0 = low, 1 = mid, 2 = hi (previously was called damaged_hurtbox)
    float force_applied; // 0x1850, 0xc, to get kb magnitude, multiply by 0.03
    Vec3 collpos;        // 0x1854, 0x10
    int attribute;       // 0x1860, 0x1c
    int x1864;           // 0x1864, 0x20
    GOBJ *source;        // 0x1868, 0x24
    float percent;       // 0x186c, 0x28
};

struct HitVictim
{
    void *data; // userdata of the object that was hit
    int timer;  // items use this to wait until hitting this gobj again
};

struct ftHit /* native twin, generated */
{
    union {
        char _mex_native_size[512];
        struct { int active; };
        struct { char _p2235[4]; int x4; };
        struct { char _p2236[8]; int dmg; };
        struct { char _p2237[12]; float dmg_f; };
        struct { char _p2238[16]; Vec3 offset; };
        struct { char _p2239[28]; float size; };
        struct { char _p2240[32]; int angle; };
        struct { char _p2241[36]; int kb_growth; };
        struct { char _p2242[40]; int wdsk; };
        struct { char _p2243[44]; int kb; };
        struct { char _p2244[48]; int attribute; };
        struct { char _p2245[52]; int shield_dmg; };
        struct { char _p2246[56]; int hitsound_severity; };
        struct { char _p2247[60]; int hitsound_kind; };
        struct { char _p2248[66]; unsigned char x421 : 1; };
        struct { char _p2249[66]; unsigned char : 1; unsigned char x422 : 1; };
        struct { char _p2250[66]; unsigned char : 2; unsigned char x423 : 1; };
        struct { char _p2251[66]; unsigned char : 3; unsigned char x424 : 1; };
        struct { char _p2252[66]; unsigned char : 4; unsigned char no_hurt : 1; };
        struct { char _p2253[66]; unsigned char : 5; unsigned char no_reflect : 1; };
        struct { char _p2254[66]; unsigned char : 6; unsigned char x427 : 1; };
        struct { char _p2255[66]; unsigned char : 7; unsigned char x428 : 1; };
        struct { char _p2256[67]; unsigned char x431 : 1; };
        struct { char _p2257[67]; unsigned char : 1; unsigned char x432 : 1; };
        struct { char _p2258[67]; unsigned char : 2; unsigned char hit_all : 1; };
        struct { char _p2259[67]; unsigned char : 3; unsigned char x434 : 1; };
        struct { char _p2260[67]; unsigned char : 4; unsigned char x435 : 1; };
        struct { char _p2261[67]; unsigned char : 5; unsigned char x436 : 1; };
        struct { char _p2262[67]; unsigned char : 6; unsigned char x437 : 1; };
        struct { char _p2263[67]; unsigned char : 7; unsigned char x438 : 1; };
        struct { char _p2264[68]; u8 x44; };
        struct { char _p2265[69]; u8 victim_num; };
        struct { char _p2266[72]; JOBJ *bone; };
        struct { char _p2267[80]; Vec3 pos; };
        struct { char _p2268[92]; Vec3 pos_prev; };
        struct { char _p2269[104]; Vec3 pos_coll; };
        struct { char _p2270[116]; float coll_distance; };
        struct { char _p2272[120]; struct {
            union {
                char _mex_span[16];
                struct { void *data; };
                struct { char _p2271[8]; int timer; };
            };
        } victims[24]; };
        struct { char _p2273[504]; int x134; };
    };
};

struct FtHurt /* native twin, generated */
{
    union {
        char _mex_native_size[72];
        struct { FtHurtKind state; };
        struct { char _p2328[4]; Vec3 hurt1_offset; };
        struct { char _p2329[16]; Vec3 hurt2_offset; };
        struct { char _p2330[28]; float scale; };
        struct { char _p2331[32]; JOBJ *jobj; };
        struct { char _p2332[40]; unsigned char is_updated : 1; };
        struct { char _p2333[40]; unsigned char : 1; unsigned char x24_2 : 1; };
        struct { char _p2334[40]; unsigned char : 2; unsigned char x24_3 : 1; };
        struct { char _p2335[40]; unsigned char : 3; unsigned char x24_4 : 1; };
        struct { char _p2336[40]; unsigned char : 4; unsigned char x24_5 : 1; };
        struct { char _p2337[40]; unsigned char : 5; unsigned char x24_6 : 1; };
        struct { char _p2338[40]; unsigned char : 6; unsigned char x24_7 : 1; };
        struct { char _p2339[40]; unsigned char : 7; unsigned char x24_8 : 1; };
        struct { char _p2340[44]; Vec3 hurt1_pos; };
        struct { char _p2341[56]; Vec3 hurt2_pos; };
        struct { char _p2342[68]; int bone_index; };
    };
};

struct FtCoin
{
    float size;    // 0x0
    JOBJ *jobj;    // 0x4
    Vec3 pos_cur;  // 0x8
    Vec3 pos_prev; // 0x14
    Vec3 coll_pos; // 0x20
};

struct FtAfterImageKey
{
    Vec3 pos;
    Vec3 rot;
};

struct CPULeaderLog
{
    int x0;                 // 0x0, 0xfc
    u8 x4;                  // 0x4, 0x100
    u8 x5;                  // 0x3, 0x101
    u8 x6;                  // 0x6, 0x102
    u8 x7;                  // 0x7, 0x103
    int x8;                 // 0x8, 0x104
    Vec3 pos;               // 0xc, 0x108
    float facing_direction; // 0x18, 0x114
};

struct CPU
{
    int held;                    // 0x0
    s8 lstickX;                  // 0x4
    s8 lstickY;                  // 0x5
    s8 cstickX;                  // 0x6
    s8 cstickY;                  // 0x7
    u8 ltrigger;                 // 0x8
    u8 rtrigger;                 // 0x9
    int ai;                      // 0xc, 25 of these, function table at 800a1090
    int level;                   // 0x10
    int x14;                     // 0x14
    int scenario_id;             // 0x18
    int x1c;                     // 0x1c
    int x20;                     // 0x20
    int x24;                     // 0x24
    int x28;                     // 0x28
    int x2c;                     // 0x2c
    int x30;                     // 0x30
    int x34;                     // 0x34
    float x38;                   // 0x38
    float x3c;                   // 0x3c
    float x40;                   // 0x40
    void *x44;                   // 0x44
    void *x48;                   // 0x48
    int x4c;                     // 0x4c
    int x50;                     // 0x50
    float x54;                   // 0x54
    float x58;                   // 0x58
    float x5c;                   // 0x5c
    int x60;                     // 0x60
    int x64;                     // 0x64
    int x68;                     // 0x68
    int x6c;                     // 0x6c
    int x70;                     // 0x70
    int x74;                     // 0x74
    int proc_num;                // 0x78, number of times it updated CPU logic in any capacity
    int scenario_check_num;      // 0x7c, number of times it tried to update CPU scenario
    int x80;                     // 0x80
    int x84;                     // 0x84
    int x88;                     // 0x88
    int x8c;                     // 0x8c
    int x90;                     // 0x90
    int x94;                     // 0x94
    int x98;                     // 0x98
    int x9c;                     // 0x9c
    int xa0;                     // 0xa0
    int xa4;                     // 0xa4
    int xa8;                     // 0xa8
    int xac;                     // 0xac
    int xb0;                     // 0xb0
    int xb4;                     // 0xb4
    int xb8;                     // 0xb8
    int xbc;                     // 0xbc
    int xc0;                     // 0xc0
    int xc4;                     // 0xc4
    u8 xc8;                      // 0xc8
    int xcc;                     // 0xcc
    int xd0;                     // 0xd0CPULeaderLog
    int xd4;                     // 0xd4
    int xd8;                     // 0xd8
    int xdc;                     // 0xdc
    int xe0;                     // 0xe0
    int xe4;                     // 0xe4
    int xe8;                     // 0xe8
    u8 xec;                      // 0xec
    int xf0;                     // 0xf0
    int xf4;                     // 0xf4
    int xf8;                     // 0xf8, flags | 0x00000100 is the "isCopy" flag (uses leaders inputs)
    CPULeaderLog leader_log[30]; // 0xfc, contains a log of per frame data about the followers leader
    void *unk_curr;              // 0x444
    void *scenario_curr;         // 0x448, cpu scenario not updated if this contains a pointer @ 800b27b8
    void *scenario_curr2;        // 0x44c, cpu scenario not updated if this contains a pointer @ 800b27c4
    void *x450;                  // 0x450
    u8 cmdscript_queue[256];     // 0x454, list of command ids for the follower to execute
    void *cmdscript_curr;        // 0x554, points to a command in the cmdscript queue
};

struct FtDmgVibrateDesc
{
    Vec2 *offsets;
    int num;
};

struct ftCommonBone
{
    u8 *x0;
    u8 *x4;
    int bone_num;
};

struct __attribute__((scalar_storage_order("big-endian"))) ftCommonData /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    float x0;
    float x4;
    float x8;
    float xc;
    float x10;
    float x14;
    float x18;
    int meteor_lockout;
    float lstick_tilt;
    float x24;
    float walk_anim_mid_percentage;
    float walk_anim_fast_percentage;
    float x30;
    float x34;
    float x38;
    float dash_min_stick;
    int dash_min_stick_tilt;
    float x44;
    float x48;
    float x4c;
    float x50;
    float x54;
    float x58;
    float x5c;
    float x60;
    float x64;
    float x68;
    float friction_mult;
    float jumpaerial_lsticky;
    int jumpaerial_lsticktimer;
    float jumpaerial_back_tilt;
    float x7c;
    float x80;
    float x84;
    float x88;
    float x8c;
    float lstick_rebirthfall;
    float x94;
    float x98;
    float x9c;
    float xa0;
    float xa4;
    float xa8;
    float xac;
    float xb0;
    float xb4;
    float xb8;
    float xbc;
    float xc0;
    float xc4;
    float xc8;
    float xcc;
    float xd0;
    float xd4;
    float xd8;
    float xdc;
    float xe0;
    float xe4;
    float xe8;
    float xec;
    float xf0;
    float xf4;
    float xf8;
    float xfc;
    float force_applied_to_kb_mag_multiplier;
    float armor_min;
    float x108;
    float x10c;
    float x110;
    float x114;
    float x118;
    float x11c;
    float x120;
    float kb_reduction_crouch;
    float x128;
    float x12c;
    float x130;
    float x134;
    float x138;
    float x13c;
    float x140;
    float kb_sakurai_angle_aerial;
    float kb_sakurai_angle_grounded;
    float kb_grounded_sakurai_angle_threshold;
    float x150;
    float x154;
    float x158;
    float x15c;
    float x160;
    float kb_maxVelX;
    float hitlag_mult;
    float hitlag_base;
    float x170;
    float x174;
    float x178;
    float x17c;
    float x180;
    float x184;
    float x188;
    float x18c;
    float x190;
    float hitlag_max;
    float x198;
    float x19c;
    float x1a0;
    float hitlag_elec;
    float tdi_maxAngle;
    float x1ac;
    float x1b0;
    float x1b4;
    float x1b8;
    float kb_bounceDecay;
    float x1c0;
    float x1c4;
    float x1c8;
    float x1cc;
    float x1d0;
    float x1d4;
    float x1d8;
    float x1dc;
    float x1e0;
    float x1e4;
    float x1e8;
    float x1ec;
    float x1f0;
    float x1f4;
    float x1f8;
    float x1fc;
    float x200;
    float kb_frameDecay;
    float x208;
    float x20c;
    float x210;
    float x214;
    float x218;
    float x21c;
    float x220;
    float x224;
    float x228;
    float x22c;
    float x230;
    float x234;
    float x238;
    float x23c;
    float x240;
    float x244;
    float x248;
    float x24c;
    float x250;
    float x254;
    float x258;
    float x25c;
    float x260;
    float x264;
    float x268;
    float x26c;
    float x270;
    float x274;
    float x278;
    float x27c;
    float x280;
    float x284;
    float x288;
    float x28c;
    float x290;
    float x294;
    float x298;
    float x29c;
    float x2a0;
    float x2a4;
    float x2a8;
    float x2ac;
    float x2b0;
    float x2b4;
    float x2b8;
    float x2bc;
    float x2c0;
    float x2c4;
    float x2c8;
    float x2cc;
    float x2d0;
    float x2d4;
    float x2d8;
    float x2dc;
    float x2e0;
    float x2e4;
    float x2e8;
    float x2ec;
    float x2f0;
    float x2f4;
    float x2f8;
    float x2fc;
    float x300;
    float x304;
    float x308;
    float x30c;
    float x310;
    float x314;
    float x318;
    float x31c;
    float x320;
    float x324;
    float x328;
    float x32c;
    float x330;
    float x334;
    float escapeair_vel;
    float escapeair_veldecaymult;
    float x340;
    float x344;
    float x348;
    float x34c;
    float x350;
    float grab_mash_min;
    float grab_mash_per_handicap;
    float grab_max_handicap;
    float grab_placing_mult;
    float grab_placing_max;
    float grab_mash_mult;
    float x36c;
    float x370;
    float x374;
    float x378;
    float x37c;
    float x380;
    float x384;
    float x388;
    float x38c;
    float x390;
    float x394;
    float x398;
    float x39c;
    float x3a0;
    float grab_mash_per_frame;
    float grab_mash_per_input;
    float x3ac;
    float grab_wiggle_per_input;
    float grab_wiggle_rate;
    float x3b8;
    float x3bc;
    float x3c0;
    float x3c4;
    float x3c8;
    float x3cc;
    float x3d0;
    float x3d4;
    float x3d8;
    float x3dc;
    float x3e0;
    float x3e4;
    float x3e8;
    float x3ec;
    float x3f0;
    float x3f4;
    float x3f8;
    float x3fc;
    float x400;
    float x404;
    float x408;
    float x40c;
    float x410;
    float x414;
    float x418;
    float x41c;
    float x420;
    float x424;
    float x428;
    float x42c;
    float x430;
    float x434;
    float x438;
    float x43c;
    float x440;
    float x444;
    float x448;
    float x44c;
    float x450;
    float zjostle_frame;
    float zjostle_max;
    float ms_zjostle_frame;
    float ms_zjostle_max;
    float x464;
    float x468;
    float x46c;
    float x470;
    float x474;
    float x478;
    float x47c;
    float x480;
    float x484;
    float x488;
    float x48c;
    float x490;
    float ledge_drop_thresh;
    float x498;
    int cliff_invuln_time;
    float x4a0;
    float x4a4;
    float x4a8;
    float x4ac;
    float asdi_mag;
    float x4b4;
    float x4b8;
    float asdi_units;
    float x4c0;
    float x4c4;
    float x4c8;
    float x4cc;
    float x4d0;
    float x4d4;
    float x4d8;
    float x4dc;
    float x4e0;
    float x4e4;
    float x4e8;
    float x4ec;
    float x4f0;
    float x4f4;
    float x4f8;
    float x4fc;
    int dead_timer;
    float x504;
    float x508;
    float x50c;
    float x510;
    float x514;
    float x518;
    float x51c;
    float x520;
    float x524;
    float x528;
    float x52c;
    float x530;
    float x534;
    float x538;
    float x53c;
    float x540;
    float x544;
    float x548;
    float x54c;
    float x550;
    float x554;
    float x558;
    float x55c;
    float x560;
    float x564;
    float x568;
    float x56c;
    float x570;
    float x574;
    float x578;
    float x57c;
    float x580;
    float x584;
    float x588;
    float x58c;
    float x590;
    float x594;
    float x598;
    float x59c;
    float x5a0;
    float x5a4;
    float x5a8;
    float x5ac;
    float x5b0;
    float x5b4;
    float x5b8;
    float x5bc;
    float x5c0;
    float x5c4;
    float x5c8;
    float x5cc;
    float x5d0;
    float x5d4;
    float x5d8;
    float x5dc;
    float x5e0;
    float x5e4;
    float x5e8;
    float x5ec;
    float x5f0;
    float x5f4;
    float x5f8;
    float x5fc;
    float x600;
    float x604;
    float x608;
    float x60c;
    float x610;
    float x614;
    float x618;
    float x61c;
    float x620;
    float x624;
    float x628;
    float x62c;
    float x630;
    float x634;
    float x638;
    float x63c;
    float x640;
    float x644;
    float x648;
    float x64c;
    float x650;
    float x654;
    float x658;
    float x65c;
    float x660;
    float x664;
    float x668;
    float x66c;
    float x670;
    float x674;
    float x678;
    float x67c;
    float x680;
    float x684;
    float x688;
    float x68c;
    float x690;
    float x694;
    float x698;
    float x69c;
    float x6a0;
    float x6a4;
    float x6a8;
    float x6ac;
    float x6b0;
    float x6b4;
    float x6b8;
    float x6bc;
    float x6c0;
    float x6c4;
    float x6c8;
    float x6cc;
    float x6d0;
    float x6d4;
    float x6d8;
    float x6dc;
    float x6e0;
    float x6e4;
    float x6e8;
    float x6ec;
    float metal_armor;
    float x6f4;
    float x6f8;
    float x6fc;
    float x700;
    float x704;
    float x708;
    float x70c;
    float x710;
    float x714;
    float kb_reduction_ice;
    float x71c;
    float x720;
    float x724;
    float x728;
    float x72c;
    float x730;
    float x734;
    float x738;
    float x73c;
    float x740;
    float x744;
    float x748;
    float x74c;
    float x750;
    float x754;
    float x758;
    float x75c;
    float x760;
    float x764;
    float x768;
    float x76c;
    float x770;
    float x774;
    float x778;
    float x77c;
    float x780;
    float x784;
    float x788;
    float x78c;
    float x790;
    float x794;
    float x798;
    float x79c;
    float x7a0;
    float x7a4;
    float tip_overlap_max;
    float x7ac;
    float x7b0;
    float x7b4;
    float x7b8;
    float x7bc;
    float x7c0;
    float kb_reduction_smashcharge;
    float x7c8;
    float x7cc;
    float x7d0;
    float x7d4;
    float x7d8;
    float x7dc;
    float x7e0;
    float x7e4;
    int meteor_angle_min;
    int meteor_angle_max;
    int meteor_delay;
    float x7f4;
    float x7f8;
    float x7fc;
    float x800;
    float x804;
    float x808;
    float x80c;
    float x810;
};

struct FighterData /* native twin, generated */
{
    union {
        char _mex_native_size[32];
        struct { GOBJ *fighter; };
        struct { char _p141[8]; int kind; };
        struct { char _p142[12]; int spawn_num; };
        struct { char _p143[16]; char ply; };
        struct { char _p144[20]; int state_id; };
        struct { char _p145[24]; int action_id; };
        struct { char _p146[28]; int common_state_num; };
        struct { char _p147[32]; FtState *ftstates_common; };
        struct { char _p148[40]; FtState *ftstates_special; };
        struct { char _p149[48]; FtAction *ftaction; };
        struct { char _p150[56]; u16 *dynamics_data; };
        struct { char _p151[64]; float facing_direction; };
        struct { char _p152[68]; float facing_direction_prev; };
        struct { char _p153[72]; Vec3 scale; };
        struct { char _p154[84]; int x40; };
        struct { char _p155[88]; Mtx temp_mtx; };
        struct { char _p170[136]; struct phys {
            union {
                char _mex_span[144];
                struct { Vec3 anim_vel; };
                struct { char _p156[12]; Vec3 self_vel; };
                struct { char _p157[24]; Vec3 kb_vel; };
                struct { char _p158[36]; Vec3 atk_shield_kb_vel; };
                struct { char _p159[48]; Vec3 xA4; };
                struct { char _p160[60]; Vec3 pos; };
                struct { char _p161[72]; Vec3 pos_prev; };
                struct { char _p162[84]; Vec3 pos_delta; };
                struct { char _p163[96]; Vec3 xD4; };
                struct { char _p164[108]; int air_state; };
                struct { char _p165[112]; float horzitonal_velocity_queue_will_be_added_to_0xec; };
                struct { char _p166[116]; float vertical_velocity_queue_will_be_added_to_0xec; };
                struct { char _p167[120]; Vec3 self_vel_ground; };
                struct { char _p168[132]; Vec2 nudge_vel; };
                struct { char _p169[140]; int x100; };
            };
        } phys; };
        struct { char _p171[288]; JOBJDesc *costume_jobjdesc; };
        struct { char _p172[296]; ftData *ftData; };
        struct { char _p263[304]; struct __attribute__((scalar_storage_order("big-endian"))) attr {
            union __attribute__((scalar_storage_order("big-endian"))) {
                char _mex_span[388];
                struct __attribute__((scalar_storage_order("big-endian"))) { float walk_initial_velocity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p173[4]; float walk_acceleration; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p174[8]; float walk_maximum_velocity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p175[12]; float slow_walk_max; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p176[16]; float mid_walk_point; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p177[20]; float fast_walk_min; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p178[24]; float ground_friction; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p179[28]; float dash_initial_velocity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p180[32]; float dashrun_acceleration_a; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p181[36]; float dashrun_acceleration_b; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p182[40]; float dashrun_terminal_velocity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p183[44]; float run_animation_scaling; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p184[48]; float max_runbrake_frames; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p185[52]; float grounded_max_horizontal_velocity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p186[56]; float jump_startup_time; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p187[60]; float jump_h_initial_velocity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p188[64]; float jump_v_initial_velocity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p189[68]; float ground_to_air_jump_momentum_multiplier; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p190[72]; float jump_h_max_velocity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p191[76]; float hop_v_initial_velocity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p192[80]; float air_jump_v_multiplier; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p193[84]; float air_jump_h_multiplier; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p194[88]; int max_jumps; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p195[92]; float gravity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p196[96]; float terminal_velocity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p197[100]; float aerial_drift_stick_mult; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p198[104]; float aerial_drift_base; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p199[108]; float aerial_drift_max; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p200[112]; float aerial_friction; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p201[116]; float fastfall_velocity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p202[120]; float horizontal_air_mobility_constant; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p203[124]; int jab_2_input_window; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p204[128]; int jab_3_input_window; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p205[132]; int frames_to_change_direction_on_standing_turn; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p206[136]; float weight; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p207[140]; float model_scaling; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p208[144]; float initial_shield_size; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p209[148]; float shield_break_initial_velocity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p210[152]; int rapid_jab_window; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p211[156]; float rebound_frames; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p212[160]; int x1B0; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p213[164]; int x1B4; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p214[168]; float ledge_jump_horizontal_velocity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p215[172]; float ledge_jump_vertical_velocity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p216[176]; float item_throw_velocity_multiplier; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p217[180]; float item_discard_vel_mult; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p218[184]; int x1C8; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p219[188]; int x1CC; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p220[192]; int x1D0; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p221[196]; int x1D4; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p222[200]; int x1D8; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p223[204]; int x1DC; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p224[208]; int x1E0; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p225[212]; int x1E4; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p226[216]; int x1E8; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p227[220]; float kirby_star_scaling; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p228[224]; float kirby_b_star_damage; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p229[228]; float normal_landing_lag; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p230[232]; float n_air_landing_lag; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p231[236]; float f_air_landing_lag; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p232[240]; float b_air_landing_lag; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p233[244]; float u_air_landing_lag; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p234[248]; float d_air_landing_lag; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p235[252]; float nametag_height; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p236[256]; float wall_tech_x_offset; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p237[260]; float wall_jump_horizontal_velocity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p238[264]; float wall_jump_vertical_velocity; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p239[268]; int x21C; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p240[272]; float trophy_scale; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p241[276]; Vec3 bunny_hood_left_offset; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p242[288]; Vec3 bunny_hood_right_offset; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p243[300]; float bunny_hood_scale; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p244[304]; Vec3 head_flower_offset; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p245[316]; float head_flower_scale; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p246[320]; int x250; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p247[324]; int x254; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p248[328]; int unk_walljump; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p249[332]; float bubble_ratio; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p250[336]; int x260; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p251[340]; int x264; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p252[344]; int x268; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p253[348]; int x26C; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p254[352]; float respawn_platform_scale; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p255[356]; int x274; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p256[360]; int x278; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p257[364]; int camera_zoom_target_bone; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p258[368]; int x280; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p259[372]; int x284; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p260[376]; int x288; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p261[380]; int special_jump_action___1; };
                struct __attribute__((scalar_storage_order("big-endian"))) { char _p262[384]; int weight_dependent_throw_speed_flags; };
            };
        } attr; };
        struct { char _p269[692]; struct {
            union {
                char _mex_span[48];
                struct { Vec2 ground_light_offset; };
                struct { char _p264[8]; Vec2 ground_light_size; };
                struct { char _p265[16]; Vec2 ground_heavy_offset; };
                struct { char _p266[24]; Vec2 ground_heavy_size; };
                struct { char _p267[32]; Vec2 air_light_offset; };
                struct { char _p268[40]; Vec2 air_light_size; };
            };
        } itpickup; };
        struct { char _p270[740]; float jostle_offset; };
        struct { char _p271[744]; float jostle_range; };
        struct { char _p272[760]; FtMultiJumpDesc *multi_jump_desc; };
        struct { char _p273[768]; void *special_attributes; };
        struct { char _p274[776]; void *special_attributes2; };
        struct { char _p279[784]; struct {
            union {
                char _mex_span[20];
                struct { float walkslow; };
                struct { char _p275[4]; float walkmiddle; };
                struct { char _p276[8]; float walkfast; };
                struct { char _p277[12]; float guardon; };
                struct { char _p278[16]; float landing; };
            };
        } anim_length_lookup; };
        struct { char _p285[808]; struct {
            union {
                char _mex_span[32];
                struct { int apply_anim_num; };
                struct { char _p284[8]; struct {
                    union {
                        char _mex_span[24];
                        struct { DynamicBoneData *data; };
                        struct { char _p280[8]; int bone_num; };
                        struct { char _p281[12]; float x8; };
                        struct { char _p282[16]; float xc; };
                        struct { char _p283[20]; float x10; };
                    };
                } boneset; };
            };
        } dynamics_boneset[10]; };
        struct { char _p286[1128]; int dynamics_num; };
        struct { char _p291[1136]; struct script {
            union {
                char _mex_span[32];
                struct { float script_event_timer; };
                struct { char _p287[4]; float script_frame_timer; };
                struct { char _p288[8]; int *script_current; };
                struct { char _p289[16]; int script_loop_num; };
                struct { char _p290[24]; int *script_return; };
            };
        } script; };
        struct { char _p292[1184]; int pointer_to_0x460; };
        struct { char _p293[1188]; int pointer_to_0x3c0; };
        struct { char _p294[1192]; ColorOverlay color[3]; };
        struct { char _p295[1672]; LOBJ *LObj; };
        struct { char _p296[1680]; int anim_num; };
        struct { char _p297[1688]; Figatree *figatree_curr; };
        struct { char _p309[1696]; struct {
            union {
                char _mex_span[4];
                struct { u32 transn_phys_update : 1; };
                struct { u32 : 1; u32 loop_anim : 1; };
                struct { u32 : 2; u32 x20000000 : 1; };
                struct { u32 : 3; u32 x10000000 : 1; };
                struct { u32 : 4; u32 no_dynamics : 1; };
                struct { u32 : 5; u32 x04000000 : 1; };
                struct { u32 : 6; u32 transn_use_model_scale : 1; };
                struct { u32 x01c00000 : 3; };
                struct { u32 : 10; u32 x003ffe00 : 13; };
                struct { u32 : 5; u32 disable_blend_bone_index : 4; };
                struct { u32 : 26; u32 kind : 5; };
            };
        } action_flags; };
        struct { char _p310[1704]; void *anim_requested; };
        struct { char _p311[1712]; void *anim_cache_curr; };
        struct { char _p312[1720]; void *anim_cache_persist; };
        struct { char _p313[1728]; void *anim_curr_ARAM; };
        struct { char _p314[1736]; void *anim_persist_ARAM; };
        struct { char _p315[1744]; int dobj_toggle_num; };
        struct { char _p316[1748]; int x5B0; };
        struct { char _p317[1760]; int x5B8; };
        struct { char _p318[1800]; int texanim_num; };
        struct { char _p319[1816]; int x5D4; };
        struct { char _p320[1856]; FighterBone *bones; };
        struct { char _p321[1864]; int bone_num; };
        struct { char _p322[1872]; DOBJ **dobj_lookup; };
        struct { char _p323[1880]; FtVis dobj_toggle[12]; };
        struct { char _p324[1904]; Effect *gfx; };
        struct { char _p325[1912]; int x610; };
        struct { char _p326[1916]; int x614; };
        struct { char _p327[1920]; char pad_index; };
        struct { char _p328[1921]; char costume_id; };
        struct { char _p329[1922]; char color_overlay_id; };
        struct { char _p330[1923]; u8 team; };
        struct { char _p331[1924]; char x61C; };
        struct { char _p332[1925]; char x61D; };
        struct { char _p333[1926]; u8 filler_x61E[0x620 - 0x61E]; };
        struct { char _p377[1928]; struct input {
            union {
                char _mex_span[108];
                struct { Vec2 lstick; };
                struct { char _p334[8]; Vec2 lstick_prev; };
                struct { char _p335[16]; int x630; };
                struct { char _p336[20]; int x634; };
                struct { char _p337[24]; Vec2 cstick; };
                struct { char _p338[32]; Vec2 cstick_prev; };
                struct { char _p339[40]; int x648; };
                struct { char _p340[44]; int x64C; };
                struct { char _p341[48]; float trigger; };
                struct { char _p342[52]; float trigger_prev; };
                struct { char _p343[56]; int x658; };
                struct { char _p344[60]; int held; };
                struct { char _p345[64]; int held_prev; };
                struct { char _p346[68]; int x664; };
                struct { char _p347[72]; int down; };
                struct { char _p348[76]; int x66C; };
                struct { char _p349[80]; char timer_lstick_tilt_x; };
                struct { char _p350[81]; char timer_lstick_tilt_y; };
                struct { char _p351[82]; char timer_trigger_analog; };
                struct { char _p352[83]; char timer_lstick_smash_x; };
                struct { char _p353[84]; char timer_lstick_smash_y; };
                struct { char _p354[85]; char timer_trigger_digital; };
                struct { char _p355[86]; char timer_lstick_any_x; };
                struct { char _p356[87]; char timer_lstick_any_y; };
                struct { char _p357[88]; char timer_trigger_any; };
                struct { char _p358[89]; char x679; };
                struct { char _p359[90]; char x67A; };
                struct { char _p360[91]; char x67B; };
                struct { char _p361[92]; char timer_a; };
                struct { char _p362[93]; char timer_b; };
                struct { char _p363[94]; char timer_xy; };
                struct { char _p364[95]; char timer_trigger_any_ignore_hitlag; };
                struct { char _p365[96]; char timer_LR; };
                struct { char _p366[97]; char timer_padup; };
                struct { char _p367[98]; char timer_paddown; };
                struct { char _p368[99]; char timer_item_release; };
                struct { char _p369[100]; char since_rapid_lr; };
                struct { char _p370[101]; char timer_jump; };
                struct { char _p371[102]; char timer_specialhi; };
                struct { char _p372[103]; char timer_speciallw; };
                struct { char _p373[104]; char timer_specials; };
                struct { char _p374[105]; char timer_specialn; };
                struct { char _p375[106]; char timer_jump_lockout; };
                struct { char _p376[107]; char timer_specialhi_lockout; };
            };
        } input; };
        struct { char _p378[2036]; Vec3 transN_pos; };
        struct { char _p379[2048]; Vec3 transN_pos_prev; };
        struct { char _p380[2060]; Vec3 transN_offset; };
        struct { char _p381[2072]; Vec3 transN_offset_prev; };
        struct { char _p382[2084]; float input_stickangle; };
        struct { char _p383[2088]; int x6C0; };
        struct { char _p384[2092]; int x6C4; };
        struct { char _p385[2096]; int x6C8; };
        struct { char _p386[2100]; int x6CC; };
        struct { char _p387[2104]; int x6D0; };
        struct { char _p388[2108]; int x6D4; };
        struct { char _p389[2112]; int x6D8; };
        struct { char _p390[2116]; int x6DC; };
        struct { char _p391[2120]; int x6E0; };
        struct { char _p392[2124]; int x6E4; };
        struct { char _p393[2128]; int x6E8; };
        struct { char _p394[2132]; int x6EC; };
        struct { char _p395[2136]; CollData coll_data; };
        struct { char _p396[2600]; CmSubject *camera_subject; };
        struct { char _p402[2608]; struct state {
            union {
                char _mex_span[24];
                struct { float frame; };
                struct { char _p397[4]; int x898; };
                struct { char _p398[8]; float rate; };
                struct { char _p399[12]; int x8a0; };
                struct { char _p400[16]; float blend; };
                struct { char _p401[20]; float current_blend; };
            };
        } state; };
        struct { char _p403[2632]; JOBJ *anim_skeleton; };
        struct { char _p404[2640]; int x8b0; };
        struct { char _p405[2644]; int x8b4; };
        struct { char _p406[2648]; int x8b8; };
        struct { char _p407[2652]; int x8bc; };
        struct { char _p408[2656]; int curr_hold_anim; };
        struct { char _p409[2660]; int x8c4; };
        struct { char _p410[2664]; int x8c8; };
        struct { char _p411[2668]; int x8cc; };
        struct { char _p412[2672]; int x8d0; };
        struct { char _p413[2676]; int x8d4; };
        struct { char _p414[2680]; int x8d8; };
        struct { char _p415[2684]; int x8dc; };
        struct { char _p416[2688]; int x8e0; };
        struct { char _p417[2692]; int x8e4; };
        struct { char _p418[2696]; int x8e8; };
        struct { char _p419[2700]; int x8ec; };
        struct { char _p420[2704]; int x8f0; };
        struct { char _p421[2708]; int x8f4; };
        struct { char _p422[2712]; int x8f8; };
        struct { char _p423[2716]; int x8fc; };
        struct { char _p424[2720]; int x900; };
        struct { char _p425[2724]; int x904; };
        struct { char _p426[2728]; int x908; };
        struct { char _p427[2732]; int x90c; };
        struct { char _p428[2736]; int x910; };
        struct { char _p429[2744]; ftHit hitbox[4]; };
        struct { char _p430[4792]; ftHit throw_hitbox[2]; };
        struct { char _p431[5816]; ftHit thrown_hitbox; };
        struct { char _p432[6328]; u8 team_unk; };
        struct { char _p433[6329]; u8 grabber_ply; };
        struct { char _p434[6330]; u8 hurt_num; };
        struct { char _p452[6336]; struct {
            union {
                char _mex_span[80];
                struct { FtHurtKind state; };
                struct { char _p435[4]; Vec3 hurt1_offset; };
                struct { char _p436[16]; Vec3 hurt2_offset; };
                struct { char _p437[28]; float scale; };
                struct { char _p438[32]; JOBJ *jobj; };
                struct { char _p439[40]; unsigned char is_updated : 1; };
                struct { char _p440[40]; unsigned char : 1; unsigned char x24_2 : 1; };
                struct { char _p441[40]; unsigned char : 2; unsigned char x24_3 : 1; };
                struct { char _p442[40]; unsigned char : 3; unsigned char x24_4 : 1; };
                struct { char _p443[40]; unsigned char : 4; unsigned char x24_5 : 1; };
                struct { char _p444[40]; unsigned char : 5; unsigned char x24_6 : 1; };
                struct { char _p445[40]; unsigned char : 6; unsigned char x24_7 : 1; };
                struct { char _p446[40]; unsigned char : 7; unsigned char x24_8 : 1; };
                struct { char _p447[44]; Vec3 hurt1_pos; };
                struct { char _p448[56]; Vec3 hurt2_pos; };
                struct { char _p449[68]; int bone_index; };
                struct { char _p450[72]; int hurt_kind; };
                struct { char _p451[76]; int is_grabbable; };
            };
        } hurtbox[15]; };
        struct { char _p457[7536]; struct {
            union {
                char _mex_span[56];
                struct { float size; };
                struct { char _p453[8]; JOBJ *jobj; };
                struct { char _p454[16]; Vec3 pos_cur; };
                struct { char _p455[28]; Vec3 pos_prev; };
                struct { char _p456[40]; Vec3 coll_pos; };
            };
        } coinbox[2]; };
        struct { char _p458[7648]; int dynamics_hit_num; };
        struct { char _p459[8104]; float x1828; };
        struct { char _p530[8112]; struct dmg {
            union {
                char _mex_span[328];
                struct { int behavior; };
                struct { char _p460[4]; float percent; };
                struct { char _p461[8]; int x1834; };
                struct { char _p462[12]; float percent_temp; };
                struct { char _p463[16]; int applied; };
                struct { char _p464[20]; int x1840; };
                struct { char _p473[24]; struct {
                    union {
                        char _mex_span[52];
                        struct { float direction; };
                        struct { char _p465[4]; int kb_angle; };
                        struct { char _p466[8]; int hurt_kind; };
                        struct { char _p467[12]; float force_applied; };
                        struct { char _p468[16]; Vec3 collpos; };
                        struct { char _p469[28]; int attribute; };
                        struct { char _p470[32]; int x1864; };
                        struct { char _p471[40]; GOBJ *source; };
                        struct { char _p472[48]; float percent; };
                    };
                } hit_log; };
                struct { char _p481[76]; struct {
                    union {
                        char _mex_span[44];
                        struct { float direction; };
                        struct { char _p474[4]; int kb_angle; };
                        struct { char _p475[8]; int hurt_kind; };
                        struct { char _p476[12]; float force_applied; };
                        struct { char _p477[16]; Vec3 collpos; };
                        struct { char _p478[28]; int attribute; };
                        struct { char _p479[32]; int x1864; };
                        struct { char _p480[40]; float percent; };
                    };
                } tip_log; };
                struct { char _p482[120]; float tip_hitlag; };
                struct { char _p483[124]; float tip_force_applied; };
                struct { char _p484[128]; float kb_mag; };
                struct { char _p485[132]; int x18a8; };
                struct { char _p486[136]; int time_since_hit; };
                struct { char _p487[140]; float armor_unk; };
                struct { char _p488[144]; float armor; };
                struct { char _p489[148]; Vec2 vibrate_offset; };
                struct { char _p490[156]; int x18c0; };
                struct { char _p491[160]; int source_ply; };
                struct { char _p492[164]; int x18c8; };
                struct { char _p493[168]; int x18cc; };
                struct { char _p494[172]; int x18d0; };
                struct { char _p495[176]; int x18d4; };
                struct { char _p496[180]; int x18d8; };
                struct { char _p497[184]; int x18dc; };
                struct { char _p498[188]; int x18e0; };
                struct { char _p499[192]; int x18e4; };
                struct { char _p500[196]; int x18e8; };
                struct { char _p501[200]; u16 atk_instance_hurtby; };
                struct { char _p502[204]; int x18f0; };
                struct { char _p503[208]; int x18f4; };
                struct { char _p504[212]; u8 vibrate_index; };
                struct { char _p505[213]; u8 x18f9; };
                struct { char _p506[214]; u16 vibrate_timer; };
                struct { char _p507[216]; u8 vibrate_index_cur; };
                struct { char _p508[217]; u8 vibrate_offset_num; };
                struct { char _p509[220]; Vec2 ground_slope; };
                struct { char _p510[228]; int x1908; };
                struct { char _p511[232]; void *random_sfx_table; };
                struct { char _p512[240]; int offscreen_damage_timer; };
                struct { char _p513[244]; int x1914; };
                struct { char _p516[248]; struct {
                    union {
                        char _mex_span[12];
                        struct { int dmg_dealt; };
                        struct { char _p514[4]; float dmg_based_rate_mult; };
                        struct { char _p515[8]; float dir; };
                    };
                } rebound; };
                struct { char _p517[260]; int x1924; };
                struct { char _p518[264]; int x1928; };
                struct { char _p519[268]; int x192c; };
                struct { char _p521[272]; struct {
                    union {
                        char _mex_span[24];
                        struct { Vec3 pos_prev; };
                        struct { char _p520[12]; Vec3 pos_cur; };
                    };
                } footstool; };
                struct { char _p522[296]; int x1948; };
                struct { char _p523[300]; int x194c; };
                struct { char _p524[304]; int x1950; };
                struct { char _p525[308]; float x1954; };
                struct { char _p526[312]; float hitlag_env_frames; };
                struct { char _p527[316]; float hitlag_frames; };
                struct { char _p528[320]; float vibrate_mult; };
                struct { char _p529[324]; float x1964; };
            };
        } dmg; };
        struct { char _p532[8440]; struct jump {
            union {
                char _mex_span[2];
                struct { char jumps_used; };
                struct { char _p531[1]; char walljumps_used; };
            };
        } jump; };
        struct { char _p533[8444]; float hitlag_mult; };
        struct { char _p534[8448]; int x1970; };
        struct { char _p535[8456]; GOBJ *item_held; };
        struct { char _p536[8464]; GOBJ *x1978; };
        struct { char _p537[8480]; GOBJ *item_head; };
        struct { char _p538[8488]; GOBJ *item_held_spec; };
        struct { char _p542[8496]; struct hurt {
            union {
                char _mex_span[16];
                struct { int kind_script; };
                struct { char _p539[4]; int kind_game; };
                struct { char _p541[8]; struct {
                    union {
                        char _mex_span[8];
                        struct { int ledge; };
                        struct { char _p540[4]; int respawn; };
                    };
                } intang_frames; };
            };
        } hurt; };
        struct { char _p552[8512]; struct shield {
            union {
                char _mex_span[44];
                struct { float health; };
                struct { char _p543[4]; float lightshield_amt; };
                struct { char _p544[8]; int dmg_taken; };
                struct { char _p545[12]; int dmg_taken2; };
                struct { char _p546[16]; GOBJ *dmg_source; };
                struct { char _p547[24]; float hit_direction; };
                struct { char _p548[28]; int hit_attr; };
                struct { char _p549[32]; float x19b4; };
                struct { char _p550[36]; float x19b8; };
                struct { char _p551[40]; int dmg_taken3; };
            };
        } shield; };
        struct { char _p557[8560]; struct shield_bubble {
            union {
                char _mex_span[40];
                struct { JOBJ *bone; };
                struct { char _p553[8]; unsigned char is_checked : 1; };
                struct { char _p554[12]; Vec3 pos; };
                struct { char _p555[24]; Vec3 offset; };
                struct { char _p556[36]; float size_mult; };
            };
        } shield_bubble; };
        struct { char _p562[8600]; struct reflect_bubble {
            union {
                char _mex_span[40];
                struct { JOBJ *bone; };
                struct { char _p558[8]; unsigned char is_checked : 1; };
                struct { char _p559[12]; Vec3 pos; };
                struct { char _p560[24]; Vec3 offset; };
                struct { char _p561[36]; float size_mult; };
            };
        } reflect_bubble; };
        struct { char _p567[8640]; struct absorb_bubble {
            union {
                char _mex_span[40];
                struct { JOBJ *bone; };
                struct { char _p563[8]; unsigned char is_checked : 1; };
                struct { char _p564[12]; Vec3 pos; };
                struct { char _p565[24]; Vec3 offset; };
                struct { char _p566[36]; float size_mult; };
            };
        } absorb_bubble; };
        struct { char _p571[8680]; struct reflect_hit {
            union {
                char _mex_span[16];
                struct { float hit_direction; };
                struct { char _p568[4]; int max_dmg; };
                struct { char _p569[8]; float dmg_mult; };
                struct { char _p570[12]; int is_break; };
            };
        } reflect_hit; };
        struct { char _p575[8696]; struct absorb_hit {
            union {
                char _mex_span[16];
                struct { int x1a3c; };
                struct { char _p572[4]; float hit_direction; };
                struct { char _p573[8]; int dmg_taken; };
                struct { char _p574[12]; int hits_taken; };
            };
        } absorb_hit; };
        struct { char _p589[8712]; struct grab {
            union {
                char _mex_span[80];
                struct { float grab_timer; };
                struct { char _p576[4]; int x1a50; };
                struct { char _p577[8]; int x1a54; };
                struct { char _p578[16]; GOBJ *victim; };
                struct { char _p579[24]; GOBJ *attacker; };
                struct { char _p580[32]; GOBJ *item; };
                struct { char _p581[48]; u16 x1a68; };
                struct { char _p582[50]; u16 vuln; };
                struct { char _p583[52]; int x1a6c; };
                struct { char _p584[56]; int x1a70; };
                struct { char _p585[60]; Vec2 release_pos; };
                struct { char _p586[68]; int x1a7c; };
                struct { char _p587[72]; int x1a80; };
                struct { char _p588[76]; int x1a84; };
            };
        } grab; };
        struct { char _p657[8792]; struct {
            union {
                char _mex_span[1424];
                struct { int held; };
                struct { char _p590[4]; s8 lstickX; };
                struct { char _p591[5]; s8 lstickY; };
                struct { char _p592[6]; s8 cstickX; };
                struct { char _p593[7]; s8 cstickY; };
                struct { char _p594[8]; u8 ltrigger; };
                struct { char _p595[9]; u8 rtrigger; };
                struct { char _p596[12]; int ai; };
                struct { char _p597[16]; int level; };
                struct { char _p598[20]; int x14; };
                struct { char _p599[24]; int scenario_id; };
                struct { char _p600[28]; int x1c; };
                struct { char _p601[32]; int x20; };
                struct { char _p602[36]; int x24; };
                struct { char _p603[40]; int x28; };
                struct { char _p604[44]; int x2c; };
                struct { char _p605[48]; int x30; };
                struct { char _p606[52]; int x34; };
                struct { char _p607[56]; float x38; };
                struct { char _p608[60]; float x3c; };
                struct { char _p609[64]; float x40; };
                struct { char _p610[72]; void *x44; };
                struct { char _p611[80]; void *x48; };
                struct { char _p612[104]; float x54; };
                struct { char _p613[108]; float x58; };
                struct { char _p614[112]; float x5c; };
                struct { char _p615[116]; int x60; };
                struct { char _p616[120]; int x64; };
                struct { char _p617[124]; int x68; };
                struct { char _p618[128]; int x6c; };
                struct { char _p619[132]; int x70; };
                struct { char _p620[136]; int x74; };
                struct { char _p621[140]; int proc_num; };
                struct { char _p622[144]; int scenario_check_num; };
                struct { char _p623[148]; int x80; };
                struct { char _p624[152]; int x84; };
                struct { char _p625[156]; int x88; };
                struct { char _p626[160]; int x8c; };
                struct { char _p627[164]; int x90; };
                struct { char _p628[168]; int x94; };
                struct { char _p629[172]; int x98; };
                struct { char _p630[176]; int x9c; };
                struct { char _p631[180]; int xa0; };
                struct { char _p632[184]; int xa4; };
                struct { char _p633[188]; int xa8; };
                struct { char _p634[192]; int xac; };
                struct { char _p635[196]; int xb0; };
                struct { char _p636[200]; int xb4; };
                struct { char _p637[204]; int xb8; };
                struct { char _p638[208]; int xbc; };
                struct { char _p639[212]; int xc0; };
                struct { char _p640[216]; int xc4; };
                struct { char _p641[220]; u8 xc8; };
                struct { char _p642[224]; int xcc; };
                struct { char _p643[228]; int xd0; };
                struct { char _p644[232]; int xd4; };
                struct { char _p645[236]; int xd8; };
                struct { char _p646[240]; int xdc; };
                struct { char _p647[244]; int xe0; };
                struct { char _p648[248]; int xe4; };
                struct { char _p649[252]; int xe8; };
                struct { char _p650[256]; u8 xec; };
                struct { char _p651[284]; CPULeaderLog leader_log[30]; };
                struct { char _p652[1128]; void *unk_curr; };
                struct { char _p653[1136]; void *scenario_curr; };
                struct { char _p654[1152]; void *x450; };
                struct { char _p655[1160]; u8 cmdscript_queue[256]; };
                struct { char _p656[1416]; void *cmdscript_curr; };
            };
        } cpu; };
        struct { char _p658[10216]; int x1fe0; };
        struct { char _p659[10220]; int x1fe4; };
        struct { char _p660[10224]; int x1fe8; };
        struct { char _p661[10228]; int x1fec; };
        struct { char _p662[10232]; int x1ff0; };
        struct { char _p663[10236]; int x1ff4; };
        struct { char _p664[10240]; int x1ff8; };
        struct { char _p665[10244]; int x1ffc; };
        struct { char _p666[10248]; int x2000; };
        struct { char _p667[10256]; int x2004; };
        struct { char _p668[10260]; int x2008; };
        struct { char _p669[10264]; int x200c; };
        struct { char _p670[10268]; int x2010; };
        struct { char _p671[10272]; int x2014; };
        struct { char _p672[10276]; int x2018; };
        struct { char _p673[10280]; int x201c; };
        struct { char _p674[10284]; int x2020; };
        struct { char _p675[10288]; int x2024; };
        struct { char _p676[10292]; int metal_timer; };
        struct { char _p677[10296]; int metal_health; };
        struct { char _p678[10300]; int x2030; };
        struct { char _p679[10304]; int x2034; };
        struct { char _p680[10308]; int x2038; };
        struct { char _p681[10312]; int x203c; };
        struct { char _p682[10328]; int x2044; };
        struct { char _p683[10332]; int x2048; };
        struct { char _p684[10336]; int x204c; };
        struct { char _p685[10340]; int x2050; };
        struct { char _p686[10344]; int x2054; };
        struct { char _p687[10348]; int x2058; };
        struct { char _p688[10352]; int x205c; };
        struct { char _p689[10356]; int x2060; };
        struct { char _p690[10360]; int ledge_cooldown; };
        struct { char _p691[10364]; int atk_kind; };
        struct { char _p692[10368]; int x206c; };
        struct { char _p693[10372]; u8 x2070; };
        struct { char _p694[10374]; u8 : 4; u8 state_kind : 4; };
        struct { char _p695[10374]; u8 x2071_x0f : 4; };
        struct { char _p696[10374]; u8 x2072; };
        struct { char _p697[10375]; u8 x2073; };
        struct { char _p698[10376]; int x2074; };
        struct { char _p699[10380]; int x2078; };
        struct { char _p700[10384]; int x207c; };
        struct { char _p701[10388]; int x2080; };
        struct { char _p702[10392]; int x2084; };
        struct { char _p703[10396]; u16 atk_instance; };
        struct { char _p704[10400]; int x208c; };
        struct { char _p705[10404]; u16 combo_count; };
        struct { char _p706[10406]; u16 x2092; };
        struct { char _p707[10408]; GOBJ *victim; };
        struct { char _p708[10416]; u16 x2098; };
        struct { char _p709[10418]; u16 is_hide_player_indicator; };
        struct { char _p710[10420]; int x209c; };
        struct { char _p711[10424]; JOBJ *accessory; };
        struct { char _p712[10432]; int x20a4; };
        struct { char _p713[10440]; void *shadow; };
        struct { char _p719[10456]; struct afterimage {
            union {
                char _mex_span[82];
                struct { struct {
                    union {
                        char _mex_span[24];
                        struct { Vec3 pos; };
                        struct { char _p714[12]; Vec3 rot; };
                    };
                } key[3]; };
                struct { char _p715[72]; float afterimage_bottom; };
                struct { char _p716[76]; float afterimage_top; };
                struct { char _p717[80]; u8 afterimage_state; };
                struct { char _p718[81]; unsigned char afterimage_num : 7; };
            };
        } afterimage; };
        struct { char _p720[10540]; int x2104; };
        struct { char _p721[10544]; int x2108; };
        struct { char _p723[10548]; struct {
            union {
                char _mex_span[8];
                struct { s8 timer; };
                struct { char _p722[4]; float direction; };
            };
        } wall; };
        struct { char _p734[10556]; struct smash {
            union {
                char _mex_span[40];
                struct { int state; };
                struct { char _p724[4]; int frame; };
                struct { char _p725[8]; float hold_frame; };
                struct { char _p726[12]; float dmg_mult; };
                struct { char _p727[16]; float speed_mult; };
                struct { char _p728[20]; int x2128; };
                struct { char _p729[24]; int x212c; };
                struct { char _p730[28]; int is_sfx_played; };
                struct { char _p731[32]; u8 vibrate_frame; };
                struct { char _p732[33]; u8 x22135; };
                struct { char _p733[36]; float since_hitbox; };
            };
        } smash; };
        struct { char _p735[10596]; int x213c; };
        struct { char _p736[10600]; int x2140; };
        struct { char _p737[10604]; int x2144; };
        struct { char _p738[10608]; int x2148; };
        struct { char _p739[10612]; int x214c; };
        struct { char _p740[10616]; int x2150; };
        struct { char _p741[10620]; int x2154; };
        struct { char _p742[10624]; int x2158; };
        struct { char _p743[10628]; int x215c; };
        struct { char _p744[10632]; int x2160; };
        struct { char _p745[10636]; int x2164; };
        struct { char _p746[10640]; int x2168; };
        struct { char _p747[10644]; int x216c; };
        struct { char _p748[10648]; int x2170; };
        struct { char _p749[10652]; Vec3 thrown_origin; };
        struct { char _p750[10664]; int x2180; };
        struct { char _p751[10680]; int screen_pixel_x; };
        struct { char _p752[10684]; int screen_pixel_y; };
        struct { char _p779[10688]; struct cb {
            union {
                char _mex_span[216];
                struct { void (*OnGrabFighter_Self)(GOBJ *fighter); };
                struct { char _p753[8]; void (*x2194)(GOBJ *fighter); };
                struct { char _p754[16]; void (*OnGrabFighter_Victim)(GOBJ *victim, GOBJ *self); };
                struct { char _p755[24]; int (*IASA)(GOBJ *fighter); };
                struct { char _p756[32]; void (*Anim)(GOBJ *fighter); };
                struct { char _p757[40]; void (*Phys)(GOBJ *fighter); };
                struct { char _p758[48]; void (*Coll)(GOBJ *fighter); };
                struct { char _p759[56]; void (*Cam)(GOBJ *fighter); };
                struct { char _p760[64]; void (*Accessory1)(GOBJ *fighter); };
                struct { char _p761[72]; void (*Accessory_Persist)(GOBJ *fighter); };
                struct { char _p762[80]; void (*Accessory_Freeze)(GOBJ *fighter); };
                struct { char _p763[88]; void (*Accessory4)(GOBJ *fighter); };
                struct { char _p764[96]; void (*OnGiveDamage)(GOBJ *fighter); };
                struct { char _p765[104]; void (*OnShieldHit)(GOBJ *fighter); };
                struct { char _p766[112]; void (*OnReflectHit)(GOBJ *fighter); };
                struct { char _p767[120]; void (*x21cc)(GOBJ *fighter); };
                struct { char _p768[128]; void (*EveryHitlag)(GOBJ *fighter); };
                struct { char _p769[136]; void (*EnterHitlag)(GOBJ *fighter); };
                struct { char _p770[144]; void (*ExitHitlag)(GOBJ *fighter); };
                struct { char _p771[152]; void (*OnTakeDamage)(GOBJ *fighter); };
                struct { char _p772[160]; void (*OnDeath_Persist)(GOBJ *fighter); };
                struct { char _p773[168]; void (*OnDeath_State)(GOBJ *fighter); };
                struct { char _p774[176]; void (*OnDeath3)(GOBJ *fighter); };
                struct { char _p775[184]; void (*OnStateChange)(GOBJ *fighter); };
                struct { char _p776[192]; void (*OnTakeDamage2)(GOBJ *fighter); };
                struct { char _p777[200]; void (*OnHurtboxDetect)(GOBJ *fighter); };
                struct { char _p778[208]; void (*OnSpin)(GOBJ *fighter); };
            };
        } cb; };
        struct { char _p780[10904]; unsigned char : 7; unsigned char x21fc_1 : 1; };
        struct { char _p781[10904]; unsigned char : 6; unsigned char show_center_sphere : 1; };
        struct { char _p782[10904]; unsigned char : 5; unsigned char show_item_pickup : 1; };
        struct { char _p783[10904]; unsigned char : 4; unsigned char show_cpu_ai : 1; };
        struct { char _p784[10904]; unsigned char : 3; unsigned char show_footstool : 1; };
        struct { char _p785[10904]; unsigned char : 2; unsigned char show_dynamics : 1; };
        struct { char _p786[10904]; unsigned char : 1; unsigned char show_hit : 1; };
        struct { char _p787[10904]; unsigned char show_model : 1; };
        struct { char _p791[10908]; struct ftcmd_var {
            union {
                char _mex_span[16];
                struct { int flag0; };
                struct { char _p788[4]; int flag1; };
                struct { char _p789[8]; int flag2; };
                struct { char _p790[12]; int flag3; };
            };
        } ftcmd_var; };
        struct { char _p934[10924]; struct flags {
            union {
                char _mex_span[27];
                struct { unsigned char : 7; unsigned char throw_1 : 1; };
                struct { unsigned char : 6; unsigned char throw_2 : 1; };
                struct { unsigned char : 5; unsigned char throw_3 : 1; };
                struct { unsigned char : 4; unsigned char throw_release : 1; };
                struct { unsigned char : 3; unsigned char throw_turn : 1; };
                struct { unsigned char : 2; unsigned char throw_6 : 1; };
                struct { unsigned char : 1; unsigned char throw_7 : 1; };
                struct { unsigned char throw_8 : 1; };
                struct { char _p800[1]; char x2211; };
                struct { char _p801[2]; char x2212; };
                struct { char _p802[3]; char x2213; };
                struct { char _p803[4]; char x2214; };
                struct { char _p804[5]; char x2215; };
                struct { char _p805[7]; char x2217; };
                struct { char _p806[8]; unsigned char past_iasa : 1; };
                struct { char _p807[8]; unsigned char : 1; unsigned char x2218_2 : 1; };
                struct { char _p808[8]; unsigned char : 2; unsigned char has_rapid_jab : 1; };
                struct { char _p809[8]; unsigned char : 3; unsigned char reflect_enable : 1; };
                struct { char _p810[8]; unsigned char : 4; unsigned char reflect_nochangeowner : 1; };
                struct { char _p811[8]; unsigned char : 5; unsigned char x2218_6 : 1; };
                struct { char _p812[8]; unsigned char : 6; unsigned char absorb_enable : 1; };
                struct { char _p813[8]; unsigned char : 7; unsigned char absorb_unk : 1; };
                struct { char _p814[9]; unsigned char persistent_gfx : 1; };
                struct { char _p815[9]; unsigned char : 1; unsigned char immune : 1; };
                struct { char _p816[9]; unsigned char : 2; unsigned char is_ignore_death : 1; };
                struct { char _p817[9]; unsigned char : 3; unsigned char hitbox_active : 1; };
                struct { char _p818[9]; unsigned char : 4; unsigned char x2219_5 : 1; };
                struct { char _p819[9]; unsigned char : 5; unsigned char freeze : 1; };
                struct { char _p820[9]; unsigned char : 6; unsigned char hitlag_unk : 1; };
                struct { char _p821[9]; unsigned char : 7; unsigned char hitlag_unk2 : 1; };
                struct { char _p822[10]; unsigned char x221a_1 : 1; };
                struct { char _p823[10]; unsigned char : 1; unsigned char x221a_2 : 1; };
                struct { char _p824[10]; unsigned char : 2; unsigned char hitlag : 1; };
                struct { char _p825[10]; unsigned char : 3; unsigned char hitlag_victim : 1; };
                struct { char _p826[10]; unsigned char : 4; unsigned char is_fastfall : 1; };
                struct { char _p827[10]; unsigned char : 5; unsigned char no_hurt_script : 1; };
                struct { char _p828[10]; unsigned char : 6; unsigned char x221a_7 : 1; };
                struct { char _p829[10]; unsigned char : 7; unsigned char gfx_persist : 1; };
                struct { char _p830[11]; unsigned char shield_enable : 1; };
                struct { char _p831[11]; unsigned char : 1; unsigned char shield_x40 : 1; };
                struct { char _p832[11]; unsigned char : 2; unsigned char shield_x20 : 1; };
                struct { char _p833[11]; unsigned char : 3; unsigned char shield_x10 : 1; };
                struct { char _p834[11]; unsigned char : 4; unsigned char shield_x8 : 1; };
                struct { char _p835[11]; unsigned char : 5; unsigned char x221b_grab : 1; };
                struct { char _p836[11]; unsigned char : 6; unsigned char x221b_7 : 1; };
                struct { char _p837[11]; unsigned char : 7; unsigned char attacker_attached_to_victim : 1; };
                struct { char _p838[12]; unsigned char hit_by_grabber : 1; };
                struct { char _p839[12]; unsigned char : 1; unsigned char x221c_2 : 1; };
                struct { char _p840[12]; unsigned char : 2; unsigned char is_powershield : 1; };
                struct { char _p841[12]; unsigned char : 3; unsigned char x221c_4 : 1; };
                struct { char _p842[12]; unsigned char : 4; unsigned char x221c_5 : 1; };
                struct { char _p843[12]; unsigned char : 5; unsigned char x221c_6 : 1; };
                struct { char _p844[12]; unsigned char : 6; unsigned char hitstun : 1; };
                struct { char _p845[13]; unsigned char : 1; unsigned char ik_orientation : 1; };
                struct { char _p846[13]; unsigned char ik_rfoot : 1; };
                struct { char _p847[12]; unsigned char : 7; unsigned char ik_lfoot : 1; };
                struct { char _p848[13]; unsigned char : 2; unsigned char ftvis_reqrevert : 1; };
                struct { char _p849[13]; unsigned char : 3; unsigned char input_enable : 1; };
                struct { char _p850[13]; unsigned char : 4; unsigned char x221d_5 : 1; };
                struct { char _p851[13]; unsigned char : 5; unsigned char nudge_disable : 1; };
                struct { char _p852[13]; unsigned char : 6; unsigned char ground_ignore : 1; };
                struct { char _p853[13]; unsigned char : 7; unsigned char x221d_8 : 1; };
                struct { char _p854[14]; unsigned char invisible : 1; };
                struct { char _p855[14]; unsigned char : 1; unsigned char x221e_2 : 1; };
                struct { char _p856[14]; unsigned char : 2; unsigned char x221e_3 : 1; };
                struct { char _p857[14]; unsigned char : 3; unsigned char item_visible : 1; };
                struct { char _p858[14]; unsigned char : 4; unsigned char item_head_visible : 1; };
                struct { char _p859[14]; unsigned char : 5; unsigned char invisible_script : 1; };
                struct { char _p860[14]; unsigned char : 6; unsigned char x221e_7 : 1; };
                struct { char _p861[14]; unsigned char : 7; unsigned char x221e_8 : 1; };
                struct { char _p862[15]; unsigned char is_offscreen : 1; };
                struct { char _p863[15]; unsigned char : 1; unsigned char dead : 1; };
                struct { char _p864[15]; unsigned char : 2; unsigned char x221f_3 : 1; };
                struct { char _p865[15]; unsigned char : 3; unsigned char sleep : 1; };
                struct { char _p866[15]; unsigned char : 4; unsigned char ms : 1; };
                struct { char _p867[15]; unsigned char : 5; unsigned char x221f_6 : 1; };
                struct { char _p868[15]; unsigned char : 6; unsigned char x221f_7 : 1; };
                struct { char _p869[15]; unsigned char : 7; unsigned char x221f_8 : 1; };
                struct { char _p870[18]; unsigned char x2222_1 : 1; };
                struct { char _p871[18]; unsigned char : 1; unsigned char is_multijump : 1; };
                struct { char _p872[18]; unsigned char : 2; unsigned char x2222_grab : 1; };
                struct { char _p873[18]; unsigned char : 3; unsigned char ceilko_nokb : 1; };
                struct { char _p874[18]; unsigned char : 4; unsigned char x2222_5 : 1; };
                struct { char _p875[18]; unsigned char : 5; unsigned char has_follower : 1; };
                struct { char _p876[18]; unsigned char : 6; unsigned char x2222_skip_phys_update : 1; };
                struct { char _p877[18]; unsigned char : 7; unsigned char x2222_8 : 1; };
                struct { char _p878[19]; unsigned char x2223_1 : 1; };
                struct { char _p879[19]; unsigned char : 1; unsigned char x2223_2 : 1; };
                struct { char _p880[19]; unsigned char : 2; unsigned char x2223_3 : 1; };
                struct { char _p881[19]; unsigned char : 3; unsigned char x2223_4 : 1; };
                struct { char _p882[19]; unsigned char : 4; unsigned char x2223_5 : 1; };
                struct { char _p883[19]; unsigned char : 5; unsigned char x2223_6 : 1; };
                struct { char _p884[19]; unsigned char : 6; unsigned char is_always_metal : 1; };
                struct { char _p885[19]; unsigned char : 7; unsigned char is_metal : 1; };
                struct { char _p886[20]; unsigned char x2224_1 : 1; };
                struct { char _p887[20]; unsigned char : 1; unsigned char x2224_2 : 1; };
                struct { char _p888[20]; unsigned char : 2; unsigned char stamina_dead : 1; };
                struct { char _p889[20]; unsigned char : 3; unsigned char x2224_4 : 1; };
                struct { char _p890[20]; unsigned char : 4; unsigned char x2224_5 : 1; };
                struct { char _p891[20]; unsigned char : 5; unsigned char x2224_6 : 1; };
                struct { char _p892[20]; unsigned char : 6; unsigned char x2224_7 : 1; };
                struct { char _p893[20]; unsigned char : 7; unsigned char can_walljump : 1; };
                struct { char _p894[21]; unsigned char x2225_1 : 1; };
                struct { char _p895[21]; unsigned char : 1; unsigned char x2225_2 : 1; };
                struct { char _p896[21]; unsigned char : 2; unsigned char has_model_addition : 1; };
                struct { char _p897[21]; unsigned char : 3; unsigned char x2225_4 : 1; };
                struct { char _p898[21]; unsigned char : 4; unsigned char x2225_5 : 1; };
                struct { char _p899[21]; unsigned char : 5; unsigned char x2225_6 : 1; };
                struct { char _p900[21]; unsigned char : 6; unsigned char is_mute_voice : 1; };
                struct { char _p901[21]; unsigned char : 7; unsigned char is_stamina : 1; };
                struct { char _p902[22]; unsigned char x2226_1 : 1; };
                struct { char _p903[22]; unsigned char : 1; unsigned char x2226_2 : 1; };
                struct { char _p904[22]; unsigned char : 2; unsigned char is_robj_child : 1; };
                struct { char _p905[22]; unsigned char : 3; unsigned char x2226_x10 : 1; };
                struct { char _p906[22]; unsigned char : 4; unsigned char cloak1 : 1; };
                struct { char _p907[22]; unsigned char : 5; unsigned char cloak2 : 1; };
                struct { char _p908[22]; unsigned char : 6; unsigned char x2226_7 : 1; };
                struct { char _p909[22]; unsigned char : 7; unsigned char x2226_8 : 1; };
                struct { char _p910[24]; char x2228_1 : 1; };
                struct { char _p911[24]; char : 1; char x2228_2 : 1; };
                struct { char _p912[24]; char : 2; char use_sandbag_logic : 1; };
                struct { char _p913[24]; char : 4; char x2228_4 : 1; };
                struct { char _p914[24]; char : 3; char x2228_5 : 1; };
                struct { char _p915[24]; char : 5; char is_ignore_death3 : 1; };
                struct { char _p916[24]; char : 6; char used_tether : 1; };
                struct { char _p917[24]; char : 7; char last_lstick_x_dir : 1; };
                struct { char _p918[25]; unsigned char x2229_1 : 1; };
                struct { char _p919[25]; unsigned char : 1; unsigned char x2229_2 : 1; };
                struct { char _p920[25]; unsigned char : 2; unsigned char x2229_3 : 1; };
                struct { char _p921[25]; unsigned char : 3; unsigned char is_ignore_offscreen : 1; };
                struct { char _p922[25]; unsigned char : 4; unsigned char skip_coin_collcheck : 1; };
                struct { char _p923[25]; unsigned char : 5; unsigned char x2229_6 : 1; };
                struct { char _p924[25]; unsigned char : 6; unsigned char x2229_7 : 1; };
                struct { char _p925[25]; unsigned char : 7; unsigned char no_reaction_always : 1; };
                struct { char _p926[26]; unsigned char x222a_x80 : 1; };
                struct { char _p927[26]; unsigned char : 1; unsigned char is_ignore_death2 : 1; };
                struct { char _p928[26]; unsigned char : 2; unsigned char x222a_x20 : 1; };
                struct { char _p929[26]; unsigned char : 4; unsigned char x222a_x10 : 1; };
                struct { char _p930[26]; unsigned char : 3; unsigned char x222a_x08 : 1; };
                struct { char _p931[26]; unsigned char : 5; unsigned char x222a_x04 : 1; };
                struct { char _p932[26]; unsigned char : 6; unsigned char x222a_x02 : 1; };
                struct { char _p933[26]; unsigned char : 7; unsigned char x222a_x01 : 1; };
            };
        } flags; };
        struct { char _p986[10952]; struct fighter_var {
            union {
                char _mex_span[348];
                struct { int ft_var1; };
                struct { char _p935[4]; int ft_var2; };
                struct { char _p936[8]; int ft_var3; };
                struct { char _p937[12]; int ft_var4; };
                struct { char _p938[16]; int ft_var5; };
                struct { char _p939[20]; int ft_var6; };
                struct { char _p940[24]; int ft_var7; };
                struct { char _p941[28]; int ft_var8; };
                struct { char _p942[32]; int ft_var9; };
                struct { char _p943[36]; int ft_var10; };
                struct { char _p944[40]; int ft_var11; };
                struct { char _p945[44]; int ft_var12; };
                struct { char _p946[48]; int ft_var13; };
                struct { char _p947[52]; int ft_var14; };
                struct { char _p948[56]; int ft_var15; };
                struct { char _p949[60]; int ft_var16; };
                struct { char _p950[64]; int ft_var17; };
                struct { char _p951[68]; int ft_var18; };
                struct { char _p952[72]; int ft_var19; };
                struct { char _p953[76]; int ft_var20; };
                struct { char _p954[92]; int ft_var21; };
                struct { char _p955[96]; int ft_var22; };
                struct { char _p956[100]; int ft_var23; };
                struct { char _p957[104]; int ft_var24; };
                struct { char _p958[96]; int ft_var25; };
                struct { char _p959[100]; int ft_var26; };
                struct { char _p960[104]; int ft_var27; };
                struct { char _p961[108]; int ft_var28; };
                struct { char _p962[112]; int ft_var29; };
                struct { char _p963[116]; int ft_var30; };
                struct { char _p964[120]; int ft_var31; };
                struct { char _p965[124]; int ft_var32; };
                struct { char _p966[224]; int ft_var33; };
                struct { char _p967[228]; int ft_var34; };
                struct { char _p968[232]; int ft_var35; };
                struct { char _p969[236]; int ft_var36; };
                struct { char _p970[240]; int ft_var37; };
                struct { char _p971[244]; int ft_var38; };
                struct { char _p972[152]; int ft_var39; };
                struct { char _p973[256]; int ft_var40; };
                struct { char _p974[160]; int ft_var41; };
                struct { char _p975[164]; int ft_var42; };
                struct { char _p976[280]; int ft_var43; };
                struct { char _p977[172]; int ft_var44; };
                struct { char _p978[176]; int ft_var45; };
                struct { char _p979[304]; int ft_var46; };
                struct { char _p980[184]; int ft_var47; };
                struct { char _p981[320]; int ft_var48; };
                struct { char _p982[192]; int ft_var49; };
                struct { char _p983[336]; int ft_var50; };
                struct { char _p984[340]; int ft_var51; };
                struct { char _p985[344]; int ft_var52; };
            };
        } fighter_var; };
        struct { char _p987[11160]; int x22fc; };
        struct { char _p988[11312]; int x2300; };
        struct { char _p989[11316]; int x2304; };
        struct { char _p990[11172]; int x2308; };
        struct { char _p991[11328]; int x230c; };
        struct { char _p992[11332]; u16 x2310; };
        struct { char _p993[11182]; u16 x2312; };
        struct { char _p994[11336]; float x2314; };
        struct { char _p995[11340]; float x2318; };
        struct { char _p996[11344]; float x231c; };
        struct { char _p997[11196]; int x2320; };
        struct { char _p998[11352]; int stage_internal; };
        struct { char _p999[11356]; int line_damage_immunity; };
        struct { char _p1000[11360]; int ftchkdevice_immunity; };
        struct { char _p1001[11364]; int x2330; };
        struct { char _p1002[11368]; int x2334; };
        struct { char _p1003[11372]; int x2338; };
        struct { char _p1004[11376]; int x233c; };
        struct { char _p1022[11384]; struct state_var {
            union {
                char _mex_span[72];
                struct { int state_var1; };
                struct { char _p1005[4]; int state_var2; };
                struct { char _p1006[8]; int state_var3; };
                struct { char _p1007[12]; int state_var4; };
                struct { char _p1008[16]; int state_var5; };
                struct { char _p1009[20]; int state_var6; };
                struct { char _p1010[24]; int state_var7; };
                struct { char _p1011[28]; int state_var8; };
                struct { char _p1012[32]; int state_var9; };
                struct { char _p1013[36]; int state_var10; };
                struct { char _p1014[40]; int state_var11; };
                struct { char _p1015[44]; int state_var12; };
                struct { char _p1016[48]; int state_var13; };
                struct { char _p1017[52]; int state_var14; };
                struct { char _p1018[56]; int state_var15; };
                struct { char _p1019[60]; int state_var16; };
                struct { char _p1020[64]; int state_var17; };
                struct { char _p1021[68]; int state_var18; };
            };
        } state_var; };
        struct { char _p1023[11456]; int x2388; };
        struct { char _p1024[11460]; int x238c; };
        struct { char _p1025[11464]; int x2390; };
        struct { char _p1026[11468]; int x2394; };
        struct { char _p1027[11472]; int x2398; };
        struct { char _p1028[11476]; int x239c; };
        struct { char _p1029[11480]; int x23a0; };
        struct { char _p1030[11484]; int x23a4; };
        struct { char _p1031[11488]; int x23a8; };
        struct { char _p1032[11492]; int x23ac; };
        struct { char _p1033[11496]; int x23b0; };
        struct { char _p1034[11500]; int x23b4; };
        struct { char _p1035[11504]; int x23b8; };
        struct { char _p1036[11508]; int x23bc; };
        struct { char _p1037[11512]; int x23c0; };
        struct { char _p1038[11516]; int x23c4; };
        struct { char _p1039[11520]; int x23c8; };
        struct { char _p1040[11524]; int x23cc; };
        struct { char _p1041[11528]; int x23d0; };
        struct { char _p1042[11532]; int x23d4; };
        struct { char _p1043[11536]; int x23d8; };
        struct { char _p1044[11540]; int x23dc; };
        struct { char _p1045[11544]; int x23e0; };
        struct { char _p1046[11548]; int x23e4; };
        struct { char _p1047[11552]; int x23e8; };
        struct { char _p1050[11560]; struct MEX {
            union {
                char _mex_span[19];
                struct { int anim_owner; };
                struct { char _p1048[8]; GOBJ *kb_abilitysource; };
                struct { char _p1049[16]; u8 ucf_stick_x[3]; };
            };
        } MEX; };
        struct { char _p1071[11584]; struct TM {
            union {
                char _mex_span[320];
                struct { s16 state_frame; };
                struct { char _p1051[2]; s16 shield_frame; };
                struct { char _p1052[4]; u16 state_prev[6]; };
                struct { char _p1053[16]; u16 state_prev_frames[6]; };
                struct { char _p1054[28]; u16 last_move_hurt; };
                struct { char _p1055[30]; u16 vuln_frames; };
                struct { char _p1056[32]; u16 can_fastfall_frames; };
                struct { char _p1057[34]; u16 iasa_frames; };
                struct { char _p1058[36]; int post_hitstun_frames; };
                struct { char _p1059[40]; FighterData *fighter_hurt_shield; };
                struct { char _p1060[48]; void *cb_anim; };
                struct { char _p1063[56]; struct {
                    union {
                        char _mex_span[8];
                        struct { struct {
                            union {
                                char _mex_span[8];
                                struct { float airdodge_angle; };
                                struct { char _p1061[4]; u32 hop_type; };
                            };
                        } wavedash; };
                        struct { struct {
                            union {
                                char _mex_span[4];
                                struct { u16 successful_sdi_inputs; };
                                struct { char _p1062[2]; u16 total_sdi_inputs; };
                            };
                        } sdi; };
                    };
                } tm_union; };
                struct { char _p1070[64]; struct {
                    union {
                        char _mex_span[256];
                        struct { u16 buttons; };
                        struct { char _p1064[2]; s8 stick_x; };
                        struct { char _p1065[3]; s8 stick_y; };
                        struct { char _p1066[4]; s8 cstick_x; };
                        struct { char _p1067[5]; s8 cstick_y; };
                        struct { char _p1068[6]; u8 ltrigger; };
                        struct { char _p1069[7]; u8 rtrigger; };
                    };
                } inputs[1]; };
            };
        } TM; };
        struct { char _p1072[2592]; int mu_ecb_bot_lock_frames; };
    };
};

struct FtMultiJumpDesc // exists in fighters special attributes
{
    int turn_frames;                    // turn frame length
    float turn_stick_x_min;             // min x value on left stick to trigger aerial turn
    float vel_stick_x_mult;             // jump x velocity = left stick x * this
    float aerial_drift_stick_mult_mult; // 0xC, multipler for the value in the fighter attribute
    float aerial_drift_max_mult;        // 0x10, multipler for the value in the fighter attribute
    float jump_vel_y[5];                // 0x14 subsequemt jumps Y velocities
    int jump_num;                       // 0x28, number of total aerial jumps
    int jump_state_start;               // 0x2C, state index for first aerial jump
    int x30;                            // 0x30,
};

struct FtParts // is in the fighter data
{
    int num;     // 0x2240
    DOBJ **dobj; // 0x2244   array of dobjs for the ftpar
    void *x8;    // 0x2248
    void *xc;    // 0x224C
};

struct FtPartsDesc
{
    int model_num; // 0x2250
    FtPartsLookup *lookups;
};

struct FtPartsLookup
{
    FtParts *x0;
    void *x4;
    void *x8;
    void *xc;
    void *x10;
};

struct FtPartsVis // is in the fighter data
{
    int num;                           // 0x0
    u8 x4[5];                          // 0x4 array of bools?
    FtPartsVisLookup *highpoly_table;  // 0x0C
    FtPartsVisLookup *lowpoly_table;   // 0x10
    FtPartsVisLookup *metalpoly_table; // 0x14
    FtPartsVisLookup *metalmain_table; // 0x18
    FtPartsVisLookup *x1C;             // 0x1C
};

struct FtSymbolLookup
{
    FtSymbols *archives;
    u8 num;
};

struct FtSymbols
{
    JOBJ *joint;          // 0x0
    void *matanim_joint;  // 0x4
    void *x8;             // 0x8
    void *xc;             // 0xc
    void *x10;            // 0x10
    HSD_Archive *costume; // 0x14
};

struct FtDatNameLookup
{
    char *filename;
    char *symbol;
};
struct FtKindDesc // this is really CKindDesc...?
{
    s8 ft_main;           // main fighter ft_kind
    s8 ft_sub;            // sub fighter ft_kind, -1 if none
    s8 no_spawn_together; //
};

/** State Structs **/
struct FtCliffCatch
{
    int ledge_index;
    float fall_timer;
    int timer;
};
struct FtDamage
{
    float hitstun; // 0x2340
    int x2344;
    int x2348;
    int x234c;
    int x2350;
    float x2354;
    u8 x2358;
    u8 hit_env_kind;   // 0x2359, 0 = none, 1 = ground 2 = wall, 3 = ceiling
    u8 is_meteor;      // 0x235a
    u8 meteor_lockout; // 0x235b
};
struct FtLanding
{
    int can_interrupt; // 0x2340
};
struct FtDead
{
    int timer;
};

struct FtEntry
{
    int delay_frames;
};
/** Script Structs **/
struct FtScript
{
    unsigned opcode : 6;
    union
    {
        struct
        {
            unsigned frames : 26;
        } timer_sync; // 2
        struct
        {
            unsigned frame : 26;
        } timer_async; // 3
        struct
        {
            unsigned bone : 8;
            unsigned use_common_bone_id : 1;
            unsigned destroy_on_state_change : 1;
            unsigned unk1 : 16;
            unsigned id : 16;
            unsigned unk2 : 16;
            unsigned offset_z : 16;
            unsigned offset_y : 16;
            unsigned offset_x : 16;
            unsigned range_z : 16;
            unsigned range_y : 16;
            unsigned range_x : 16;
        } gfx; // 10
        struct
        {
            unsigned id : 3;
            unsigned hit_group : 3;
            unsigned only_hit_grabbed_fighter : 1;
            unsigned bone : 8;
            unsigned use_common_bone_id : 1;
            unsigned dmg : 10;
            unsigned size : 16;
            unsigned offset_z : 16;
            unsigned offset_y : 16;
            unsigned offset_x : 16;
            unsigned angle : 9;
            unsigned kb_growth : 9;
            unsigned wdsk : 9;
            unsigned is_hit_items : 1;
            unsigned ignore_thrown_fighter : 1;
            unsigned ignore_fighter_scale : 1;
            unsigned clank_pri : 2;
            unsigned base_kb : 9;
            unsigned attribute : 5;
            unsigned shield_dmg : 8;
            unsigned hit_sfx_severity : 3; // weak, moderate, strong
            unsigned hit_sfx_kind : 5;     // none, punch, kick, sword, coin, bat, fan, elec, fire, yoshi chew, shell hit, energy, peach item, ice
            unsigned is_hit_grounded_fighter : 1;
            unsigned is_hit_aerial_fighter : 1;
        } hit; // 11
        struct
        {
            unsigned id : 3;
            unsigned dmg : 23;
        } hit_update_dmg; // 12
        struct
        {
            unsigned id : 3;
            unsigned size : 23;
        } hit_update_size; // 13
        struct
        {
            unsigned id : 26;
        } hit_clear; // 15
        struct
        {
            unsigned null : 26;
        } hit_clear_all; // 16
        struct
        {
            unsigned behavior : 8;
            unsigned unk : 18;
            unsigned id : 32;
            unsigned unk2 : 16; // padding?
            unsigned volume : 8;
            unsigned pan : 8;
        } sfx; // 17
        struct
        {
            unsigned kind : 26; // 0 = normal, 1 = invuln, 2 = intang
        } vuln;                 // 26
        struct
        {
            unsigned do_second : 1;
            unsigned mat_index1 : 7;
            unsigned mat_index2 : 7;
            unsigned mat_frame : 11;
        } eye; // 40
        struct
        {
            unsigned flag : 1;
            unsigned value1 : 12;
            unsigned value2 : 13;
        } rumble; // 43
        struct
        {
            unsigned id : 8;
            unsigned time : 18;
        } colanimapply; // 46
        struct
        {
            unsigned unk : 26;
        } ik; // 52
    } __attribute__((__packed__)) u;
} __attribute__((__packed__));
/*
struct FtScriptTimerSync
{
    unsigned opcode : 6;
    unsigned time : 26;
}; // 2
struct FtScriptTimerAsync
{
    unsigned opcode : 6;
    unsigned time : 26;
}; // 3
struct FtScriptGFX
{
    unsigned opcode : 6;
    unsigned bone : 8;
    unsigned use_common_bone_id : 1;
    unsigned destroy_on_state_change : 1;
    unsigned unk1 : 16;
    unsigned id : 16;
    unsigned unk2 : 16;
    unsigned offset_z : 16;
    unsigned offset_y : 16;
    unsigned offset_x : 16;
    unsigned range_z : 16;
    unsigned range_y : 16;
    unsigned range_x : 16;
}; // 10
struct FtScriptHit
{
    unsigned opcode : 6;
    unsigned id : 3;
    unsigned hit_group : 3;
    unsigned only_hit_grabbed_fighter : 1;
    unsigned bone : 8;
    unsigned use_common_bone_id : 1;
    unsigned dmg : 10;
    unsigned size : 16;
    unsigned offset_z : 16;
    unsigned offset_y : 16;
    unsigned offset_x : 16;
    unsigned angle : 9;
    unsigned kb_growth : 9;
    unsigned wdsk : 9;
    unsigned is_hit_items : 1;
    unsigned ignore_thrown_fighter : 1;
    unsigned ignore_fighter_scale : 1;
    unsigned clank_pri : 2;
    unsigned base_kb : 9;
    unsigned element : 5;
    unsigned shield_dmg : 8;
    unsigned hit_sfx_severity : 3; // weak, moderate, strong
    unsigned hit_sfx_kind : 5;     // none, punch, kick, sword, coin, bat, fan, elec, fire, yoshi chew, shell hit, energy, peach item, ice
    unsigned is_hit_grounded_fighter : 1;
    unsigned is_hit_aerial_fighter : 1;
}; // 11
struct FtScriptHitClear
{
    unsigned opcode : 6;
    unsigned null : 26;
}; // 16
struct FtScriptSFX
{
    unsigned opcode : 6;
    unsigned behavior : 8;
    unsigned unk : 18;
    unsigned id : 32;
    unsigned unk2 : 16; // padding?
    unsigned volume : 8;
    unsigned pan : 8;
}; // 17
struct FtScriptVuln
{
    unsigned opcode : 6;
    unsigned kind : 26; // 0 = normal, 1 = invuln, 2 = intang
};                      // 26
struct FtScriptEye
{
    unsigned opcode : 6;
    unsigned do_second : 1;
    unsigned mat_index1 : 7;
    unsigned mat_index2 : 7;
    unsigned mat_frame : 11;
}; // 40
struct FtScriptRumble
{
    unsigned opcode : 6;
    unsigned flag : 1;
    unsigned value1 : 12;
    unsigned value2 : 13;
}; // 43
struct FtScriptColAnimApply
{
    unsigned opcode : 6;
    unsigned id : 8;
    unsigned time : 18;
}; // 46
struct FtScriptIK
{
    unsigned opcode : 6;
    unsigned unk : 26;
}; // 52
*/

/** Static Variables **/
extern char mu_mx_ftPartsTable[] __asm__("ftPartsTable");
static ftCommonBone ***stc_ftbone = (void *)(mu_mx_ftPartsTable + 0);
extern char mu_mx_p_ftCommonData[] __asm__("p_ftCommonData");
static ftCommonData **stc_ftcommon = (void *)(mu_mx_p_ftCommonData + 0);
extern char mu_mx_Fighter_804D653C[] __asm__("Fighter_804D653C");
static ColAnimDesc **stc_plco_colanimdesc = (void *)(mu_mx_Fighter_804D653C + 0);
extern char mu_mx_Fighter_804D650C[] __asm__("Fighter_804D650C");
static GXColor **stc_shieldcolors = (void *)(mu_mx_Fighter_804D650C + 0);
extern char mu_mx_Fighter_804D6530[] __asm__("Fighter_804D6530");
static FtDmgVibrateDesc **stc_dmg_vibrate_desc = (void *)(mu_mx_Fighter_804D6530 + 0);
// static int *stc_ft_hitlog = R13_OFFSET(-0x5148); // used as semi-local variables remembering if a solid hit occured @ 8006cbc4
extern void *mu_tmce_ref_stc_ft_hitlog;
#define stc_ft_hitlog ((int *)((char *)mu_tmce_ref_stc_ft_hitlog + 0))
// static int *stc_ft_tiplog = R13_OFFSET(-0x5144); // used as semi-local variables remembering if a tip hit occured @ 8006cbc4
extern void *mu_tmce_ref_stc_ft_tiplog;
#define stc_ft_tiplog ((int *)((char *)mu_tmce_ref_stc_ft_tiplog + 0))

/*** Functions ***/
GOBJ *Fighter_Create(PlayerData *pd);
GOBJ *Fighter_Create2(PlayerData *pd);
void Fighter_EnterAerial(GOBJ *fighter, int aerialState);
void ActionStateChange(float startFrame, float animSpeed, float animBlend, GOBJ *fighter, int stateID, int flags1, GOBJ *alt_state_source);
void FrameSpeedChange(GOBJ *f, float speed);
void Fighter_UpdateAnim(GOBJ *f);      // 8006a360
void Fighter_UpdateIASA(GOBJ *f);      // 8006ad10
void Fighter_UpdatePhys(GOBJ *f);      // 8006b82c
void Fighter_UpdateEnvColl(GOBJ *f);   // 8006c27c
void Fighter_UpdateAccessory(GOBJ *f); // 8006c624
void Fighter_UpdateGFX(GOBJ *f);       // 8006c80c
void Fighter_UpdateAllHitboxPos(GOBJ *f);
void Fighter_UpdateBonePos(FighterData *fighter_data, int unk);
void Fighter_SubactionFastForward(GOBJ *fighter);
FtAction *Fighter_GetFtAction(FighterData *fighter, int action_id); // returns the desired ft action entry stored in the dat file
Figatree *Fighter_GetAnimData(FighterData *fighter, int action_id); // this will request the anim data from the AJ file in ARAM and overwrite the current animation!
float Fighter_GetAnimLength(Figatree *ft_anim);
void Fighter_EnterLightThrow(GOBJ *fighter, int stateID);
void Fighter_EnterDamageFall(GOBJ *fighter);
void Fighter_EnterWait(GOBJ *fighter);
void Fighter_EnterAirCatch(GOBJ *fighter);
void Fighter_EnterFall(GOBJ *fighter);
void Fighter_EnterFallAerial(GOBJ *fighter);
void Fighter_EnterSpecialFall(GOBJ *fighter, int can_fastfall, int no_soft_landing, int can_interrupt_landing, float air_drift_multiplier, float landing_frames);
void Fighter_EnterLanding(GOBJ *fighter);
void Fighter_EnterSpecialLanding(GOBJ *fighter, int unk, float state_length);
void Fighter_EnterSleep(GOBJ *fighter, int ms);
void Fighter_EnterEntry(GOBJ *fighter);
void Fighter_EnterDownBound(GOBJ *f);
void Fighter_EnterDownWait(GOBJ *f);
void Fighter_EnterJumpAerial(GOBJ *f);
void Fighter_EnterDeadDown(GOBJ *f);
void Fighter_EnterDeadUp(GOBJ *f);
void Fighter_EnterDeadLeft(GOBJ *f);
void Fighter_EnterDeadRight(GOBJ *f);
int Fighter_CheckNearbyLedges(GOBJ *fighter);
int Fighter_CheckForOtherFighterOnLedge(GOBJ *fighter);
void Fighter_EnterCliffCatch(GOBJ *fighter);
void Fighter_EnterCliffWait(GOBJ *fighter);
void Fighter_EnterCliffJumpSlow2(GOBJ *fighter, float y_velocity);
void Fighter_EnterRebirth(GOBJ *fighter);
void Fighter_EnterRebirthWait(GOBJ *fighter);
void Fighter_UpdateRebirthPlatformPos(GOBJ *fighter);
void Fighter_MoveToCliff(GOBJ *fighter);
GOBJ *Fighter_GetGObj(int ply);
GOBJ *Fighter_GetSubcharGObj(int ply, int ms);
Playerblock *Fighter_GetPlayerblock(int ply);
void Fighter_Playerblock_UpdateDamage(int ply, int ms, int percent);
void Fighter_Playerblock_Init(int ply);
void Fighter_Playerblock_ResetStaleMoves(int ply);
void Fighter_SetSlotType(int ply, int slot);
int Fighter_GetControllerPort(int ply);
int Fighter_GetTeam(int ply);
int *Fighter_GetStaleMoveTable(int ply);
void Fighter_SetInitialPosition(int ply, Vec3 *pos);
void Fighter_SetPosition(int ply, int ms, Vec3 *pos);
void Fighter_GetPosition(int ply, Vec3 *pos);
void Fighter_SetDirection(int ply, float dir);
float Fighter_GetDirection(int ply);
void Fighter_ApplyIntang(GOBJ *fighter, int duration);
int Fighter_GetSlotType(int index); // returns 0x0 for HMN, 0x1 for CPU, 0x2 for Demo, 0x3 for not present
int Fighter_GetStocks(int ply);
void Fighter_SetStocks(int ply, int stocks);
void Fighter_LoseStock(int ply);
void Fighter_DeathLogic(GOBJ *f);
void Fighter_RunOnDeathCallbacks(GOBJ *f);
int Fighter_GetStaminaHP(int ply);
void Fighter_SetStaminaHP(int ply, int hp);
int Fighter_CheckStaminaMode(int ply);
void Fighter_SetStaminaMode(int ply, int is_stamina);
void Fighter_SetFallNum(int index, int ms, int falls);
int Fighter_GetFallNum(int index, int ms);
void Fighter_EnableCollUpdate(FighterData *fighter);
void Fighter_EnterDamageState(GOBJ *fighter, int stateID, float new_facing_dir); // new_facing_dir = 0 to use current
s8 Fighter_BoneLookup(FighterData *fighter, int boneID);
void Fighter_GiveDamage(FighterData *fighter, float damage);
void Fighter_GiveHeal(FighterData *fighter, int heal);
float Fighter_StaleDamage(FighterData *fighter, float dmg, int atk_kind, int atk_instance);
void Fighter_SetHUDDamage(int player, u16 damage);
void Fighter_RunOnHitCallbacks(GOBJ *fighter);
void Fighter_ExitHitlag(GOBJ *fighter);
int FrameTimerCheck(GOBJ *fighter);
void Fighter_EnterMiscPassState(float start_frame, GOBJ *fighter, int state, int flags);
int Fighter_CollGround_PassLedge(GOBJ *fighter);
void Fighter_CollGround_PassLedgeCB(GOBJ *fighter, void *callback);
int Fighter_CollGround_StopLedge(GOBJ *fighter); // returns is_grounded
void Fighter_CollGround_StopLedge_EnterFall(GOBJ *fighter);
int Fighter_CollAir_GrabFacingLedgeWalljump(GOBJ *fighter, void *perFrame, void *onLand); // this will handle entering cliffcatch / walljump. all in one collision func
int Fighter_CollAir_GrabBothLedgesWalljump(GOBJ *fighter, void *onLand);                  // this will handle entering cliffcatch / walljump. all in one collision func
int Fighter_CollAir_CheckLedge(GOBJ *fighter, int grab_direction);                        // this will only check for ledges, you still need to call the cliffcatch/walljump IASA function after
void Fighter_CollAir_IgnoreLedge(GOBJ *fighter, void *callback);
int Fighter_CollAir_IgnoreLedge_NoCB(GOBJ *fighter);
int Fighter_CollAir_SoftLanding(GOBJ *fighter);
int Fighter_CollAir_DefineECB(GOBJ *fighter, ECBSize *ecb);
int Fighter_Coll_DamageState(GOBJ *fighter);
int Fighter_Coll_CheckToPass(GOBJ *fighter, int floor_type); // usually used as a callback, pass = fall through platform
int Fighter_IASACheck_CliffCatch(GOBJ *fighter);
int Fighter_IASACheck_WallJump(GOBJ *fighter);
int Fighter_IASACheck_TechRoll(GOBJ *fighter);
int Fighter_IASACheck_TechInPlace(GOBJ *fighter);
int Fighter_IASACheck_StaminaDead(GOBJ *fighter);
int Fighter_IASACheck_WallTechJump(GOBJ *fighter);
int Fighter_IASACheck_CeilingTech(GOBJ *fighter);
int Fighter_IASACheck_WallCeilingReflect(GOBJ *fighter);
int Fighter_IASACheck_JumpAerial(GOBJ *fighter);
int Fighter_IASACheck_JumpF(GOBJ *fighter);
int Fighter_IASACheck_PassConditions(GOBJ *fighter);
int Fighter_IASACheck_Turn(GOBJ *fighter);
int Fighter_IASACheck_AllGrounded(GOBJ *fighter);
int Fighter_IASACheck_AllAerial(GOBJ *fighter);
void Fighter_PhysGround_ApplyFriction(GOBJ *fighter);
void Fighter_PhysGround_ApplyCustomFriction(FighterData *fighter, float friction);
void Fighter_PhysGround_ApplyVelocity(GOBJ *fighter);
void Fighter_PhysAir_CheckFastfall(FighterData *fighter);
void Fighter_PhysAir_ApplyGravityDecayX(GOBJ *);
void Fighter_PhysAir_ApplyGravityFastfall(GOBJ *);
void Fighter_PhysAir_ApplyGravity(FighterData *fighter, float gravity, float limit);
void Fighter_PhysAir_ApplyAerialDrift(FighterData *fighter);
void Fighter_PhysAir_ApplyCustomAerialDrift(FighterData *fighter, float unk, float speed, float limit);
void Fighter_PhysAir_SetAerialDrift(FighterData *fp, float curr_x_vel, float this_frame_drift, float x_vel_limit, float aerial_friction); // this func name sucks but i just need to link it lol
void Fighter_PhysAir_DecayXVelocity(FighterData *fighter, float aerial_friction);
void Fighter_PhysAir_LimitXVelocity(FighterData *fighter);
void Fighter_Phys_UseAnimYVelocity(GOBJ *fighter);
void Fighter_Phys_UseAnimPos(GOBJ *fighter);
void Fighter_Phys_UseAnimPosAndStick(GOBJ *fighter);
void Fighter_SetGrounded(FighterData *fighter);
void Fighter_SetGrounded2(FighterData *fighter);
void Fighter_SetAirborne(FighterData *fighter);             // locks ecb for 10 frames
void Fighter_SetAirborneNoJumps(FighterData *fighter_data); // locks ecb for 5 frames
void Fighter_LoseGroundJump(FighterData *fighter_data);
void Fighter_KillAllVelocity(GOBJ *fighter);
void Fighter_AdvanceScript(GOBJ *fighter);
void Fighter_GFXRemoveAll(GOBJ *fighter);
void Fighter_EnableReflectUpdate(GOBJ *fighter);
void Fighter_CreateReflect(GOBJ *fighter, ReflectDesc *reflect, void *cb_OnReflectHit);
float Fighter_GetBoneRotX(FighterData *fighter, int bone);
float Fighter_GetBoneRotY(FighterData *fighter, int bone);
void Fighter_SetBoneRotX(FighterData *fighter, int bone, float angle);
void Fighter_SetBoneRotY(FighterData *fighter, int bone, float angle);
void Fighter_SetBoneRotZ(FighterData *fighter, int bone, float angle);
void Fighter_PlayPositionalSFX(FighterData *fp, int sfxID, int volume, int balance);
void Fighter_PlayVoiceSFX(FighterData *fighter, int sfxID, int volume, int balance);
void Fighter_PlayVoiceSFX2(FighterData *fighter, int sfxID, int volume, int balance);
void Fighter_DestroyVoiceSFX(FighterData *fighter);
void Fighter_ColAnim_Apply(FighterData *fighter_data, int colanim_kind, int unk); // will apply the color_anim specified
void Fighter_ColAnim_Remove(FighterData *fighter_data, int colanim_kind);         // will remove the color_anim specified
void Fighter_ColAnim_Update(GOBJ *fighter);                                       // fighter gobj callback, will step colanim logic forward one frame. (you probably dont need to call this!)
void Fighter_DisableBlend(GOBJ *fighter, int animd_id);
void Fighter_UpdateDynamics(GOBJ *fighter, u16 *dynamic_struct);
void Fighter_ZeroCPUInputs(FighterData *fighter_data);
void Fighter_CreateShieldGFX(GOBJ *fighter);
void Fighter_UpdateShieldGFX(GOBJ *fighter, float size);
int Fighter_GetShieldColorIndex(int ply);
int Fighter_GetExternalID(int ply);
int Fighter_GetCostumeID(int ply);
int Fighter_GetHandicap(int ply);
float Fighter_GetBaseScale(FighterData *fighter);
void Fighter_SetScale(GOBJ *fighter, float scale);
void Fighter_ProcDynamics(GOBJ *fighter);
void Fighter_CheckToEnableDynamics(FighterData *fp, u16 *dynamics_data);
void Fighter_FreeAllDynamics(FighterData *fighter_data);
float Fighter_GetKnockbackAngle(FighterData *fighter_data);
void Fighter_UpdateCameraBox(GOBJ *fighter);
void Fighter_SetAllHurtboxesNotUpdated(GOBJ *fighter);
void Fighter_UpdateHurtboxes(FighterData *fighter_data);
void Fighter_UpdateIK(GOBJ *fighter);
void Fighter_CPUInitialize(FighterData *fighter_data, int cpu_kind, int cpu_level, int unk);
int Fighter_GetCPUKind(int ply);
int Fighter_SetCPUKind(int ply, int cpu_kind);
int Fighter_GetCPULevel(int ply);
int Fighter_SetCPULevel(int ply, int cpu_level);
char *Fighter_GetName(int external_id);
void Fighter_InitPObj();
void Fighter_InitPObj2();
void Fighter_IndexFtPartsDObjs(GOBJ *fighter, JOBJ *copy_model, FtParts *ftparts); // inits the dobj array in ftpartsmodel
void Fighter_InitFtPartsModel(FtPartsDesc *ftpartsdesc, FtPartsVis *unk, int index, FtParts *ftparts, FtParts *ftparts2);
int Fighter_CheckUnlocked(int ext_id);
void Fighter_UpdateDObjFlags(void *ftparts1, int r4, void *bone_info);
void Fighter_UpdateDObjFlags2(void *ftparts1, int r4, void *bone_info);
void Fighter_UpdateOnscreenBool(GOBJ *fighter);
void Fighter_SetFacingToStickDirection(FighterData *fighter_data);
int Fighter_CheckToIgnorePlatform(GOBJ *fighter);
int Hitbox_CheckIfPreviouslyHit(void *victim_data, ftHit *hitbox);
void Hitbox_SetAsPreviouslyHit(ftHit *hitbox, int unk, void *victim_data);
void Fighter_HitboxDisableAll(GOBJ *fighter);
int Fighter_CountPlayers();
void Fighter_InitData(GOBJ *f);
void Fighter_InitInputs(GOBJ *fighter);
void Fighter_InitInputTimers(GOBJ *fighter);
void Fighter_BreakGrabUnk(GOBJ *victim);
void Fighter_BreakGrab(GOBJ *fighter, GOBJ *victim);
void Fighter_InitGrab(FighterData *fighter, int is_enable, void *on_grabber, void *on_item, void *on_victim);
void Fighter_SetCharacterFlags(GOBJ *fighter, int, int);
void Fighter_GetECBPosition(GOBJ *fighter, Vec3 *position);
void Fighter_Phys_AnimationFriction(GOBJ *fighter);
void Fighter_CollAir_IgnoreGround(GOBJ *fighter);
void Fighter_SetFacingToStickDirection(FighterData *fighter_data);
void Fighter_ClampHorizontalVelocity(FighterData *fighter_data, float max_vel);
void Fighter_ClampFallSpeed(FighterData *fighter_data, float max_vel);
void Fighter_QueueAllowXDrift(FighterData *fighter_data, float unk, float accel, float max_vel);
void Fighter_AllowXDrift(FighterData *fighter_data, float unk, float accel, float max_vel);
void Fighter_AddClampYPosition(FighterData *fighter_data, float amt, float max);
void Fighter_ClampHorizontalGroundVelocity(FighterData *, float);
void Fighter_Phys_ApplyVerticalAirFriction(FighterData *fighter_data);
void Fighter_GetVisGroupDefault(GOBJ *fighter, int vis_group);
void Fighter_SetVisGroupDefault(GOBJ *fighter, int vis_group, s8 index); // sets the default value for this vis group (-1 = hide all)
void Fighter_SetVisGroupCurrent(GOBJ *fighter, int vis_group, s8 index); // sets the current active value for this vis group (-1 = hide all)
void Fighter_RevertAllVisGroups(GOBJ *fighter);                          // sets all vis groups to their default values (specified by Fighter_SetVisGroupDefault)
void Fighter_HideAllVisGroups(GOBJ *fighter);                            // hides all dobjs in all vis group (sets default and current to -1)
int Fighter_CheckVisible(GOBJ *fighter);
void Fighter_GiveItem(GOBJ *fighter, GOBJ *item);
void Fighter_ReleaseItemUnk(int ply, int ms, GOBJ *item);
void Fighter_InitDamageVibrate(FighterData *fp, int dmg_attr, int dmg, float mult, int pre_hurt_state, int air_state);
float Fighter_CalcHitlagFrames(int dmg, int state_id, float mult);
void Fighter_LoadAnimation(FighterData *fp, FighterData *fp_source, int anim_id);
void Fighter_ApplyAnimation(GOBJ *f, float start_frame, float speed, float blend);
void Fighter_UpdateStateFrameInfo(GOBJ *f);
void Fighter_ScriptUpdate(GOBJ *f);
void Fighter_ScriptFastForward(GOBJ *f);
void Fighter_DropCrate(GOBJ *f);
void Fighter_GrabBreakCheck(GOBJ *f);
void Fighter_ThrownAttach(FighterData *anchor_data, FighterData *attachee_data);
void Fighter_ThrownRelease_UpdateUnk(GOBJ *thrower, GOBJ *victim);
void Fighter_ThrownRelease_NoUpdateUnk(GOBJ *thrower, GOBJ *victim);
void Fighter_ThrownRelease(GOBJ *thrower, GOBJ *victim, int is_update_unk);
void Fighter_ThrownApplyKnockback(GOBJ *victim, GOBJ *hit_exception, int is_enter_dmgflytop);
void Fighter_ThrownApplyKnockbackNoTDI(GOBJ *victim, float frame);
void Fighter_AddStaleIncCombo(GOBJ *thrower, GOBJ *victim, float dmg); // 8007891c
void Fighter_SetAllHurtboxState(GOBJ *f, int state);                   // 8007b0c0
void Fighter_SetHurtboxState(GOBJ *f, int bone_index, int state);      // 8007b128
void Fighter_SetScriptHurtStatus(GOBJ *f, int state);                  // 8007b62c
int Fighter_GetIntangibleFrames(GOBJ *f);                              // 8007b868
void Fighter_IncPercent(GOBJ *f, float *dmg);                          // 80076640
float Fighter_KnockbackCalculate(float match_dmg_ratio, float ft_attk_ratio, float ft_def_ratio, float unk, FighterData *fp, ftHit *hit, int dmg);
float Fighter_GetAttackRatio(int ply);                                            // 800338f4
float Fighter_GetDefenseRatio(int ply);                                           // 800339e0
void Fighter_SetDamageSource(GOBJ *attacker, GOBJ *victim, float *dmg_direction); // 80078710
void Fighter_SetDamageSourceGrab(GOBJ *attacker, GOBJ *victim, int unk);          // 80078754
void Fighter_DamageRumble(FighterData *fp, int dmg);                              // 8007ee0c
void Fighter_RumbleExecute(FighterData *fp, int strength, int unk);
void Fighter_CheckKnockbackModifiers(FighterData *fp); // 8008d930
int Fighter_GetCurrentPlacing(int ply);
void Fighter_StoreGrabBreakout(FighterData *fp, int flag, float amt);
int Fighter_CheckGrabBreakout(FighterData *fp, float mash_amt); // returns 1 if inputted something
void Fighter_SetAnimRate(GOBJ *f, float rate);
int Fighter_CheckJumpInput(GOBJ *f);
void Fighter_SetEyeTexture(GOBJ *f, int material_index, float frame);
void Fighter_GetECBCenter(GOBJ *f, Vec3 *center_pos);
void Fighter_ApplyPartAnim(GOBJ *f, int part_id, int anim_id);
void Fighter_SetHoldKind(GOBJ *f, int r4, int r5);
void Fighter_ApplyHandAnim(GOBJ *f, int r4);
void Fighter_CheckToRespawn(int ply, int ms);
void Fighter_Respawn(GOBJ *f, int ms);
void Fighter_CreateAbsorb(GOBJ *fighter_gobj, AbsorbDesc *absorb_desc);
void Fighter_EnableAbsorbUpdate(GOBJ *fighter_gobj);
void Fighter_PlaySFX(FighterData *fp, int sfxid, int volume, int pitch);
void SFX_StopAllFighterSFX(FighterData *fighter_data);
void Fighter_EnterFallOrWait(GOBJ *fighter_gobj);
void Fighter_EnterTech(GOBJ *gobj);
int Fighter_CheckTechInput(GOBJ *f);
void Fighter_EnterSpecialFallLoseJumps(GOBJ *fighter_gobj, int can_fastfall, int can_not_noimpactland, int can_not_interrupt, float aerial_drift_mult, float landing_lag, float blend);
void Fighter_RumbleController(GOBJ *f, int unk1, int unk2);
void Fighter_GetLeftStick(GOBJ *fighter_gobj, float *stick_x, float *stick_y);
void Fighter_ClampMaxAirDrift(FighterData *fighter_gobj);
void Fighter_UpdateHitboxDamage(ftHit *hit, int dmg, GOBJ *f);
void Fighter_GetCollisionSlope(GOBJ *fighter, Vec3 *position);
void Fighter_ThrowVictim(GOBJ *f, GOBJ *victim, int param_3);
void Fighter_StoreAccessoryJObj(FighterData *fd, JOBJDesc *jobj_desc);
void Fighter_ResetModelScale(GOBJ *fighter);
void Fighter_AddPlayerException(FighterData *fd, GOBJ *target);
void Fighter_EnterCaptureCut(GOBJ *fighter);
void Fighter_EnterCatchCut(GOBJ *fighter, int victim_enter_capturecut);
void Fighter_SetGrabbableFlag(GOBJ *fighter, int);
float Fighter_GetDistanceFromPointSquared(GOBJ *fighter, Vec3 *position);
int Fighter_CheckNotWalkInput(GOBJ *fighter);
int Fighter_CheckWalkInput(GOBJ *fighter);
void GXLink_Fighter(GOBJ *f, int pass);
void Fighter_MultiJump_TurnThink(FighterData *fp, int turn_frames);
int Fighter_CheckFootstool(GOBJ *f);
float Fighter_GetSoftLandVelocity(FighterData *fp);
void Fighter_Transform(GOBJ *f, void *EnterStateCallback);
void Fighter_InitCameraBox(FighterData *fp);                // 80076064
void Fighter_SetSelfDamageSource(GOBJ *f);                  // 800788d4
float Fighter_CalcForceApplied(FighterData *fp, void *unk); // 80079ea8
void Fighter_UpdateModelShift(GOBJ *f);                     // updates the offsets of the model during hitlag and smash charge
void Fighter_GivePersistentIntangibility(GOBJ *f, int frames);
void Fighter_TDI(FighterData *fp);
void Fighter_PlayQueuedDamageSounds(FighterData *fp);
GXColor Fighter_GetPlyHUDColor(int ply); // used for lupe, pokemon stadium text color, results viewport broder
void FSmash_GetASForAnalogAngle(GOBJ *ft);
Mtx *Fighter_GetMtxPtr(FighterData *fp);

#endif
