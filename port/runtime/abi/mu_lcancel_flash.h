/* L-cancel display choices shared by settings and the native game. No game state changes.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MU_LCANCEL_FLASH_H
#define MU_LCANCEL_FLASH_H

#define MU_LCFLASH_TE_ENABLE       0x00000200u
#define MU_LCFLASH_TE_MISS_ONLY    0x00080000u
#define MU_LCFLASH_TE_SUCCESS_ONLY 0x00100000u
#define MU_LCFLASH_TE_GREEN        0x00200000u
#define MU_LCFLASH_TE_SUCCESS_OFF  0x00400000u
#define MU_LCFLASH_TE_REPORT_MASK  (MU_LCFLASH_TE_MISS_ONLY | MU_LCFLASH_TE_SUCCESS_ONLY)
#define MU_LCFLASH_TE_COLOR_MASK   (MU_LCFLASH_TE_GREEN | MU_LCFLASH_TE_SUCCESS_OFF)
#define MU_LCFLASH_TE_EXTRA_MASK   (MU_LCFLASH_TE_REPORT_MASK | MU_LCFLASH_TE_COLOR_MASK)

enum MuLcancelFlashMode {
    MU_LCFLASH_OFF = 0,
    MU_LCFLASH_MU_MISSED = 1,
    MU_LCFLASH_TE_MISSED = 2,
    MU_LCFLASH_TE_SUCCESS = 3,
    MU_LCFLASH_TE_BOTH = 4,
    /* Append so existing settings and TE recordings keep their numeric meaning. */
    MU_LCFLASH_MU_SUCCESS = 5,
    MU_LCFLASH_MU_BOTH = 6
};
enum MuLcancelSuccessColor {
    MU_LCFLASH_SUCCESS_OFF = 0,
    MU_LCFLASH_SUCCESS_WHITE = 1,
    MU_LCFLASH_SUCCESS_GREEN = 2
};

#if defined(__cplusplus)
#define MU_LCFLASH_INLINE constexpr
#else
#define MU_LCFLASH_INLINE static inline
#endif

/* Old recordings have only TE_ENABLE: preserve their existing white success and red miss. */
MU_LCFLASH_INLINE int mu_lcancel_renderer_mode(int mode)
{
    return mode == MU_LCFLASH_MU_MISSED || mode == MU_LCFLASH_MU_SUCCESS || mode == MU_LCFLASH_MU_BOTH;
}
MU_LCFLASH_INLINE int mu_lcancel_renderer_reports(int mode, int success)
{
    return success ? mode == MU_LCFLASH_MU_SUCCESS || mode == MU_LCFLASH_MU_BOTH
                   : mode == MU_LCFLASH_MU_MISSED || mode == MU_LCFLASH_MU_BOTH;
}
MU_LCFLASH_INLINE int mu_lcancel_flash_mode(unsigned int word, int renderer_mode)
{
    if (word & MU_LCFLASH_TE_ENABLE) {
        if (word & MU_LCFLASH_TE_MISS_ONLY) return MU_LCFLASH_TE_MISSED;
        if (word & MU_LCFLASH_TE_SUCCESS_ONLY) return MU_LCFLASH_TE_SUCCESS;
        return MU_LCFLASH_TE_BOTH;
    }
    return mu_lcancel_renderer_mode(renderer_mode) ? renderer_mode : MU_LCFLASH_OFF;
}
MU_LCFLASH_INLINE int mu_lcancel_success_color(unsigned int word)
{
    if (word & MU_LCFLASH_TE_SUCCESS_OFF) return MU_LCFLASH_SUCCESS_OFF;
    return (word & MU_LCFLASH_TE_GREEN) ? MU_LCFLASH_SUCCESS_GREEN : MU_LCFLASH_SUCCESS_WHITE;
}
MU_LCFLASH_INLINE unsigned int mu_lcancel_with_flash_mode(unsigned int word, int mode)
{
    unsigned int next = word & ~(MU_LCFLASH_TE_ENABLE | MU_LCFLASH_TE_REPORT_MASK);
    if (mode >= MU_LCFLASH_TE_MISSED && mode <= MU_LCFLASH_TE_BOTH) {
        next |= MU_LCFLASH_TE_ENABLE;
        if (mode == MU_LCFLASH_TE_MISSED) next |= MU_LCFLASH_TE_MISS_ONLY;
        if (mode == MU_LCFLASH_TE_SUCCESS) next |= MU_LCFLASH_TE_SUCCESS_ONLY;
    }
    return next;
}
MU_LCFLASH_INLINE unsigned int mu_lcancel_with_success_color(unsigned int word, int color)
{
    unsigned int next = word & ~MU_LCFLASH_TE_COLOR_MASK;
    if (color == MU_LCFLASH_SUCCESS_OFF) next |= MU_LCFLASH_TE_SUCCESS_OFF;
    if (color == MU_LCFLASH_SUCCESS_GREEN) next |= MU_LCFLASH_TE_GREEN;
    return next;
}
MU_LCFLASH_INLINE int mu_lcancel_te_reports(unsigned int word, int success)
{
    if (!(word & MU_LCFLASH_TE_ENABLE)) return 0;
    if (success && (word & MU_LCFLASH_TE_MISS_ONLY)) return 0;
    if (!success && (word & MU_LCFLASH_TE_SUCCESS_ONLY)) return 0;
    return !success || mu_lcancel_success_color(word) != MU_LCFLASH_SUCCESS_OFF;
}

#undef MU_LCFLASH_INLINE
#endif
