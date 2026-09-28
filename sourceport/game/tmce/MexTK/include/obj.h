#ifndef MEX_H_OBJ
#define MEX_H_OBJ

#include "structs.h"
#include "datatypes.h"
#include "gx.h"

// JObj Flags
#define JOBJ_SKELETON (1 << 0)             // 0x00000001
#define JOBJ_SKELETON_ROOT (1 << 1)        // 0x00000002
#define JOBJ_ENVELOPE_MODEL (1 << 2)       // 0x00000004
#define JOBJ_CLASSICAL_SCALING (1 << 3)    // 0x00000008
#define JOBJ_HIDDEN (1 << 4)               // 0x00000010
#define JOBJ_PTCL (1 << 5)                 // 0x00000020
#define JOBJ_MTX_DIRTY (1 << 6)            // 0x00000040
#define JOBJ_LIGHTING (1 << 7)             // 0x00000080
#define JOBJ_TEXGEN (1 << 8)               // 0x00000100
#define JOBJ_BILLBOARD (1 << 9)            // 0x00000200
#define JOBJ_VBILLBOARD (2 << 9)           // 0x00000400
#define JOBJ_HBILLBOARD (3 << 9)           // 0x00000600
#define JOBJ_RBILLBOARD (4 << 9)           // 0x00000800
#define JOBJ_INSTANCE (1 << 12)            // 0x00001000
#define JOBJ_PBILLBOARD (1 << 13)          // 0x00002000
#define JOBJ_SPLINE (1 << 14)              // 0x00004000
#define JOBJ_FLIP_IK (1 << 15)             // 0x00008000
#define JOBJ_SPECULAR (1 << 16)            // 0x00010000
#define JOBJ_USE_QUATERNION (1 << 17)      // 0x00020000
#define JOBJ_OPA (1 << 18)                 // 0x00040000 only rendered with gx pass 3
#define JOBJ_XLU (1 << 19)                 // 0x00080000
#define JOBJ_TEXEDGE (1 << 20)             // 0x00100000
#define JOBJ_NULL (0 << 21)                // 0x00000000
#define JOBJ_JOINT1 (1 << 21)              // 0x00100000
#define JOBJ_JOINT2 (2 << 21)              // 0x00200000
#define JOBJ_EFFECTOR (3 << 21)            // 0x00300000
#define JOBJ_USER_DEFINED_MTX (1 << 23)    // 0x00800000
#define JOBJ_MTX_INDEP_PARENT (1 << 24)    // 0x01000000
#define JOBJ_MTS_INDEP_SRT (1 << 25)       // 0x02000000
#define JOBJ_GENERALFLAG (1 << 26)         // 0x04000000
#define JOBJ_GENERALFLAG2 (1 << 27)        // 0x08000000
#define JOBJ_ROOT_OPA (1 << 28)            // 0x10000000 only rendered with gx pass 3
#define JOBJ_ROOT_XLU (1 << 29)            // 0x20000000
#define JOBJ_ROOT_TEXEDGE (1 << 30)        // 0x40000000
#define JOBJ_31 (1 << 31)                  // 0x80000000

// MObj Flags
#define HSD_A_M_AMBIENT_R 1
#define HSD_A_M_AMBIENT_G 2
#define HSD_A_M_AMBIENT_B 3
#define HSD_A_M_DIFFUSE_R 4
#define HSD_A_M_DIFFUSE_G 5
#define HSD_A_M_DIFFUSE_B 6
#define HSD_A_M_SPECULAR_R 7
#define HSD_A_M_SPECULAR_G 8
#define HSD_A_M_SPECULAR_B 9
#define HSD_A_M_ALPHA 10
#define HSD_A_M_PE_REF0 11
#define HSD_A_M_PE_REF1 12
#define HSD_A_M_PE_DSTALPHA 13
#define RENDER_DIFFUSE_SHIFT 0
#define RENDER_DIFFUSE_BITS (3 << RENDER_DIFFUSE_SHIFT)
#define RENDER_DIFFUSE_MAT0 (0 << RENDER_DIFFUSE_SHIFT)
#define RENDER_DIFFUSE_MAT (1 << RENDER_DIFFUSE_SHIFT)
#define RENDER_DIFFUSE_VTX (2 << RENDER_DIFFUSE_SHIFT)
#define RENDER_DIFFUSE_BOTH (3 << RENDER_DIFFUSE_SHIFT)
#define RENDER_CONSTANT (1 << 0) // 0x00000001
#define RENDER_VERTEX (1 << 1)   // 0x00000002
#define RENDER_DIFFUSE (1 << 2)  // 0x00000004
#define RENDER_SPECULAR (1 << 3) // 0x00000008
#define CHANNEL_FIELD (RENDER_CONSTANT | RENDER_VERTEX | RENDER_DIFFUSE | RENDER_SPECULAR)
#define RENDER_TEX0 (1 << 4)  // 0x00000010
#define RENDER_TEX1 (1 << 5)  // 0x00000020
#define RENDER_TEX2 (1 << 6)  // 0x00000040
#define RENDER_TEX3 (1 << 7)  // 0x00000080
#define RENDER_TEX4 (1 << 8)  // 0x00000100
#define RENDER_TEX5 (1 << 9)  // 0x00000200
#define RENDER_TEX6 (1 << 10) // 0x00000400
#define RENDER_TEX7 (1 << 11) // 0x00000800
#define RENDER_TEXTURES (RENDER_TEX0 | RENDER_TEX1 | RENDER_TEX2 | RENDER_TEX3 | RENDER_TEX4 | RENDER_TEX5 | RENDER_TEX6 | RENDER_TEX7)
#define RENDER_TOON (1 << 12)                         // 0x00001000
#define RENDER_ALPHA_SHIFT 13                         //
#define RENDER_ALPHA_BITS (3 << RENDER_ALPHA_SHIFT)   // 0x00006000
#define RENDER_ALPHA_COMPAT (0 << RENDER_ALPHA_SHIFT) //
#define RENDER_ALPHA_MAT (1 << RENDER_ALPHA_SHIFT)    // 0x00002000
#define RENDER_ALPHA_VTX (2 << RENDER_ALPHA_SHIFT)    // 0x00004000
#define RENDER_ALPHA_BOTH (3 << RENDER_ALPHA_SHIFT)   // 0x00006000
#define RENDER_SHADOW (1 << 26)                       // 0x04000000
#define RENDER_ZMODE_ALWAYS (1 << 27)                 // 0x08000000
#define RENDER_NO_ZUPDATE (1 << 29)                   // 0x20000000
#define RENDER_XLU (1 << 30)                          // 0x40000000

