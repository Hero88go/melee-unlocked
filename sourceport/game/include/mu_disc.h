/* Data that comes off the disc keeps the console's layout in memory: big-endian scalars, MSB-first
 * bitfields and 32-bit pointer slots. Nothing is converted at load. A struct that maps such data is
 * marked DISC_STRUCT and the compiler swaps each field as it is read or written, which costs one
 * instruction and leaves the file's own bytes (and the vertex and texture data the GPU decoder
 * reads in place) exactly as they were.
 *
 * Three rules the compiler enforces or that bite silently:
 *   - the address of a scalar field of a DISC_STRUCT cannot be taken; copy it out first
 *   - the order does not reach into a nested struct: a Vec3 inside a DISC_STRUCT stays host order
 *     unless the nested type is itself big-endian, hence the _BE twins below
 *   - an array of plain scalars reached through a pointer is not covered; use the be_* wrappers
 */
#ifndef MU_DISC_H
#define MU_DISC_H

#include <stdint.h>

#define DISC_STRUCT __attribute__((scalar_storage_order("big-endian")))

/* A pointer slot inside disc data. The archive loader relocates it to a real address, and MEM1 and
 * the game image both sit below 4 GB so that address fits. It is a struct on purpose: using one as
 * if it were a pointer is a compile error rather than a truncation. */
#define DISC_PTR(T) struct { uint32_t raw; } DISC_STRUCT
#define DISC_GET(T, slot) ((T*) (uintptr_t) (slot).raw)
#define DISC_SET(slot, ptr) ((slot).raw = mu_addr32(ptr))
#define DISC_NULL(slot) ((slot).raw == 0)

/* Narrowing a pointer to the 32 bits a disc slot or a hardware register holds. Anything the game
 * can legitimately point at is below 4 GB; a stack address or a host allocation is not, and the
 * debug build stops on it. */
void mu_addr32_failed(const void* ptr, const char* file, int line);
static inline uint32_t mu_addr32_checked(const void* ptr, const char* file, int line)
{
    uintptr_t value = (uintptr_t) ptr;
#ifndef NDEBUG
    if (value >> 32) {
        mu_addr32_failed(ptr, file, line);
    }
#endif
    return (uint32_t) value;
}
#define mu_addr32(ptr) mu_addr32_checked((ptr), __FILE__, __LINE__)
#define MU_ADDR32(ptr) mu_addr32(ptr)

/* Elements of a scalar array reached through a pointer: f32* weights becomes be_f32* weights. */
typedef struct { float v; } DISC_STRUCT be_f32;
typedef struct { uint32_t v; } DISC_STRUCT be_u32;
typedef struct { int32_t v; } DISC_STRUCT be_s32;
typedef struct { uint16_t v; } DISC_STRUCT be_u16;
typedef struct { int16_t v; } DISC_STRUCT be_s16;

/* Big-endian twins of the maths types, for use inside disc structs. */
typedef struct { float x, y, z; } DISC_STRUCT Vec3_BE;
typedef struct { float x, y; } DISC_STRUCT Vec2_BE;
typedef struct { float x, y, z, w; } DISC_STRUCT Quaternion_BE;
typedef struct { float m[3][4]; } DISC_STRUCT Mtx_BE;

#endif
