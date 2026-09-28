#ifndef MEX_H_TEXT
#define MEX_H_TEXT

#include "structs.h"
#include "datatypes.h"
#include "obj.h"
#include "gx.h"

/*** Structs ***/

struct SISData
{
    u8 *image_data_arr;   // array of I4 image data for characters, stride is (32*32) / 2
    u8 *kerning_data_arr; // array of kerning data for characters, stride is 0x2, kerning is at 0x0
};

struct TextCanvas
{
    TextCanvas *next; // 0x0
    GOBJ *cam_gobj;   // 0x4,
    u16 size;         // 0x8 data remaining after this alloc?
    u16 sis_idx;      // 0xa sis group this canvas belongs to
    // data after this....
    u8 p_link;  // 0xC
    u8 xd;      // 0xD
    u8 gx_link; // 0xE
    u8 gx_pri;  // 0xF
};

struct Text /* native twin, generated */
{
    union {
        char _mex_native_size[192];
        struct { Vec3 trans; };
        struct { char _p1620[12]; Vec2 aspect; };
        struct { char _p1621[20]; float scissor_top; };
        struct { char _p1622[24]; float scissor_bot; };
        struct { char _p1623[28]; float scissor_left; };
        struct { char _p1624[32]; float scissor_right; };
        struct { char _p1625[36]; Vec2 viewport_scale; };
        struct { char _p1626[44]; GXColor viewport_color; };
        struct { char _p1627[48]; GXColor color; };
        struct { char _p1628[52]; Vec2 scale; };
        struct { char _p1629[60]; float x3c; };
        struct { char _p1630[64]; float x40; };
        struct { char _p1631[68]; u16 x44; };
        struct { char _p1632[70]; u16 x46; };
        struct { char _p1633[72]; u8 use_aspect; };
        struct { char _p1634[73]; u8 kerning; };
        struct { char _p1635[74]; u8 align; };
        struct { char _p1636[75]; u8 x4b; };
        struct { char _p1637[76]; u8 is_depth_compare; };
        struct { char _p1638[77]; u8 hidden; };
        struct { char _p1639[78]; u8 is_scissor; };
        struct { char _p1640[79]; u8 sis_id; };
        struct { char _p1641[80]; void *x50; };
        struct { char _p1642[88]; GOBJ *gobj; };
        struct { char _p1643[96]; void (*render_callback)(GOBJ *text_gobj); };
        struct { char _p1644[104]; u8 *text_start; };
        struct { char _p1645[112]; u8 *text_end; };
        struct { char _p1646[120]; TextCanvas *allocInfo; };
        struct { char _p1647[128]; void *x68; };
        struct { char _p1648[136]; u16 x6c; };
        struct { char _p1649[138]; u16 x6e; };
        struct { char _p1650[140]; float x70; };
        struct { char _p1651[144]; float x74; };
        struct { char _p1652[148]; float x78; };
        struct { char _p1653[152]; float x7c; };
        struct { char _p1654[156]; float x80; };
        struct { char _p1655[160]; float x84; };
        struct { char _p1656[164]; float x88; };
        struct { char _p1657[168]; GXColor x8c; };
        struct { char _p1658[172]; u16 x90; };
        struct { char _p1659[174]; u16 x92; };
        struct { char _p1660[176]; int x94; };
        struct { char _p1661[180]; int char_display_num; };
        struct { char _p1662[184]; u8 is_fit; };
        struct { char _p1663[185]; u8 x9d; };
        struct { char _p1664[186]; u8 x9e; };
        struct { char _p1665[187]; u8 x9f; };
    };
};

/*** Functions ***/

int Text_CreateCanvas(int sis_id, GOBJ *cam_gobj, int gobj_entityclass, int gobj_plink, int gobj_ppriority, int gxlink, int gxpri, int cobj_gxpri); // the optional gobj and cobj_gxlink are used to create a cobj as well. set gobj
Text *Text_CreateText(int sis_id, int canvasID);
Text *Text_CreateText2(int sis_id, int canvasID, float pos_x, float pos_y, float pos_z, float limit_x, float limit_y);
void Text_Destroy(Text *text);
int Text_AddSubtext(Text *text, float xPos, float yPos, char *string, ...);
void Text_SetScale(Text *text, int subtext, float x, float y);
void Text_SetColor(Text *text, int subtext, GXColor *color);
void Text_SetPosition(Text *text, int subtext, float x, float y);
void Text_SetText(Text *text, int subtext, const char *string, ...);
u8 *Text_Alloc(int size);
void Text_DestroyAlloc(u8 *alloc);
void Text_DestroyAllAlloc(Text *text);
int Text_ConvertToMenuText(char *out, char *in);
void Text_GX(GOBJ *gobj, int pass);
void Text_LoadSdFile(int index, char *filename, char *symbol);
void Text_SetSisText(Text *text, int text_index);
void Text_DestroyAllSisCanvas(int sis_id);
void Text_DestroyCanvas(TextCanvas *);
void Text_InitSisHeap();

/*** Variables ***/
// Text data
static int *stc_textheap_size = (int *)(R13_OFFSET(-0x3d38));
static TextCanvas **stc_textheap_start = (TextCanvas **)(R13_OFFSET(-0x3d34));
static TextCanvas **stc_textheap_next = (TextCanvas **)(R13_OFFSET(-0x3d30));
static TextCanvas **stc_textheap_first = (TextCanvas **)(R13_OFFSET(-0x3d2c));

// Text object
static Text **stc_text_first = (Text **)(R13_OFFSET(-0x3d28));

// Text canvas
static TextCanvas **stc_textcanvas_first = (TextCanvas **)(R13_OFFSET(-0x3d24));

// Sis Library
// static HSD_Archive **stc_sis_archives = (HSD_Archive **)0x804d1110; // array of sis file archive pointers
extern void *mu_tmce_ref_stc_sis_archives;
#define stc_sis_archives ((HSD_Archive * *)((char *)mu_tmce_ref_stc_sis_archives + 0))
extern char mu_mx_HSD_SisLib_804D1124[] __asm__("HSD_SisLib_804D1124");
static SISData **stc_sis_data = (void *)(mu_mx_HSD_SisLib_804D1124 + 0);                 // array of currently loaded sis data, indexed by sis_id

#endif