// DOBJ flags
#define DOBJ_HIDDEN (1 << 0)           // 0x00000001
#define DOBJ_RENDER_ORDER_UNK (1 << 2) // 0x00000004

// POBJ flags
#define POBJ_ANIM (1 << 3)
#define POBJ_SKIN (0 << 12)
#define POBJ_SHAPEANIM (1 << 12)
#define POBJ_ENVELOPE (2 << 12)
#define POBJ_CULLFRONT (1 << 14)
#define POBJ_CULLBACK (1 << 15)

// AOBJ flags
#define AOBJ_REWINDED (1 << 26)   // 0x04000000
#define AOBJ_FIRST_PLAY (1 << 27) // 0x08000000
#define AOBJ_NO_UPDATE (1 << 28)  // 0x10000000
#define AOBJ_LOOP (1 << 29)       // 0x20000000
#define AOBJ_NO_ANIM (1 << 30)    // 0x40000000

// LOBJ flags
#define LOBJ_AMBIENT (0 << 0)
#define LOBJ_INFINITE (1 << 0)
#define LOBJ_POINT (2 << 0)
#define LOBJ_SPOT (3 << 0)
#define LOBJ_DIFFUSE (1 << 2)
#define LOBJ_SPECULAR (1 << 3)
#define LOBJ_ALPHA (1 << 4)
#define LOBJ_HIDDEN (1 << 5)
#define LOBJ_RAW_PARAM (1 << 6)
#define LOBJ_DIFF_DIRTY (1 << 7)
#define LOBJ_SPEC_DIRTY (1 << 8)

// COBJ flags
#define COBJ_UP_VECTOR_UNK (1 << 0) // 0x00000001, related to initing the up vector on cobj load (8036a440)
#define COBJ_MTX_DIRTY (1 << 1)     // 0x00000002, updates the view mtx when this flag is lowered during COBJSetCurrent @ 80368564
#define COBJ_40000000 (1 << 30)     // 0x40000000, is checked during CObjMtxIsDirty (8036959c)
#define COBJ_80000000 (1 << 31)     // 0x80000000, is raised when the COBJSetCurrent returns

#define PROJ_PERSPECTIVE 1
#define PROJ_FRUSTRUM 2
#define PROJ_ORTHO 3

// Anim flags (used for JOBJ_XByFlags)
#define JOBJ_ANIM 0x1
#define MOBJ_ANIM 0x4
#define TOBJ_ANIM 0x10
#define ALL_ANIM 0x7FF

// Macro
#define JOBJ_PauseOnFrame(jobj, child_index, flags, frame)                    \
    {                                                                         \
        JOBJ *this_jobj;                                                      \
        if (child_index != 0)                                                 \
            JOBJ_GetChild(jobj, &this_jobj, child_index, -1);                 \
        else                                                                  \
            this_jobj = jobj;                                                 \
        JOBJ_ForEachAnim(this_jobj, 6, flags, AOBJ_ReqAnim, 1, (float)frame); \
        JOBJ_AnimAll(this_jobj);                                              \
        JOBJ_ForEachAnim(this_jobj, 6, flags, AOBJ_StopAnim, 6, 0, 0);        \
    }
#define JOBJ_PlayOnFrame(jobj, child_index, flags, frame)                     \
    {                                                                         \
        JOBJ *this_jobj;                                                      \
        if (child_index != 0)                                                 \
            JOBJ_GetChild(jobj, &this_jobj, child_index, -1);                 \
        else                                                                  \
            this_jobj = jobj;                                                 \
        JOBJ_ForEachAnim(this_jobj, 6, flags, AOBJ_ReqAnim, 1, (float)frame); \
        JOBJ_AnimAll(this_jobj);                                              \
    }
#define JOBJ_GetChildPosition(jobj, child_index, pos)         \
    {                                                         \
        JOBJ *this_jobj;                                      \
        if (child_index != 0)                                 \
            JOBJ_GetChild(jobj, &this_jobj, child_index, -1); \
        else                                                  \
            this_jobj = jobj;                                 \
        JOBJ_GetWorldPosition(this_jobj, 0, pos);             \
    }

