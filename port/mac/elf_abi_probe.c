/* A small game library for port/mac/elf_loader_test.cpp: built like melee_game.so (an ELF linked at
 * 0x82800000), it exercises what the Mac loader has to get right. Pointers in data need RELATIVE
 * relocations; the printf family crosses from the Linux calling convention to Darwin's, with more
 * integer and floating-point arguments than there are registers so some arrive on the stack, and
 * vsnprintf receives a Linux va_list. SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdarg.h>
#include <stdio.h>

static const char* const names[] = {"zero", "one", "two"};
static int counter = 40;

static int format_list(char* out, unsigned long n, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(out, n, fmt, ap);
    va_end(ap);
    return r;
}

__attribute__((visibility("default"))) int mu_probe(char* out, int n)
{
    int k = snprintf(out, n, "%d %s %.3f %ld %c %x %g %d %d %d %d %d %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f|",
                     1, names[2], 3.25, 4L, 'f', 0xab, 1e10, 7, 8, 9, 10, 11,
                     1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5);
    k += format_list(out + k, n - k, "%s=%d,%f,%*d,%lld,%.2e|", names[1], ++counter, 0.5, 5, 9, -3LL, 12345.678);
    return k;
}

/* The address of something in this image's data, for the window check. */
__attribute__((visibility("default"))) const void* mu_probe_data(void) { return &counter; }
