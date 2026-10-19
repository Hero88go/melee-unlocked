#ifndef MU_FICHINA_CODEC_H
#define MU_FICHINA_CODEC_H
#include "../common/mu_gr_common.h"
typedef struct FcDialogue { int portrait, texture, sound, text; } FcDialogue;
typedef struct FcCodecPlayer { int used; const FcDialogue* sequence; int index; } FcCodecPlayer;
FcCodecPlayer* fc_codec_player(void);
int fc_bank(void);
int fc_sound(int id, int volume, int pan);
void fc_codec_create(Ground_GObj* gobj);
void fc_codec_frame(Ground_GObj* gobj);
void fc_codec_draw(HSD_GObj* gobj, intptr_t pass);
void fc_codec_check_spawn(FcCodecPlayer* player);
#endif