typedef enum ForEachAnimFlag //  (used for JOBJ_ForEachAnim)
{
    AOBJFLAG_JOBJ = 0x1,
    AOBJFLAG_MOBJ = 0x80,
    AOBJFLAG_TOBJ = 0x400,
    AOBJFLAG_ALL = 0x7FF,
} ForEachAnimFlag;

typedef enum HSD_ObjKind
{
    HSD_OBJKIND_NONE = 0,
    HSD_OBJKIND_COBJ,
    HSD_OBJKIND_LOBJ,
    HSD_OBJKIND_JOBJ,
    HSD_OBJKIND_FOG
} HSD_ObjKind;

/*** Structs ***/

struct HSD_Obj
{
    HSD_ClassInfo *parent;    // 0x0
    s16 ref_count;            // 0x4
    s16 ref_count_individual; // 0x6
};

struct GOBJ /* native twin, generated */
{
    union {
        char _mex_native_size[96];
        struct { short entity_class; };
        struct { char _p1[2]; char p_link; };
        struct { char _p2[3]; char gx_link; };
        struct { char _p3[4]; char p_priority; };
        struct { char _p4[5]; char gx_pri; };
        struct { char _p5[6]; char obj_kind; };
        struct { char _p6[7]; char data_kind; };
        struct { char _p7[8]; GOBJ *next; };
        struct { char _p8[16]; GOBJ *previous; };
        struct { char _p9[24]; GOBJ *nextOrdered; };
        struct { char _p10[32]; GOBJ *previousOrdered; };
        struct { char _p11[40]; GOBJProc *proc; };
        struct { char _p12[48]; void (*gx_cb)(GOBJ *gobj, int code); };
        struct { char _p13[56]; u64 cobj_links; };
        struct { char _p14[64]; void *hsd_object; };
        struct { char _p15[72]; void *userdata; };
    };
};

struct GOBJProc /* native twin, generated */
{
    union {
        char _mex_native_size[48];
        struct { GOBJ *parent; };
        struct { char _p16[8]; GOBJProc *next; };
        struct { char _p17[16]; GOBJProc *prev; };
        struct { char _p18[24]; char s_link; };
        struct { char _p19[25]; char x0d_80 : 1; };
        struct { char _p20[25]; char : 1; char x0d_40 : 1; };
        struct { char _p21[25]; char : 2; char update_idx : 2; };
        struct { char _p22[25]; char : 5; char x0d_08 : 1; };
        struct { char _p23[25]; char : 4; char x0d_04 : 1; };
        struct { char _p24[32]; GOBJ *parentGOBJ; };
        struct { char _p25[40]; void (*cb)(GOBJ *gobj); };
    };
};

struct GXList
{
    // pointed to @ -0x3e80(r13)
    GOBJ *gx_render[63]; // pointer to 63 gobjs
    GOBJ *gx_camera;     // pointer to the highest priority cobj gobj. they are linked together via the next member.
};

struct TOBJ /* native twin, generated */
{
    union {
        char _mex_native_size[216];
        struct { struct {
            union {
                char _mex_span[12];
                struct { HSD_ClassInfo *parent; };
                struct { char _p60[8]; s16 ref_count; };
                struct { char _p61[10]; s16 ref_count_individual; };
            };
        } parent; };
        struct { char _p62[16]; TOBJ *next; };
        struct { char _p63[24]; u32 id; };
        struct { char _p64[28]; u32 src; };
        struct { char _p65[32]; u32 mtxid; };
        struct { char _p66[36]; Vec4 rotate; };
        struct { char _p67[52]; Vec3 scale; };
        struct { char _p68[64]; Vec3 translate; };
        struct { char _p69[76]; u32 wrap_s; };
        struct { char _p70[80]; u32 wrap_t; };
        struct { char _p71[84]; u8 repeat_s; };
        struct { char _p72[85]; u8 repeat_t; };
        struct { char _p73[88]; u32 flags; };
        struct { char _p74[92]; f32 blending; };
        struct { char _p75[96]; u32 magFilt; };
        struct { char _p76[104]; struct _HSD_ImageDesc *imagedesc; };
        struct { char _p77[112]; struct _HSD_Tlut *tlut; };
        struct { char _p78[120]; struct _HSD_TexLODDesc *lod; };
        struct { char _p79[128]; AOBJ *aobj; };
        struct { char _p80[136]; struct _HSD_ImageDesc **imagetbl; };
        struct { char _p81[144]; struct _HSD_Tlut **tluttbl; };
        struct { char _p82[152]; int tlut_no; };
        struct { char _p83[156]; Mtx mtx; };
        struct { char _p84[204]; u32 coord; };
        struct { char _p85[208]; struct _HSD_TObjTev *tev; };
    };
};

typedef struct _HSD_AObjDesc
{
    u32 flags;                      // 0x00
    f32 end_frame;                  // 0x04
    struct _HSD_FObjDesc *fobjdesc; // 0x08
    u32 obj_id;                     // 0x0C
} HSD_AObjDesc;

