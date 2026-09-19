#include <Runtime/Gecko_setjmp.h>

static jmp_buf saved;
static int value;

__attribute__((noinline)) static void visit(int* volatile* result)
{
    *result = &value;
    longjmp(saved, 1);
}

int main(void)
{
    int* volatile result = 0;
    if (setjmp(saved) == 0) {
        visit(&result);
        return 1;
    }
    return result != &value;
}
