/* PlRc.dat +44D4..+51F8. All storage here is native float/vector/matrix
 * data; the disc's instruction bytes are evidence only. */
#include "raichu_trail.h"
#include <math.h>
#include <melee/lb/lbvector.h>
#include <sysdolphin/baselib/mtx.h>

static Vec3 scaled(Vec3 v, float s)
{
    return (Vec3) { v.x * s, v.y * s, v.z * s };
}
static Vec3 sum(Vec3 a, Vec3 b)
{
    Vec3 out;
    PSVECAdd(&a, &b, &out);
    return out;
}
static Vec3 diff(Vec3 a, Vec3 b)
{
    Vec3 out;
    PSVECSubtract(&a, &b, &out);
    return out;
}
static Vec3 madd(float s, Vec3 a, Vec3 b)
{
    return (Vec3) { __builtin_fmaf(s, a.x, b.x),
                    __builtin_fmaf(s, a.y, b.y),
                    __builtin_fmaf(s, a.z, b.z) };
}
static Vec3 translation(MtxPtr m)
{
    Vec3 out;
    HSD_MtxGetTranslate(m, &out);
    return out;
}

/* +44D4. Two discarded cross products at +457C and +458C use otherwise
 * uninitialized scratch vectors. They have no reader and are omitted. */
void ftRc_Trail_Controls(MtxPtr before, MtxPtr start, MtxPtr end, MtxPtr after,
                        MtxPtr offset, ftRc_TrailSegment* out)
{
    Mtx unused_start, unused_end;
    Vec3 a = translation(start), b = translation(end), direction = diff(b, a);
    float length;
    PSMTXConcat(start, offset, unused_start);
    PSMTXConcat(end, offset, unused_end);
    length = lbVector_Normalize(&direction);
    PSVECNormalize(&direction, &direction);
    if (before != NULL) {
        Vec3 p = translation(before);
        Vec3 diagonal = diff(b, p), tangent = diff(a, p);
        float diagonal_len = lbVector_Normalize(&diagonal);
        float prev_len = lbVector_Normalize(&tangent);
        float total = length + prev_len;
        float w = length / total;
        Vec3 blended = madd(w, tangent, scaled(direction, prev_len / total));
        float magnitude = sqrtf(__builtin_fmaf(blended.z, blended.z,
                                __builtin_fmaf(blended.x, blended.x,
                                               blended.y * blended.y)));
        Vec3 delta = scaled(blended, ((diagonal_len / total) * length) /
                                     (magnitude + magnitude));
        out->control1 = sum(delta, a);
        if (after == NULL) {
            float dot = PSVECDotProduct(&direction, &delta);
            out->control2 = madd(-(dot + dot), direction, sum(b, delta));
        }
    }
    if (after != NULL) {
        Vec3 p = translation(after);
        Vec3 diagonal = diff(p, a), tangent = diff(p, b);
        float diagonal_len = lbVector_Normalize(&diagonal);
        float next_len = lbVector_Normalize(&tangent);
        float total = length + next_len;
        float w = length / total;
        Vec3 blended = madd(next_len / total, direction, scaled(tangent, w));
        float magnitude = sqrtf(__builtin_fmaf(blended.z, blended.z,
                                __builtin_fmaf(blended.x, blended.x,
                                               blended.y * blended.y)));
        Vec3 delta = scaled(blended, ((diagonal_len / total) * length) /
                                     (magnitude + magnitude));
        out->control2 = diff(b, delta);
        if (before == NULL) {
            float dot = PSVECDotProduct(&direction, &delta);
            out->control1 = madd(dot + dot, direction, diff(a, delta));
        }
    } else if (before == NULL) {
        out->control1 = out->control2 = scaled(sum(a, b), 0.5f);
    }
    out->start = a;
    out->end = b;
    out->length = length;
}

/* +4FC4: evaluation order, including each single-precision rounding. */
static float ftRc_Trail_Cubic(float a, float b, float c, float d, float t)
{
    float tt = t * t;
    float s = 1.0f - t;
    float ss = s * s;
    float stt = (s * 3.0f) * tt;
    float sst = (ss * 3.0f) * t;
    float sss = s * ss;
    float ttt = t * tt;
    float result = __builtin_fmaf(a, sss, sst * b);
    result = __builtin_fmaf(stt, c, result);
    return __builtin_fmaf(d, ttt, result);
}