struct AOBJ /* native twin, generated */
{
    union {
        char _mex_native_size[40];
        struct { u32 flags; };
        struct { char _p86[4]; f32 curr_frame; };
        struct { char _p87[8]; f32 rewind_frame; };
        struct { char _p88[12]; f32 end_frame; };
        struct { char _p89[16]; f32 framerate; };
        struct { char _p90[24]; struct _HSD_FObj *fobj; };
        struct { char _p91[32]; struct _HSD_Obj *hsd_obj; };
    };
};

struct MOBJ /* native twin, generated */
{
    union {
        char _mex_native_size[64];
        struct { int *parent; };
        struct { char _p53[8]; u32 rendermode; };
        struct { char _p54[16]; TOBJ *tobj; };
        struct { char _p55[24]; HSD_Material *mat; };
        struct { char _p56[32]; struct _HSD_PEDesc *pe; };
        struct { char _p57[40]; AOBJ *aobj; };
        struct { char _p58[48]; struct _HSD_TExpTevDesc *tevdesc; };
        struct { char _p59[56]; union _HSD_TExp *texp; };
    };
};

struct __attribute__((scalar_storage_order("big-endian"))) JOBJDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int class_name;
    unsigned int flags;
    unsigned int child;
    unsigned int next;
    union __attribute__((scalar_storage_order("big-endian"))) {
        unsigned int dobjdesc;
        unsigned int spline;
        unsigned int ptcl;
    } u;
    struct __attribute__((scalar_storage_order("big-endian"))) {
        float X;
        float Y;
        float Z;
    } rotation;
    struct __attribute__((scalar_storage_order("big-endian"))) {
        float X;
        float Y;
        float Z;
    } scale;
    struct __attribute__((scalar_storage_order("big-endian"))) {
        float X;
        float Y;
        float Z;
    } position;
    float mtx[3][4];
    unsigned int robjdesc;
};

struct __attribute__((scalar_storage_order("big-endian"))) MatAnimDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int next;
    unsigned int material_aobj;
    unsigned int texture_anim;
    int is_render_anim;
};

struct __attribute__((scalar_storage_order("big-endian"))) MatAnimJointDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int child;
    unsigned int next;
    unsigned int matanim;
};

struct __attribute__((scalar_storage_order("big-endian"))) AnimJointDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int child;
    unsigned int next;
    unsigned int aobj;
    int flags;
    int flags2;
};

struct __attribute__((scalar_storage_order("big-endian"))) WOBJDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int class_name;
    struct __attribute__((scalar_storage_order("big-endian"))) {
        float X;
        float Y;
        float Z;
    } pos;
    unsigned int robjdesc;
    unsigned int next;
};

struct __attribute__((scalar_storage_order("big-endian"))) COBJDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int class_name;
    unsigned short flags;
    unsigned short projection_type;
    unsigned short viewport_left;
    unsigned short viewport_right;
    unsigned short viewport_top;
    unsigned short viewport_bottom;
    unsigned int scissor_lr;
    unsigned int scissor_tb;
    unsigned int eye_desc;
    unsigned int interest_desc;
    float roll;
    unsigned int vector;
    float near;
    float far;
    union __attribute__((scalar_storage_order("big-endian"))) {
        struct __attribute__((scalar_storage_order("big-endian"))) {
            float fov;
            float aspect;
        } perspective;
        struct __attribute__((scalar_storage_order("big-endian"))) {
            float top;
            float bottom;
            float left;
            float right;
        } frustrum;
        struct __attribute__((scalar_storage_order("big-endian"))) {
            float top;
            float bottom;
            float left;
            float right;
        } ortho;
    } projection_param;
};

struct HSD_VtxDescList
{
    GXAttribute attr;             // 0x0
    GXAttributeType attr_type;    // 0x4
    GXComponentContents comp_cnt; // 0x8
    GXComponentType comp_type;    // 0xc
    u8 frac;                      // 0x10
    u16 stride;                   // 0x12
    void *vertex;                 // 0x14
};

struct __attribute__((scalar_storage_order("big-endian"))) POBJDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int class_name;
    unsigned int next;
    unsigned int verts;
    unsigned short flags;
    unsigned short n_display;
    unsigned int display;
    union __attribute__((scalar_storage_order("big-endian"))) {
        unsigned int joint;
        unsigned int shape_set;
        unsigned int envelope_p;
    } u;
};
struct POBJ /* native twin, generated */
{
    union {
        char _mex_native_size[48];
        struct { char _p47[8]; POBJ *next; };
        struct { char _p48[16]; struct HSD_VtxDescList *verts; };
        struct { char _p49[24]; u16 flags; };
        struct { char _p50[26]; u16 n_display; };
        struct { char _p51[32]; u8 *display; };
        struct { char _p52[40]; struct {
            union {
                char _mex_span[8];
                struct { JOBJ *jobj; };
                struct { void *shape_set; };
                struct { void *envelope_list; };
            };
        } u; };
    };
};

struct DOBJ /* native twin, generated */
{
    union {
        char _mex_native_size[48];
        struct { char _p42[8]; DOBJ *next; };
        struct { char _p43[16]; MOBJ *mobj; };
        struct { char _p44[24]; POBJ *pobj; };
        struct { char _p45[32]; AOBJ *aobj; };
        struct { char _p46[40]; u32 flags; };
    };
};

