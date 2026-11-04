/* Exercise the production SIS state routines at allocation and cursor boundaries. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef float f32;
typedef struct { f32 x, y; } Vec2;
typedef struct {
    u8* state_stack;
    u16 state_stack_used, state_stack_capacity;
    Vec2 x78, x80;
    struct { u8 r, g, b, a; } active_color;
    u8 alignment;
    u8 hidden, font_idx;
} HSD_Text;
enum { SIS_SAVED_CURSOR_SIZE = sizeof(u8*) };
/* One neighbor byte makes dependence on memory outside the logical allocation deterministic. */
static void* HSD_SisLib_Alloc(int size) { return calloc((size_t)size + 1, 1); }
static void HSD_SisLib_Free(void* p) { free(p); }
#define SIS_GET_U16(p) ((u16)(((p)[0] << 8) | (p)[1]))
#define SIS_GET_S16(p) ((s16)SIS_GET_U16(p))
static u8* saved_cursor(const u8* p)
{
    uintptr_t value = 0;
    for (int i = 0; i < SIS_SAVED_CURSOR_SIZE; i++) value = (value << 8) | p[i];
    return (u8*)value;
}
#define SIS_GET_SAVED_CURSOR(p) saved_cursor(p)
static int reports;
static void OSReport(const char* format, ...) { (void)format; reports++; }
#include "HSD_SisLib_803A7684_body.h"
#include "HSD_SisLib_803A7F0C_body.h"
#include "sisRejectInvalidCursor_body.h"

int main(void)
{
    HSD_Text text = {0};
    /* No stack allocation exists yet. Empty pops must not read it. */
    assert(HSD_SisLib_803A7F0C(&text, 5) == NULL);
    text.x78 = (Vec2){1, 2}; text.x80 = (Vec2){3, 4};
    text.active_color.r = 17; text.active_color.g = 23; text.active_color.b = 42;
    text.alignment = 2;
    HSD_SisLib_803A7684(&text, NULL, 4);
    HSD_SisLib_803A7684(&text, NULL, 2);
    HSD_SisLib_803A7684(&text, NULL, 1);
    HSD_SisLib_803A7684(&text, NULL, 3);
    assert(text.state_stack_used == 16 && text.state_stack_capacity == 16);
    for (int neighbor = 0; neighbor < 256; neighbor++) {
        text.state_stack[16] = (u8)neighbor;
        assert(HSD_SisLib_803A7F0C(&text, 5) == NULL);
        assert(text.state_stack_used == 16);
    }
    text.x78 = (Vec2){0, 0}; text.x80 = (Vec2){0, 0}; text.alignment = 0;
    HSD_SisLib_803A7F0C(&text, 1);
    assert(text.x78.x == 1 && text.x78.y == 2);
    HSD_SisLib_803A7F0C(&text, 3);
    assert(text.x80.x == 3 && text.x80.y == 4);
    HSD_SisLib_803A7F0C(&text, 2);
    assert(text.active_color.r == 17 && text.active_color.g == 23 && text.active_color.b == 42);
    HSD_SisLib_803A7F0C(&text, 4);
    assert(text.alignment == 2 && text.state_stack_used == 0);
    HSD_SisLib_Free(text.state_stack);
    text = (HSD_Text){0};
    /* Runtime cursors retain all native pointer bits across nested measurement/render flags. */
    u8* cursor = (u8*)(uintptr_t)(sizeof(void*) > 4 ? UINT64_C(0x123456789abc) : UINT64_C(0x81234567));
    HSD_SisLib_803A7684(&text, cursor, 5);
    HSD_SisLib_803A7684(&text, (u8*)(uintptr_t)0x80001234, 0x85);
    assert(HSD_SisLib_803A7F0C(&text, 0x85) == (u8*)(uintptr_t)0x80001234);
    assert(HSD_SisLib_803A7F0C(&text, 5) == cursor);
    assert(text.state_stack_used == 0);
    assert(HSD_SisLib_803A7F0C(&text, 5) == NULL);
    HSD_SisLib_Free(text.state_stack);
    text = (HSD_Text){0};
    /* Exact cursor from the report is rejected before either text reader can dereference it. */
    assert(sisRejectInvalidCursor(&text, (u8*)(uintptr_t)0x5BBB));
    assert(text.hidden && reports == 1);
    text.hidden = 0;
    u8 valid_stream[] = {0};
    assert(!sisRejectInvalidCursor(&text, valid_stream));
    assert(!text.hidden && reports == 1);
    return 0;
}
