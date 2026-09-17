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
    uint8_t n_ptr;           /* 0..3 */
    uint8_t ptr_floats[3];   /* length of each array, in floats */
    uint8_t ptr_written[3];  /* 1 when the routine writes it (it may then also alias an input) */
    uint8_t has_char;
    uint8_t n_scalar;        /* 0..3 */
    uint8_t is_double;
    uint8_t ret;             /* MU_RET_* */
} MuParityEntry;

/* Exported by the library as "mu_parity_table". */
typedef const MuParityEntry* (*MuParityTableFn)(int* count);

#ifdef __cplusplus
}
#endif
#endif