struct JOBJ /* native twin, generated */
{
    union {
        char _mex_native_size[184];
        struct { struct {
            union {
                char _mex_span[12];
                struct { HSD_ClassInfo *parent; };
                struct { char _p26[8]; s16 ref_count; };
                struct { char _p27[10]; s16 ref_count_individual; };
            };
        } object; };
        struct { char _p28[16]; JOBJ *sibling; };
        struct { char _p29[24]; JOBJ *parent; };
        struct { char _p30[32]; JOBJ *child; };
        struct { char _p31[40]; int flags; };
        struct { char _p32[48]; DOBJ *dobj; };
        struct { char _p33[56]; Vec4 rot; };
        struct { char _p34[72]; Vec3 scale; };
        struct { char _p35[84]; Vec3 trans; };
        struct { char _p36[96]; Mtx rotMtx; };
        struct { char _p37[144]; Vec3 *VEC; };
        struct { char _p38[152]; Mtx *MTX; };
        struct { char _p39[160]; AOBJ *aobj; };
        struct { char _p40[168]; int *RObj; };
        struct { char _p41[176]; JOBJDesc *desc; };
    };
};

struct WOBJ /* native twin, generated */
{
    union {
        char _mex_native_size[48];
        struct { struct {
            union {
                char _mex_span[12];
                struct { HSD_ClassInfo *parent; };
                struct { char _p120[8]; s16 ref_count; };
                struct { char _p121[10]; s16 ref_count_individual; };
            };
        } parent; };
        struct { char _p122[16]; u32 flags; };
        struct { char _p123[20]; Vec3 pos; };
        struct { char _p124[32]; AOBJ *aobj; };
        struct { char _p125[40]; void *robj; };
    };
};

struct COBJ /* native twin, generated */
{
    union {
        char _mex_native_size[168];
        struct { struct {
            union {
                char _mex_span[12];
                struct { HSD_ClassInfo *parent; };
                struct { char _p92[8]; s16 ref_count; };
                struct { char _p93[10]; s16 ref_count_individual; };
            };
        } parent; };
        struct { char _p94[16]; u32 flags; };
        struct { char _p95[20]; f32 viewport_left; };
        struct { char _p96[24]; f32 viewport_right; };
        struct { char _p97[28]; f32 viewport_top; };
        struct { char _p98[32]; f32 viewport_bottom; };
        struct { char _p99[36]; u16 scissor_left; };
        struct { char _p100[38]; u16 scissor_right; };
        struct { char _p101[40]; u16 scissor_top; };
        struct { char _p102[42]; u16 scissor_bottom; };
        struct { char _p103[48]; WOBJ *eye; };
        struct { char _p104[56]; WOBJ *interest; };
        struct { char _p105[64]; struct {
            union {
                char _mex_span[12];
                struct { f32 roll; };
                struct { Vec3 up; };
            };
        } u; };
        struct { char _p106[76]; f32 near; };
        struct { char _p107[80]; f32 far; };
        struct { char _p115[84]; struct {
            union {
                char _mex_span[16];
                struct { struct {
                    union {
                        char _mex_span[8];
                        struct { f32 fov; };
                        struct { char _p108[4]; f32 aspect; };
                    };
                } perspective; };
                struct { struct {
                    union {
                        char _mex_span[16];
                        struct { f32 top; };
                        struct { char _p109[4]; f32 bottom; };
                        struct { char _p110[8]; f32 left; };
                        struct { char _p111[12]; f32 right; };
                    };
                } frustrum; };
                struct { struct {
                    union {
                        char _mex_span[16];
                        struct { f32 top; };
                        struct { char _p112[4]; f32 bottom; };
                        struct { char _p113[8]; f32 left; };
                        struct { char _p114[12]; f32 right; };
                    };
                } ortho; };
            };
        } projection_param; };
        struct { char _p116[100]; u8 projection_type; };
        struct { char _p117[104]; Mtx view_mtx; };
        struct { char _p118[152]; AOBJ *aobj; };
        struct { char _p119[160]; Mtx *proj_mtx; };
    };
};

struct __attribute__((scalar_storage_order("big-endian"))) _HSD_ImageDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int img_ptr;
    unsigned short width;
    unsigned short height;
    unsigned int format;
    unsigned int mipmap;
    float minLOD;
    float maxLOD;
};

struct __attribute__((scalar_storage_order("big-endian"))) _HSD_Tlut /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int tlut_ptr;
    unsigned int format;
    unsigned int gxtlut;
    unsigned short colorcount;
    unsigned short x0E;
};

struct __attribute__((scalar_storage_order("big-endian"))) _HSD_TObjTev /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned char color_op;
    unsigned char alpha_op;
    unsigned char color_bias;
    unsigned char alpha_bias;
    unsigned char color_scale;
    unsigned char alpha_scale;
    unsigned char color_clamp;
    unsigned char alpha_clamp;
    unsigned char color_a;
    unsigned char color_b;
    unsigned char color_c;
    unsigned char color_d;
    unsigned char alpha_a;
    unsigned char alpha_b;
    unsigned char alpha_c;
    unsigned char alpha_d;
    struct __attribute__((scalar_storage_order("big-endian"))) {
        unsigned char r;
        unsigned char g;
        unsigned char b;
        unsigned char a;
    } constant;
    struct __attribute__((scalar_storage_order("big-endian"))) {
        unsigned char r;
        unsigned char g;
        unsigned char b;
        unsigned char a;
    } tev0;
    struct __attribute__((scalar_storage_order("big-endian"))) {
        unsigned char r;
        unsigned char g;
        unsigned char b;
        unsigned char a;
    } tev1;
    unsigned int flags;
};

