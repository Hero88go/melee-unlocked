/* ACE 2.0.0's Ninten: the ability Kirby copies from him (ninten_kirby.c, PlKbCpNt.dat). */
#ifndef MU_ACE_NINTEN_KIRBY_H
#define MU_ACE_NINTEN_KIRBY_H

#include "../../akaneia/common/mu_ak_kirby.h"

/* Ninten's article the copy spawns: index 3 of his MxDt item list (item kind 332). The hat
 * file's itFunction 3 is its code; its ItemLogicTable is Lucas's Kirby PK Freeze table
 * (ftLc_ArticleLogic[ftLc_Art_KirbyPKFreeze]), see KIRBY.md. */
#define ftKbNt_Article_PKHypnosis 3

/* The kbFunction exports of PlKbCpNt.dat. Ninten's MuAkFighter (ace/ninten/ninten.c) points at
 * it:  .kirby = &ftKbNt_Copy, */
extern const MuAkKirbyCopy ftKbNt_Copy;

#endif
