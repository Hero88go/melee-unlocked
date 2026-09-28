/* The table a parity library exports: native routines that must agree, bit for bit, with the same
 * routine in the translated game. Plain C, shared by the GCC side that fills it and the MSVC harness
 * that reads it (port/tests/native_parity.cpp).
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MU_PARITY_H
#define MU_PARITY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MU_RET_NONE = 0, MU_RET_U32 = 1, MU_RET_REAL = 2 };

/* Arguments in the console's order: pointers to float arrays first (r3..), then an optional char,
 * then up to three scalars (f1..), which are doubles when is_double is set. */
typedef struct MuParityEntry {
    const char* name;        /* the routine's symbol in the DOL */
    void* fn;
    uint8_t n_ptr;           /* 0..9 (the 9th goes on the guest stack) */
    uint8_t ptr_floats[9];   /* length of each array, in floats */
    uint8_t ptr_written[9];  /* 1 when the routine writes it (it may then also alias an input) */
    uint8_t has_char;
    uint8_t n_scalar;        /* 0..8 (f1..f8) */
    uint8_t is_double;
    uint8_t ret;             /* MU_RET_* */
    /* Per scalar, the range the routine is meant for; both zero means the harness's own spread of
     * magnitudes. A series that converges for the game's inputs may never converge for others. */
    float scalar_min[8], scalar_max[8];
    /* The range every pointer-array element is drawn from; both zero means the harness's own spread. */
    float ptr_min, ptr_max;
} MuParityEntry;

/* Exported by the library as "mu_parity_table". */
typedef const MuParityEntry* (*MuParityTableFn)(int* count);

#ifdef __cplusplus
}
#endif
#endif