struct _HSD_LightPoint
{
    f32 cutoff;
    u8 point_func;
    f32 ref_br;
    f32 ref_dist;
    u8 dist_func;
};

struct __attribute__((scalar_storage_order("big-endian"))) _HSD_LightPointDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    float cutoff;
    unsigned char point_func;
    float ref_br;
    float ref_dist;
    unsigned char dist_func;
};

struct _HSD_LightSpot
{
    f32 cutoff;
    u8 spot_func;
    f32 ref_br;
    f32 ref_dist;
    u8 dist_func;
};

struct __attribute__((scalar_storage_order("big-endian"))) _HSD_LightSpotDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    float cutoff;
    unsigned char spot_func;
    float ref_br;
    float ref_dist;
    unsigned char dist_func;
};

struct _HSD_LightAttn
{
    f32 a0;
    f32 a1;
    f32 a2;
    f32 k0;
    f32 k1;
    f32 k2;
};

struct __attribute__((scalar_storage_order("big-endian"))) LObjDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int class_name;
    unsigned int next;
    unsigned short flags;
    unsigned short attnflags;
    struct __attribute__((scalar_storage_order("big-endian"))) {
        unsigned char r;
        unsigned char g;
        unsigned char b;
        unsigned char a;
    } color;
    unsigned int position;
    unsigned int interest;
    union __attribute__((scalar_storage_order("big-endian"))) {
        unsigned int p;
        unsigned int shininess;
        unsigned int point;
        unsigned int spot;
        unsigned int attn;
    } u;
};
struct LightAnim
{
    LightAnim *next;
    struct _HSD_AObjDesc *aobjdesc;
    struct _HSD_WObjAnim *position_anim;
    struct _HSD_WObjAnim *interest_anim;
};
struct LightGroup
{
    LObjDesc *lobj_desc;
    LightAnim *anim;
};

struct LOBJ /* native twin, generated */
{
    union {
        char _mex_native_size[240];
        struct { struct {
            union {
                char _mex_span[12];
                struct { HSD_ClassInfo *parent; };
                struct { char _p126[8]; s16 ref_count; };
                struct { char _p127[10]; s16 ref_count_individual; };
            };
        } parent; };
        struct { char _p128[16]; u16 flags; };
        struct { char _p129[18]; u16 priority; };
        struct { char _p130[24]; struct LOBJ *next; };
        struct { char _p131[32]; GXColor color; };
        struct { char _p132[36]; GXColor hw_color; };
        struct { char _p133[40]; WOBJ *position; };
        struct { char _p134[48]; WOBJ *interest; };
        struct { char _p135[56]; struct {
            union {
                char _mex_span[24];
                struct { _HSD_LightPoint point; };
                struct { _HSD_LightSpot spot; };
                struct { _HSD_LightAttn attn; };
            };
        } u; };
        struct { char _p136[80]; f32 shininess; };
        struct { char _p137[84]; Vec3 lvec; };
        struct { char _p138[96]; AOBJ *aobj; };
        struct { char _p139[104]; u32 id; };
        struct { char _p140[108]; u32 spec_id; };
    };
};

struct HSD_Fog
{
    HSD_Obj parent;
    u8 type;           // 0x08
    HSD_Fog *fog_adj;  // 0x0C
    f32 start;         // 0x10
    f32 end;           // 0x14
    GXColor color;     // 0x18
    struct AOBJ *aobj; // 0x1C
};

struct __attribute__((scalar_storage_order("big-endian"))) HSD_FogDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned char type;
    unsigned int fog_adj;
    float start;
    float end;
    struct __attribute__((scalar_storage_order("big-endian"))) {
        unsigned char r;
        unsigned char g;
        unsigned char b;
        unsigned char a;
    } color;
    unsigned int aobj;
};

struct __attribute__((scalar_storage_order("big-endian"))) JOBJSet /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int jobj;
    unsigned int animjoint;
    unsigned int matanimjoint;
    unsigned int shapeaninjoint;
};

struct __attribute__((scalar_storage_order("big-endian"))) HSD_SObjDesc /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */
{
    unsigned int jobjset;
    unsigned int cobjdesc;
    unsigned int lights;
    unsigned int fog;
};

