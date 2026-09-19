/* setjmp expands at its call site in Gecko_setjmp.h. Every game longjmp uses
 * value 1, as required by the compiler's matching builtin. */
void __longjmp(void* env, int val)
{
    (void) val;
    __builtin_longjmp((void**) env, 1);
}
