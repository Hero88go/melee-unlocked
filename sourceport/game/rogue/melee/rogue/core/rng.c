#include "rng.h"

static unsigned mix(unsigned x)
{
    x ^= x >> 16; x *= 0x85ebca6bU;
    x ^= x >> 13; x *= 0xc2b2ae35U; x ^= x >> 16;
    return x;
}

void RogueRng_Derive(RogueRng* rng, RogueSeed seed, unsigned stream,
                     unsigned ruleset, unsigned node)
{
    /* All operations modulo 2^32; no clocks, pointers or host byte order. */
    unsigned x = mix((unsigned) seed ^ mix((unsigned) (seed >> 32)));
    x = mix(x ^ mix(stream + 0x9e3779b9U));
    x = mix(x ^ mix(ruleset + 0x85ebca6bU));
    x = mix(x ^ mix(node + 0xc2b2ae35U));
    rng->state = x ? x : 1;
    rng->draws = 0;
}

void RogueRng_Init(RogueRng* rng, RogueSeed seed, unsigned stream)
{
    unsigned high = (unsigned) (seed >> 32);
    unsigned x = (unsigned) seed ^ (high * (0x85ebca6bU + 2 * stream)) ^ (0x9e3779b9U * (stream + 1));
    x ^= x >> 16; x *= 0x85ebca6bU;
    x ^= x >> 13; x *= 0xc2b2ae35U; x ^= x >> 16;
    rng->state = x ? x : 1;
    rng->draws = 0;
}
unsigned RogueRng_Next(RogueRng* rng)
{
    unsigned x = rng->state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng->state = x; rng->draws++;
    return x;
}
unsigned RogueRng_Bounded(RogueRng* rng, unsigned bound)
{
    unsigned value, threshold;
    if (!bound) return 0;
    threshold = (0U - bound) % bound;
    do { value = RogueRng_Next(rng); } while (value < threshold);
    return value % bound;
}
