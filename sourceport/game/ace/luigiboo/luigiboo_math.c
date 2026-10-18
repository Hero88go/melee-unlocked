/* Literal native translations of the two copies of the path solver in
 * PlLb.dat. Offsets below are relative to ftFunction or article 1 itFunction.
 * No retail function is substituted for these local bodies. */
#include "luigiboo_math.h"

#include <math.h>

float ftLb_EaseSquare(float t) { return t * t; }

ftLb_Path* ftLb_PathSolve(ftLb_Path* out, const Vec3* start, const Vec3* end,
                        int steps, ftLb_Ease ease, float height, float angle)
{
    float inv_steps = (float) (1.0 / (double) steps);
    float start_x = start->x;
    float start_y = start->y;
    float end_x = end->x;
    float end_y = end->y;
    float near_end;
    float slope_step;
    float lower_y;
    float control_x;
    float control_y;
    float dx;
    float dy;
    float length;
    float control_angle;
    float control_cos;
    float control_sin;

    /* ft+1F54, +1F60, +1F80, +1F90. The original callback may carry
     * side effects, so its discarded return values remain discarded calls. */
    ease(0.0f);
    ease(inv_steps);
    near_end = ease((float) (steps - 1) * inv_steps);
    slope_step = (ease(1.0f) - near_end) * 3.0f;
    lower_y = end_y - height / slope_step;
    control_x = (float) __builtin_fma((double) end_x, 2.0, -(double) start_x);
    control_y = (float) __builtin_fma((double) lower_y, 2.0, -(double) start_y);

    dx = end_x - start_x;
    if (dx < 0.0f) {
        dx = -dx;
    }
    dy = lower_y - start_y;
    if (dy < 0.0f) {
        dy = -dy;
    }
    /* ft+1FE0/+1FF0 use double fmul/fmadd, followed by frsp. */
    length = sqrtf((float) __builtin_fma((double) dy, (double) dy,
                                       (double) dx * (double) dx));
    length = length * 1.5f;
    control_angle = atan2f(
        (float) __builtin_fma((double) (lower_y + start_y), 0.5,
                             -(double) control_y),
        (float) __builtin_fma((double) (end_x + start_x), 0.5,
                             -(double) control_x));
    if (start->x < end->x) {
        angle = control_angle + angle;
    } else {
        angle = control_angle - angle;
    }
    control_cos = cosf(angle);
    control_sin = sinf(angle);

    out->start.x = start_x;
    out->start.y = start_y;
    out->control1.x = (float) __builtin_fma((double) control_cos, (double) length,
                                         (double) control_x);
    out->control1.y = (float) __builtin_fma((double) control_sin, (double) length,
                                         (double) control_y);
    out->end.x = end_x;
    out->end.y = end_y;
    out->control2.x = end_x;
    out->control2.y = lower_y;
    /* The console leaves these bytes as stack contents. They are never read
     * by either path routine. Give native copies deterministic unused bytes. */
    out->start.z = out->control1.z = out->end.z = out->control2.z = 0.0f;
    out->spare = 0;
    return out;
}

Vec3* ftLb_PathSample(Vec3* out, const ftLb_Path* path, ftLb_Ease ease, float t)
{
    float eased = ease(t);
    float t2 = eased * eased;
    float three_t = eased * 3.0f;
    float one_minus_t = 1.0f - eased;
    float t3 = eased * t2;
    float omt2 = one_minus_t * one_minus_t;
    float c1x = three_t * path->control1.x;
    float c1y = three_t * path->control1.y;
    float three_omt = one_minus_t * 3.0f;
    float omt3;
    float c2x;
    float c2y;
    float x;
    float y;

    c1x = c1x * omt2;
    omt3 = one_minus_t * omt2;
    c1y = c1y * omt2;
    c2x = three_omt * path->control2.x;
    c2y = three_omt * path->control2.y;
    x = (float) __builtin_fma((double) path->start.x, (double) omt3, (double) c1x);
    y = (float) __builtin_fma((double) path->start.y, (double) omt3, (double) c1y);
    x = (float) __builtin_fma((double) c2x, (double) t2, (double) x);
    y = (float) __builtin_fma((double) c2y, (double) t2, (double) y);
    x = (float) __builtin_fma((double) path->end.x, (double) t3, (double) x);
    y = (float) __builtin_fma((double) path->end.y, (double) t3, (double) y);
    out->x = x;
    out->y = y;
    out->z = 0.0f;
    return out;
}

float ftLb_VectorLength(const Vec3* vector)
{
    float yy = vector->y * vector->y;
    float xy = (float) __builtin_fma((double) vector->x, (double) vector->x,
                                   (double) yy);
    float xyz = (float) __builtin_fma((double) vector->z, (double) vector->z,
                                    (double) xy);
    return sqrtf(xyz);
}
