#ifndef ROGUE_NATIVE_INTRO_H
#define ROGUE_NATIVE_INTRO_H
/* Byte layout is the native gmClassicIntroData input, not a saved pointer. */
typedef struct RogueNativeIntroData {
    int model_kind, game_type;
    unsigned char port, nametag, map_stop, left_count, right_count;
    unsigned char left[3], right[3], left_costume[3], right_costume[3];
    unsigned char left_flags[3], right_flags[3], padding;
} RogueNativeIntroData;
extern RogueNativeIntroData rogue_native_intro;
int RogueNativeIntro_Prepare(unsigned port);
int RogueNativeIntro_Held(void);
void RogueNativeIntro_Frame(void);
void RogueNativeIntro_Standby(void* root);
void RogueNativeIntro_End(void);
void RogueNativeIntro_Labels(void);
void RogueNativeIntro_Focus(unsigned side);
void RogueNativeIntro_KitChanged(unsigned side);
struct HSD_GObj;
void RogueNativeIntro_BackdropDraw(struct HSD_GObj* object, __INTPTR_TYPE__ pass);
void RogueNativeIntro_ModelFocus(struct HSD_GObj* object);
void RogueNativeIntro_NameFocus(void* text, unsigned index);
unsigned RogueNativeIntro_PlayerCharacter(void);
void RogueNativeIntro_Commit(unsigned side);
/* Implemented at the native scene boundary with access to its layout data. */
void RogueNativeIntro_ReplaceSide(unsigned side, unsigned character, unsigned costume);
void RogueNativeIntro_ReleaseTimeline(void);
void RogueNativeIntro_Map(void* bases, void* icons);
float RogueNativeIntro_GroupY(unsigned count, unsigned index, float native_y);
#endif
