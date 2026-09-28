/* The write-gather pipe, natively. The console's GX library stored each command byte, halfword,
 * word or float to one address and the hardware gathered them into the command FIFO. Here the same
 * stores append big-endian bytes to a buffer, and the host's FIFO decoder (the one the shipped
 * game already uses) drains it at the points the decoder needs to see (mu_wg_flush).
 *
 * No #include: this header reaches nearly every translation unit through GXVert.h, and the game
 * carries its own C library whose va_list and ssize_t collide with the host's. Compiler built-ins
 * only. */
#ifndef MU_GXPIPE_H
#define MU_GXPIPE_H

extern unsigned char* mu_wg_cursor;
extern unsigned char* mu_wg_high_water; /* flush once the cursor passes this */
void mu_wg_flush(void);

static inline void mu_wg_check(void)
{
    if (mu_wg_cursor >= mu_wg_high_water)
        mu_wg_flush();
}
static inline void GXW_u8(__UINT8_TYPE__ v)
{
    *mu_wg_cursor++ = v;
    mu_wg_check();
}
static inline void GXW_u16(__UINT16_TYPE__ v)
{
    __UINT16_TYPE__ be = __builtin_bswap16(v);
    __builtin_memcpy(mu_wg_cursor, &be, 2);
    mu_wg_cursor += 2;
    mu_wg_check();
}
static inline void GXW_u32(__UINT32_TYPE__ v)
{
    __UINT32_TYPE__ be = __builtin_bswap32(v);
    __builtin_memcpy(mu_wg_cursor, &be, 4);
    mu_wg_cursor += 4;
    mu_wg_check();
}
static inline void GXW_u64(__UINT64_TYPE__ v)
{
    __UINT64_TYPE__ be = __builtin_bswap64(v);
    __builtin_memcpy(mu_wg_cursor, &be, 8);
    mu_wg_cursor += 8;
    mu_wg_check();
}
static inline void GXW_s8(__INT8_TYPE__ v) { GXW_u8((__UINT8_TYPE__) v); }
static inline void GXW_s16(__INT16_TYPE__ v) { GXW_u16((__UINT16_TYPE__) v); }
static inline void GXW_s32(__INT32_TYPE__ v) { GXW_u32((__UINT32_TYPE__) v); }
static inline void GXW_s64(__INT64_TYPE__ v) { GXW_u64((__UINT64_TYPE__) v); }
static inline void GXW_f32(float v)
{
    __UINT32_TYPE__ bits;
    __builtin_memcpy(&bits, &v, 4);
    GXW_u32(bits);
}
static inline void GXW_f64(double v)
{
    __UINT64_TYPE__ bits;
    __builtin_memcpy(&bits, &v, 8);
    GXW_u64(bits);
}

#endif
