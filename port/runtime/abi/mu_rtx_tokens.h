/* Render semantics for the ray-traced renderer: private FIFO tokens 0xF3..0xF6.
 *
 * They are not GX commands. While the host asks for them (MuHostApi.native_rtx_semantics, host
 * API 25) the native game writes them into the command stream next to the draws they describe, and
 * the host's GX decoder (port/runtime/gx/rtx_semantics.cpp) takes them out again, so they never reach
 * a shader UID, a pipeline, the simulation, a snapshot or a replay. With the host not asking, not one
 * byte is written and the stream is the game's own.
 *
 * The game only reads values it has already computed for its own drawing (it never calls a getter
 * that resolves something lazily), so sending them cannot change what it does next.
 *
 * Shared by the game (GCC, no C library headers) and the host (MSVC): plain #defines only. Every
 * multi-byte value is big-endian, as the write-gather pipe carries it.
 *
 *   F3 03 <count> 00                textures a material setup loads (HSD_TObjSetup), in chain order,
 *                                   count x 16 bytes; the host keeps them for the next material:
 *           +0 u8 texmap            GX_TEXMAP0..7 the texture is loaded into, 0xFF none
 *           +1 u8 coord             TEX_COORD_* (UV, REFLECTION, HILIGHT, SHADOW, TOON, ...)
 *           +2 u8 texgen source     GX_TG_*
 *           +3 u8 reserved
 *           +4 u32 flags            HSD_TObj.flags (lightmap role, colour/alpha map operation)
 *           +8 f32 blending         HSD_TObj.blending
 *           +12 u16 width, u16 height of the image
 *   F3 01 00 00                     material begin (HSD_DObjDisp, after the material's setup)
 *       +4  u32 rendermode          HSD_MObj.rendermode (RENDER_* in mobj.h)
 *       +8  u32 ambient  RGBA       HSD_Material colours
 *       +12 u32 diffuse  RGBA
 *       +16 u32 specular RGBA
 *       +20 f32 alpha
 *       +24 f32 shininess
 *       +28 u32 identity            the material object, hashed (stable while it is alive)
 *   F3 02                           material end (the DObj's draws are done)
 *
 *   F4 <light count> 00 00          the active light set (HSD_LObjSetupInit, once per camera)
 *       light count x 68 bytes:
 *           +0 u8 type              LOBJ_AMBIENT 0, LOBJ_INFINITE 1, LOBJ_POINT 2, LOBJ_SPOT 3
 *           +1 u8 diffuse slot      GX light index 0..7 (8 = the ambient slot), 0xFF none
 *           +2 u8 specular slot     GX light index 0..7, 0xFF none
 *           +3 u8 resolved          bit 0 position, bit 1 interest: the world value was computed
 *           +4 u32 flags            HSD_LObj.flags (LOBJ_DIFFUSE, LOBJ_SPECULAR, LOBJ_ALPHA, ...)
 *           +8 u32 colour RGBA
 *           +12 f32 x3 position     world space (0 when not resolved)
 *           +24 f32 x3 interest     world space (the point an infinite or spot light aims at)
 *           +36 u32 x6 parameters   HSD_LObj.u raw (point/spot: cutoff, func, ref_br, ref_dist,
 *                                   dist_func; raw attenuation: a0 a1 a2 k0 k1 k2)
 *           +60 f32 shininess
 *           +64 u32 identity
 *
 *   F5 <render pass> <projection> 00   camera made current (HSD_CObjSetCurrent)
 *       +4  u32 identity
 *       +8  f32 x12 view matrix     world -> view, row major 3x4 (HSD_CObj.view_mtx)
 *       +56 f32 x3 eye              world space (0 when not resolved; the view matrix is exact)
 *       +68 f32 x3 interest         world space
 *       +80 f32 x4 projection       perspective: fov, aspect, 0, 0; frustum/ortho: top, bottom,
 *                                   left, right
 *       +96 f32 near, +100 f32 far
 *       +104 f32 x4 viewport        xmin, xmax, ymin, ymax
 *
 *   F6 <pass class>                 what the following draws are (MU_RTX_PASS_*)
 */
#ifndef MU_RTX_TOKENS_H
#define MU_RTX_TOKENS_H

#define MU_RTX_TOKEN_MATERIAL 0xF3
#define MU_RTX_TOKEN_LIGHTS 0xF4
#define MU_RTX_TOKEN_CAMERA 0xF5
#define MU_RTX_TOKEN_PASS 0xF6

#define MU_RTX_MATERIAL_BEGIN 1
#define MU_RTX_MATERIAL_END 2
#define MU_RTX_MATERIAL_TEXTURES 3
#define MU_RTX_MATERIAL_HEADER_BYTES 32
#define MU_RTX_TEXTURES_HEADER_BYTES 4
#define MU_RTX_TOBJ_BYTES 16
#define MU_RTX_MAX_TOBJS 8

#define MU_RTX_LIGHTS_HEADER_BYTES 4
#define MU_RTX_LIGHT_BYTES 68
#define MU_RTX_MAX_LIGHTS 9 /* MAX_GXLIGHT: eight GX lights and the ambient slot */
#define MU_RTX_LIGHT_POSITION_RESOLVED 0x01
#define MU_RTX_LIGHT_INTEREST_RESOLVED 0x02

#define MU_RTX_CAMERA_BYTES 120

/* Pass classes. */
#define MU_RTX_PASS_NONE 0
#define MU_RTX_PASS_PARTICLES 1 /* the particle system (efLib_render_callback) */

#endif
