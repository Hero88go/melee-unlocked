/* The pieces of the console's C runtime that were the compiler's or the linker's job.
 *
 * The game carries its own C library (src/MSL), and most of it compiles natively. What does not is
 * what Metrowerks supplied by other means: a character-class table that lived in a library, a
 * float conversion helper, the stdio object table, the stack bounds the linker defined, and
 * setjmp, which saved PowerPC registers. */
#include "mu_shim.h"

/* ---- character classes ----
 * MSL's ctype.h indexes this table (ctype.h defines the bit for each class). The values are the
 * ASCII classes; the game only ever asks about ASCII. */
#define MU_CTRL 0x01
#define MU_WHITE 0x02
#define MU_PUNCT 0x04
#define MU_DIGIT 0x08
#define MU_HEX 0x10
#define MU_LOWER 0x20
#define MU_UPPER 0x40

const unsigned char __ctype_map[256] = {
    /* 0x00 */ MU_CTRL, MU_CTRL, MU_CTRL, MU_CTRL, MU_CTRL, MU_CTRL, MU_CTRL, MU_CTRL,
    /* 0x08 */ MU_CTRL, MU_CTRL | MU_WHITE, MU_CTRL | MU_WHITE, MU_CTRL | MU_WHITE,
               MU_CTRL | MU_WHITE, MU_CTRL | MU_WHITE, MU_CTRL, MU_CTRL,
    /* 0x10 */ MU_CTRL, MU_CTRL, MU_CTRL, MU_CTRL, MU_CTRL, MU_CTRL, MU_CTRL, MU_CTRL,
    /* 0x18 */ MU_CTRL, MU_CTRL, MU_CTRL, MU_CTRL, MU_CTRL, MU_CTRL, MU_CTRL, MU_CTRL,
    /* 0x20 */ MU_WHITE, MU_PUNCT, MU_PUNCT, MU_PUNCT, MU_PUNCT, MU_PUNCT, MU_PUNCT, MU_PUNCT,
    /* 0x28 */ MU_PUNCT, MU_PUNCT, MU_PUNCT, MU_PUNCT, MU_PUNCT, MU_PUNCT, MU_PUNCT, MU_PUNCT,
    /* 0x30 */ MU_DIGIT | MU_HEX, MU_DIGIT | MU_HEX, MU_DIGIT | MU_HEX, MU_DIGIT | MU_HEX,
               MU_DIGIT | MU_HEX, MU_DIGIT | MU_HEX, MU_DIGIT | MU_HEX, MU_DIGIT | MU_HEX,
    /* 0x38 */ MU_DIGIT | MU_HEX, MU_DIGIT | MU_HEX, MU_PUNCT, MU_PUNCT, MU_PUNCT, MU_PUNCT,
               MU_PUNCT, MU_PUNCT,
    /* 0x40 */ MU_PUNCT, MU_UPPER | MU_HEX, MU_UPPER | MU_HEX, MU_UPPER | MU_HEX,
               MU_UPPER | MU_HEX, MU_UPPER | MU_HEX, MU_UPPER | MU_HEX, MU_UPPER,
    /* 0x48 */ MU_UPPER, MU_UPPER, MU_UPPER, MU_UPPER, MU_UPPER, MU_UPPER, MU_UPPER, MU_UPPER,
    /* 0x50 */ MU_UPPER, MU_UPPER, MU_UPPER, MU_UPPER, MU_UPPER, MU_UPPER, MU_UPPER, MU_UPPER,
    /* 0x58 */ MU_UPPER, MU_UPPER, MU_UPPER, MU_PUNCT, MU_PUNCT, MU_PUNCT, MU_PUNCT, MU_PUNCT,
    /* 0x60 */ MU_PUNCT, MU_LOWER | MU_HEX, MU_LOWER | MU_HEX, MU_LOWER | MU_HEX,
               MU_LOWER | MU_HEX, MU_LOWER | MU_HEX, MU_LOWER | MU_HEX, MU_LOWER,
    /* 0x68 */ MU_LOWER, MU_LOWER, MU_LOWER, MU_LOWER, MU_LOWER, MU_LOWER, MU_LOWER, MU_LOWER,
    /* 0x70 */ MU_LOWER, MU_LOWER, MU_LOWER, MU_LOWER, MU_LOWER, MU_LOWER, MU_LOWER, MU_LOWER,
    /* 0x78 */ MU_LOWER, MU_LOWER, MU_LOWER, MU_PUNCT, MU_PUNCT, MU_PUNCT, MU_PUNCT, MU_CTRL,
    /* 0x80 and up: the game's Japanese text is handled by its own Shift-JIS code, not by these. */
    0
};

/* ---- the stack ----
 * The console's linker placed the stack and defined these; the debug build prints how much of it
 * was ever touched. Natively the game runs on the host's stack, so this is a window of the right
 * size for that code to measure against. */
unsigned char _stack_end[64 * 1024];
unsigned char _stack_addr[1] = { 0 };

/* ---- stdio ----
 * MSL's file table. Nothing in the game opens a file; the one reference comes from code that
 * checks whether a stream is buffered before it would print. */
void* __files[3] = { 0, 0, 0 };

/* ---- float conversion ----
 * Metrowerks' helper for turning a double into an unsigned 64-bit value, used where the game
 * converts a frame count. The C cast is the same operation. */
unsigned long long __cvt_dbl_usll(double value)
{
    if (value <= 0.0)
        return 0;
    if (value >= 18446744073709551616.0)
        return ~0ull;
    return (unsigned long long) value;
}

/* ---- setjmp ----
 * The console version saved the PowerPC registers by hand. Natively the host compiler's own
 * setjmp does the equivalent, and the game's buffer is large enough to hold it. */
typedef struct mu_jmp_buf_tag {
    unsigned long pc, cr, sp, rtoc, reserved, gprs[19];
    double fp[18], fpscr;
    int pad[8];
} mu_jmp_buf_tag;

int __builtin_setjmp_wrapper(void* env);

int __setjmp(void* env)
{
    /* GCC's __builtin_setjmp needs a five-word buffer and pairs with __builtin_longjmp; the game's
     * buffer is far larger, so it is used as the storage. */
    return __builtin_setjmp((void**) env);
}

void __longjmp(void* env, int val)
{
    (void) val;   /* the game only ever longjmps with 1, and __builtin_longjmp requires it */
    __builtin_longjmp((void**) env, 1);
}

/* ---- debugger ---- */
int DBIsDebuggerPresent(void) { return 0; }

/* A pointer that had to fit a 32-bit slot did not (see mu_disc.h). */
void mu_addr32_failed(const void* ptr, const char* file, int line)
{
    (void) ptr;
    mu_host->panic(file, line, "a pointer does not fit its 32-bit slot");
}
