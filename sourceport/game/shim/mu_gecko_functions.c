/* The game functions a player's Gecko code may make "return at once" on the Source Port, with their
 * native addresses, for the host (port/runtime/host/user_gecko.cpp, gecko_function_patch.h).
 *
 * The list is generated (tools/gecko_functions.py) from this library's own debug information: only
 * external functions the compiler never copied into a caller or specialised, so that overwriting a
 * function's entry reaches every call of it, and only ones whose return value is void, an integer or
 * a pointer. The host overwrites an entry only while an enabled code asks for it, offline, and puts
 * the original bytes back otherwise.
 *
 * Only the addresses are taken here, so each function is declared without its prototype; this file
 * includes no game header for that reason. */

typedef struct MuGeckoFunction {
    unsigned int console;   /* the function's console address */
    unsigned int kind;      /* its return kind: 0x00 void, 0x02 pointer, 0x11/0x21/0x41 integer, 0x19 bool */
    void* native;
} MuGeckoFunction;

#define MU_GECKO_FN(addr, name, kind) extern void name(void);
#ifdef _WIN32
#include "mu_gecko_functions.inc"
#else
#include "mu_gecko_functions_linux.inc"
#endif
#undef MU_GECKO_FN

static const MuGeckoFunction gecko_functions[] = {
#define MU_GECKO_FN(addr, name, kind) {addr, kind, (void*) name},
#ifdef _WIN32
#include "mu_gecko_functions.inc"
#else
#include "mu_gecko_functions_linux.inc"
#endif
#undef MU_GECKO_FN
    {0xFFFFFFFFu, 0, 0},
};

/* The table, sorted by console address, and how many entries it has (the end marker not counted). */
MU_EXPORT unsigned int mu_user_gecko_functions(const MuGeckoFunction** out)
{
    *out = gecko_functions;
    return (unsigned int) (sizeof gecko_functions / sizeof gecko_functions[0]) - 1u;
}