static Vec3 ftRc_Trail_Derivative(ftRc_TrailSegment* p, float t)
{
    float s = 1.0f - t;
    float a = (s * s) * 3.0f;
    float b = (s * 6.0f) * t;
    float c = (t * t) * 3.0f;
    Vec3 v = scaled(diff(p->control2, p->control1), b);
    v = madd(a, diff(p->control1, p->start), v);
    return madd(c, diff(p->end, p->control2), v);
}

/* +500C. out_normal and out_binormal follow the disc's cross-product order. */
static void ftRc_Trail_Frame(ftRc_TrailSegment* p, float t,
                             Vec3* out_normal, Vec3* out_binormal)
{
    float s = 1.0f - t;
    float a = s * 6.0f, b = t * 6.0f;
    Vec3 derivative = ftRc_Trail_Derivative(p, t);
    Vec3 left = madd(-2.0f, p->control1, p->control2);
    Vec3 right = madd(-2.0f, p->control2, p->end);
    Vec3 second;
    left = sum(left, p->start);
    right = sum(right, p->control1);
    second = madd(a, left, scaled(right, b));
    PSVECNormalize(&derivative, &derivative);
    PSVECCrossProduct(&derivative, &second, out_binormal);
    PSVECNormalize(out_binormal, out_binormal);
    PSVECCrossProduct(out_binormal, &derivative, out_normal);
    PSVECNormalize(out_normal, out_normal);
}

/* +4918. Preserve the mod's twist interpolation and its zero-length/parallel
 * vector behavior by using the same native vector helpers without guards. */
void ftRc_Trail_Sample(ftRc_TrailSegment* p, MtxPtr start, MtxPtr end,
                      float t, MtxPtr out)
{
    Vec3 y0 = { start[0][1], start[1][1], start[2][1] };
    Vec3 y1 = { end[0][1], end[1][1], end[2][1] };
    Vec3 tangent = ftRc_Trail_Derivative(p, t);
    Vec3 d0 = ftRc_Trail_Derivative(p, 0), d1 = ftRc_Trail_Derivative(p, 1);
    Vec3 n, b, n0, b0, n1, b1, y, x, z;
    float s = 1.0f - t;
    float w0, w1, w2, parallel, rest;
    PSVECNormalize(&y0, &y0);
    PSVECNormalize(&y1, &y1);
    PSVECNormalize(&tangent, &tangent);
    ftRc_Trail_Frame(p, t, &n, &b);
    PSVECNormalize(&d0, &d0);
    ftRc_Trail_Frame(p, 0, &n0, &b0);
    PSVECNormalize(&d1, &d1);
    ftRc_Trail_Frame(p, 1, &n1, &b1);
    w0 = __builtin_fmaf(PSVECDotProduct(&y0, &d0), s,
                        t * PSVECDotProduct(&y1, &d1));
    w1 = __builtin_fmaf(PSVECDotProduct(&y0, &n0), s,
                        t * PSVECDotProduct(&y1, &n1));
    w2 = __builtin_fmaf(PSVECDotProduct(&y0, &b0), s,
                        t * PSVECDotProduct(&y1, &b1));
    parallel = (PSVECDotProduct(&y0, &y1) + 1.0f) * 0.5f;
    rest = 1.0f - parallel;
    y = sum(sum(scaled(tangent, w0), scaled(n, w1)), scaled(b, w2));
    y = sum(scaled(y, rest), scaled(sum(scaled(y0, s), scaled(y1, t)), parallel));
    PSVECNormalize(&y, &y);
    PSVECNormalize(&tangent, &z);
    PSVECCrossProduct(&y, &z, &x);
    PSVECCrossProduct(&x, &y, &z);
    PSVECNormalize(&x, &x);
    PSVECNormalize(&z, &z);
    PSVECNormalize(&y, &y);
    out[0][0] = x.x; out[1][0] = x.y; out[2][0] = x.z;
    out[0][1] = y.x; out[1][1] = y.y; out[2][1] = y.z;
    out[0][2] = z.x; out[1][2] = z.y; out[2][2] = z.z;
    out[0][3] = ftRc_Trail_Cubic(p->start.x, p->control1.x, p->control2.x, p->end.x, t);
    out[1][3] = ftRc_Trail_Cubic(p->start.y, p->control1.y, p->control2.y, p->end.y, t);
    out[2][3] = ftRc_Trail_Cubic(p->start.z, p->control1.z, p->control2.z, p->end.z, t);
}