/*** Static Variables ***/
extern char mu_mx_HSD_GObjGXLinkHead[] __asm__("HSD_GObjGXLinkHead");
// static GOBJ ***stc_gobj_gx_lookup = (void *)(mu_mx_HSD_GObjGXLinkHead + 0);
extern char mu_mx_HSD_GObjPLinkHead[] __asm__("HSD_GObjPLinkHead");
static GOBJ ***stc_gobj_lookup = (void *)(mu_mx_HSD_GObjPLinkHead + 0);    //
extern char mu_mx_HSD_GObjLibInitData[] __asm__("HSD_GObjLibInitData");
static u8 *stc_gobj_proc_num = (void *)(mu_mx_HSD_GObjLibInitData + 2);           // number of elements in the below array
extern char mu_mx_HSD_GObj_GObjProcHead[] __asm__("HSD_GObj_GObjProcHead");
static GOBJProc ***stc_gobjproc_lookup = (void *)(mu_mx_HSD_GObj_GObjProcHead + 0); // array of gobj procs ptrs
extern char mu_mx_HSD_GObj_CurrentInvokedProc[] __asm__("HSD_GObj_CurrentInvokedProc");
static GOBJProc **stc_gobjproc_cur = (void *)(mu_mx_HSD_GObj_CurrentInvokedProc + 0);     // current gobj proc being processed
extern char mu_mx_HSD_GObj_804D783C[] __asm__("HSD_GObj_804D783C");
static u32 *stc_gobjproc_updateidx_cur = (void *)(mu_mx_HSD_GObj_804D783C + 0); // update index of the current gobj proc being processed. this is compared to
// static u8 *objkind_sobj = R13_OFFSET(-0x3D40);
extern void *mu_tmce_ref_objkind_sobj;
#define objkind_sobj ((u8 *)((char *)mu_tmce_ref_objkind_sobj + 0))
extern char mu_mx_HSD_GObj_CameraKind[] __asm__("HSD_GObj_CameraKind");
static u8 *objkind_cobj = (void *)(mu_mx_HSD_GObj_CameraKind + 0);
extern char mu_mx_HSD_GObj_LightKind[] __asm__("HSD_GObj_LightKind");
static u8 *objkind_lobj = (void *)(mu_mx_HSD_GObj_LightKind + 0);
extern char mu_mx_HSD_GObj_JObjKind[] __asm__("HSD_GObj_JObjKind");
static u8 *objkind_jobj = (void *)(mu_mx_HSD_GObj_JObjKind + 0);
extern char mu_mx_HSD_GObj_FogKind[] __asm__("HSD_GObj_FogKind");
static u8 *objkind_fog = (void *)(mu_mx_HSD_GObj_FogKind + 0);

