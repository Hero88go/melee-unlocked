#ifndef MEX_H_DEVTEXT
#define MEX_H_DEVTEXT

#include "structs.h"
#include "gx.h"

/*** Structs ***/

struct DevText /* native twin, generated */
{
    union {
        char _mex_native_size[64];
        struct { int x0; };
        struct { char _p1666[4]; u8 width; };
        struct { char _p1667[5]; u8 height; };
        struct { char _p1668[6]; u8 cursor_x; };
        struct { char _p1669[7]; u8 cursor_y; };
        struct { char _p1670[8]; Vec2 scale; };
        struct { char _p1671[16]; GXColor bg_color; };
        struct { char _p1672[20]; int x14; };
        struct { char _p1673[24]; int x18; };
        struct { char _p1674[28]; int x1c; };
        struct { char _p1675[32]; int x20; };
        struct { char _p1676[36]; char x24; };
        struct { char _p1677[37]; char x25; };
        struct { char _p1678[38]; char : 7; char show_text : 1; };
        struct { char _p1679[38]; char : 6; char show_background : 1; };
        struct { char _p1680[38]; char : 5; char x26_20 : 1; };
        struct { char _p1681[38]; char : 4; char show_cursor : 1; };
        struct { char _p1682[40]; u8 *text_data; };
        struct { char _p1683[56]; DevText *next; };
    };
};

/*** Functions ***/

DevText *DevelopText_CreateDataTable(int unk1, int x, int y, int width, int height, void *alloc);
void DevelopText_Activate(void *unk, DevText *text);
void DevelopText_Deactivate(void *unk);
void DevelopText_AddString(DevText *text, ...);
void DevelopText_EraseAllText(DevText *text);
void DevelopText_ResetCursorXY(DevText *text, int x, int y);
void DevelopText_StoreTextColor(DevText *text, GXColor *RGBA);
void DevelopText_StoreBGColor(DevText *text, GXColor *RGBA);
void DevelopText_ShowText(DevText *text);
void DevelopText_HideText(DevText *text);
void DevelopText_ShowBG(DevText *text);
void DevelopText_HideBG(DevText *text);
void DevelopText_StoreTextScale(DevText *text, float x, float y);
void Develop_DrawSphere(float size, Vec3 *pos1, Vec3 *pos2, GXColor *diffuse, GXColor *ambient);
void Develop_UpdateMatchHotkeys();

extern char mu_mx_DbLevel[] __asm__("DbLevel");
static int *stc_dblevel = (void *)(mu_mx_DbLevel + 0);

#endif
