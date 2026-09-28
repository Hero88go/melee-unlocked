#ifndef MEX_H_COLOR
#define MEX_H_COLOR

#include "gx.h"
#include "structs.h"
#include "datatypes.h"

/*** Structs ***/

struct __attribute__((scalar_storage_order("big-endian"))) ColAnimDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int cmd_data;
    unsigned char priority;
    unsigned char x5;
};
struct ColorOverlay /* native twin, generated */
{
    union {
        char _mex_native_size[160];
        struct { int timer; };
        struct { char _p1582[4]; int pri; };
        struct { char _p1583[8]; void *ptr1; };
        struct { char _p1584[16]; int loop; };
        struct { char _p1585[24]; void *ptr2; };
        struct { char _p1586[32]; int x14; };
        struct { char _p1587[40]; void *alloc; };
        struct { char _p1588[48]; int x1c; };
        struct { char _p1589[52]; int x20; };
        struct { char _p1590[56]; int x24; };
        struct { char _p1591[64]; int colanim; };
        struct { char _p1592[72]; GXColor hex; };
        struct { char _p1593[76]; float color_red; };
        struct { char _p1594[80]; float color_green; };
        struct { char _p1595[84]; float color_blue; };
        struct { char _p1596[88]; float color_alpha; };
        struct { char _p1597[92]; float colorblend_red; };
        struct { char _p1598[96]; float colorblend_green; };
        struct { char _p1599[100]; float colorblend_blue; };
        struct { char _p1600[104]; float colorblend_alpha; };
        struct { char _p1601[108]; GXColor light_color; };
        struct { char _p1602[112]; float light_red; };
        struct { char _p1603[116]; float light_green; };
        struct { char _p1604[120]; float light_blue; };
        struct { char _p1605[124]; float light_alpha; };
        struct { char _p1606[128]; float lightblend_red; };
        struct { char _p1607[132]; float lightblend_green; };
        struct { char _p1608[136]; float lightblend_blue; };
        struct { char _p1609[140]; float lightblend_alpha; };
        struct { char _p1610[144]; float light_angle; };
        struct { char _p1611[148]; float light_unk; };
        struct { char _p1612[152]; unsigned char color_enable : 1; };
        struct { char _p1613[152]; unsigned char : 1; unsigned char flag2 : 1; };
        struct { char _p1614[152]; unsigned char : 2; unsigned char light_enable : 1; };
        struct { char _p1615[152]; unsigned char : 3; unsigned char flag4 : 1; };
        struct { char _p1616[152]; unsigned char : 4; unsigned char flag5 : 1; };
        struct { char _p1617[152]; unsigned char : 5; unsigned char flag6 : 1; };
        struct { char _p1618[152]; unsigned char : 6; unsigned char flag7 : 1; };
        struct { char _p1619[152]; unsigned char : 7; unsigned char flag8 : 1; };
    };
};

void ColAnim_Apply(ColorOverlay *col, void *colanim_data, int colanim_index, int r6);
void ColAnim_Disable(ColorOverlay *col);

#endif