/*** Functions ***/
int JOBJ_GetWorldPosition(JOBJ *source, Vec3 *add, Vec3 *dest);
void JOBJ_SetMtxDirtySub(JOBJ *jobj);
void JOBJ_SetupMtxSub(JOBJ *jobj);
void JOBJ_MakeMatrix(JOBJ *jobj);
JOBJ *JOBJ_LoadDummy();
JOBJ *JOBJ_LoadJoint(JOBJDesc *joint);
void JOBJ_RemoveAll(JOBJ *joint);
void JOBJ_Remove(JOBJ *joint);
void JOBJ_GetChild(JOBJ *joint, JOBJ **ptr, int index, ...);
void JOBJ_AddChild(JOBJ *parent, JOBJ *child);
void JOBJ_AddNext(JOBJ *parent, JOBJ *child);
float JOBJ_GetCurrentMatAnimFrame(JOBJ *joint);
void JOBJ_SetFlags(JOBJ *joint, int flags);
void JOBJ_SetFlagsAll(JOBJ *joint, int flags);
void JOBJ_ClearFlags(JOBJ *joint, int flags);
void JOBJ_ClearFlagsAll(JOBJ *joint, int flags);
void JOBJ_BillBoard(JOBJ *joint, Mtx *m, Mtx *mx);
void JOBJ_ForEachAnim(JOBJ *joint, int unk, ForEachAnimFlag flags, void *cb, int argkind, ...); // argkind specifies how to pop args off the va_list
void JOBJ_Anim(JOBJ *joint);
void JOBJ_AnimAll(JOBJ *joint);
void JOBJ_AddAnimAll(JOBJ *joint, void *animjoint, void *matanimjoint, void *shapeanimjoint);
void JOBJ_RemoveAnimAll(JOBJ *joint);
void JOBJ_ReqAnim(JOBJ *joint, float frame);
void JOBJ_ReqAnimByFlags(JOBJ *joint, int flags, float frame);
void JOBJ_ReqAnimAll(JOBJ *joint, float unk);
void JOBJ_ReqAnimAllByFlags(JOBJ *joint, int flags, float frame);
float JOBJ_GetJointAnimCurrFrame(JOBJ *joint);
float JOBJ_GetJointAnimFrameTotal(JOBJ *joint);
float JOBJ_GetJointAnimNextFrame(JOBJ *joint);
void JOBJ_SetAllMOBJFlags(JOBJ *joint, int flags);
void JOBJ_SetFlagAllMOBJ(JOBJ *joint, int flags); // enables this flag for all mobjs
int JOBJ_CheckAObjEnd(JOBJ *joint);
void JOBJ_CompileTEVAllMOBJ(JOBJ *joint);
void JObj_DispAll(JOBJ *joint, Mtx *vmtx, int rendermode, int mobj_flags);
void JOBJ_AttachPosition(JOBJ *to_attach, JOBJ *attach_to);
void JOBJ_AttachPositionRotation(JOBJ *to_attach, JOBJ *attach_to);
GOBJ *JOBJ_LoadSet(int is_hidden, JOBJSet *set, int anim_id, float frame, int p_link, int gx_link, int is_add_anim, void *cb); // 8019035c
void JOBJ_AddSetAnim(JOBJ *jobj, JOBJSet *set, int anim_id);                                                                   // 8016895c
void JOBJ_Detach(JOBJ *to_attach);
void JOBJ_ResetFromDesc(JOBJ *, JOBJDesc *);
void JOBJ_RemoveAnimByFlags(JOBJ *, int);
void AOBJ_ReqAnim(int *aobj, float unk);
void AOBJ_StopAnim(AOBJ *aobj);
void AOBJ_SetRate(AOBJ *aobj, float rate);
void AOBJ_SetFlags(AOBJ *aobj, int flags);
void AOBJ_ClearFlags(AOBJ *aobj, int flags);
void DOBJ_SetFlags(DOBJ *dobj, int flags);
void DOBJ_ClearFlags(DOBJ *dobj, int flags);
void DOBJ_AddAnimAll(DOBJ *dobj, void *matanim, void *textureanim);
void TOBJ_AddAnim(TOBJ *tobj, void *textureanim);
COBJ *COBJ_Alloc();
COBJ *COBJ_LoadDesc(COBJDesc *cobj);
COBJ *COBJ_LoadDescSetScissor(COBJDesc *cobj);
void COBJ_Init(COBJ *cobj, COBJDesc *cobj_desc); // re-initializes a live cobj using its descriptor
void CObjThink_Common(GOBJ *gobj);
int CObj_SetCurrent(COBJ *cobj);
void CObj_SetEraseColor(int r, int g, int b, int a);
void CObj_EraseScreen(COBJ *cobj, GXBool color_update_enable, GXBool alpha_update_enable, GXBool depth_update_enable);
void CObj_UpdateAll();
void CObj_RenderGXLinks(GOBJ *gobj, int render_mode);
void CObj_EndCurrent();
void CObj_SetOrtho(COBJ *cobj, float top, float bottom, float left, float right);
void CObj_SetViewport(COBJ *cobj, float left, float right, float top, float bottom);
void CObj_SetScissor(COBJ *cobj, u16 top, u16 bottom, u16 left, u16 right);
void CObj_SetEyePosition(COBJ *cobj, Vec3 *eye_pos);
void COBJ_GetEyePosition(COBJ *cobj, Vec3 *eye_pos);
void CObj_SetInterest(COBJ *cobj, Vec3 *pos);
void CObj_SetRoll(COBJ *cobj, float roll);
void CObj_Release(COBJ *cobj);
void CObj_Destroy(COBJ *cobj);
COBJ *COBJ_GetCurrent(void);
void COBJ_GetEyeVector(COBJ *cobj, Vec3 *eye_vec);
void COBJ_GetInterest(COBJ *cobj, Vec3 *interest);
float COBJ_GetEyeDistance(COBJ *cobj);
void COBJ_GetViewingMtx(COBJ *cobj, Mtx *out);
Mtx *COBJ_SetupViewingMtx(COBJ *cobj);
GOBJ *GObj_Create(int entity_class, int p_link, int p_priority);
void GObj_Destroy(GOBJ *gobj);
void GObj_AddGXLink(GOBJ *gobj, void *cb, int gx_link, int gx_pri);
void GObj_DestroyGXLink(GOBJ *gobj);
void GObj_GXReorder(GOBJ *gobj, int unk);
GOBJProc *GObj_AddProc(GOBJ *gobj, void *callback, int priority);
void GObj_RemoveProc(GOBJ *gobj);
void GObj_AddObject(GOBJ *gobj, u8 obj_kind, void *object);
void GObj_FreeObject(GOBJ *gobj);
void GObj_AddUserData(GOBJ *gobj, int userDataKind, void *destructor, void *userData);
void GOBJ_InitCamera(GOBJ *gobj, void *cb, int gx_pri);
void GObj_Anim(GOBJ *gobj);
void *GObj_AddRenderObject(GOBJ *gobj, int width, int height);
void GObj_ProcUnk(GOBJ *gobj);
void GObj_DestroyByPLink(int p_link);                           // destroys all gobjs with p_link X
void GObj_DestroyByPLinkRange(int p_link_low, int p_link_high); // destroys all gobjs of p_link_low -> p_link_high
void GObj_UpdateAll();
void GXLink_Common(GOBJ *gobj, int pass);
int GX_LookupRenderPass(int pass);
void GXLink_LObj(GOBJ *gobj, int pass);
void GXLink_Fog(GOBJ *gobj, int pass);
LOBJ *LObj_LoadDesc(void *lobjdesc);
LOBJ *LObj_CreateAll(void **lobjdesc);
int LObj_GetPosition(LOBJ *lobj, Vec3 *pos);
void LObj_SetPosition(LOBJ *lobj, Vec3 *pos);
int LObj_GetInterest(LOBJ *lobj, Vec3 *pos);
void LObj_SetInterest(LOBJ *lobj, Vec3 *pos);
void LObj_ReqAnimAll(LOBJ *lobj, float frame);
void LObj_AnimAll(LOBJ *lobj);
void LObj_DeleteCurrentAll(int unk);
void LObj_RemoveAll(LOBJ *lobj);
HSD_Fog *Fog_LoadDesc(HSD_FogDesc *fogdesc);
void Fog_Set(HSD_Fog *fog);
void Fog_Release(HSD_Fog *fog);
DOBJ *JOBJ_GetDObj(JOBJ *jobj);
void *MOBJ_SetAlpha(DOBJ *dobj, float alpha);
void MOBJ_SetToonTextureImage(_HSD_ImageDesc *);
void MOBJ_ReqAnim(MOBJ *, float frame);
void MObj_Anim(MOBJ *);
void GObj_CopyGXPri(GOBJ *target, GOBJ *source);
#endif
