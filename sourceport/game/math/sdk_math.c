/* Source implementations of Dolphin SDK matrix/vector algorithms.
 * Based on libs/dolphin/src/dolphin/mtx/{mtx,mtxvec,vec}.c in the melee
 * decompilation. Explicit roundings preserve the SDK's numerical contract;
 * there is no instruction decoder, guest register context, or generated code.
 * Compile with contraction disabled: only the explicit fused operations fuse.
 */
#include <dolphin/mtx.h>
#include "math_native.h"

static float mul(float a, float b) { return (float)((double)a * b); }
static float add(float a, float b) { return (float)((double)a + b); }
static float sub(float a, float b) { return (float)((double)a - b); }
static float mad(float a, float b, float c)
{ return (float)__builtin_fma((double)a, (double)b, (double)c); }
static float msub(float a, float b, float c)
{ return (float)__builtin_fma((double)a, (double)b, -(double)c); }
static float nmsub(float a, float b, float c)
{ return (float)__builtin_fma(-(double)a, (double)b, (double)c); }

/* The reciprocal-square-root estimate has more precision than a float. The
 * SDK refines it with single-precision multiplies whose second operand has
 * 25 significant bits. This is a numerical rule, independent of a CPU ISA. */
static double significand25(double x)
{
    union { double d; unsigned long long u; } v = { x };
    v.u = (v.u & 0xfffffffff8000000ULL) + (v.u & 0x8000000ULL);
    return v.d;
}
static float inverse_length(float squared)
{
    double estimate = mu_frsqrte(squared);
    float square = (float)(estimate * significand25(estimate));
    float half = (float)(estimate * 0.5);
    return mul(nmsub(square, squared, 3.0f), half);
}
static float length_squared(Vec v)
{ return add(mad(v.z, v.z, mul(v.x, v.x)), mul(v.y, v.y)); }

void PSMTXIdentity(Mtx m)
{
    for (int r=0;r<3;r++) for (int c=0;c<4;c++) m[r][c] = r==c ? 1.0f : 0.0f;
}
void PSMTXCopy(Mtx src, Mtx dst)
{
    for (int r=0;r<3;r++) for (int c=0;c<4;c++) dst[r][c]=src[r][c];
}
void PSMTXConcat(Mtx a, Mtx b, Mtx dst)
{
    Mtx out;
    for (int r=0;r<3;r++) for (int c=0;c<4;c++) {
        float v=mul(b[0][c],a[r][0]);
        v=mad(b[1][c],a[r][1],v);
        v=mad(b[2][c],a[r][2],v);
        if(c>=2) v=mad(c==3 ? 1.0f:0.0f,a[r][3],v);
        out[r][c]=v;
    }
    PSMTXCopy(out,dst);
}
void PSMTXTranspose(Mtx src, Mtx dst)
{
    Mtx out;
    for(int r=0;r<3;r++) {
        for(int c=0;c<3;c++) out[r][c]=src[c][r];
        out[r][3]=0.0f;
    }
    PSMTXCopy(out,dst);
}
u32 PSMTXInverse(Mtx a, Mtx dst)
{
    MU_MATH_AUDIT_IN(PSMTXInverse, a, 12);
    Mtx v;
    v[0][0]=msub(a[1][1],a[2][2],mul(a[2][1],a[1][2]));
    v[1][0]=msub(a[1][2],a[2][0],mul(a[2][2],a[1][0]));
    v[0][1]=msub(a[2][1],a[0][2],mul(a[0][1],a[2][2]));
    v[1][1]=msub(a[2][2],a[0][0],mul(a[0][2],a[2][0]));
    v[0][2]=msub(a[0][1],a[1][2],mul(a[1][1],a[0][2]));
    v[1][2]=msub(a[0][2],a[1][0],mul(a[1][2],a[0][0]));
    v[2][0]=msub(a[1][0],a[2][1],mul(a[1][1],a[2][0]));
    v[2][1]=msub(a[0][1],a[2][0],mul(a[0][0],a[2][1]));
    v[2][2]=msub(a[0][0],a[1][1],mul(a[0][1],a[1][0]));
    float det=mad(a[2][0],v[0][2],mad(a[1][0],v[0][1],mul(a[0][0],v[0][0])));
    if(det==0.0f) return 0;
    float estimate=(float)mu_fres(det);
    float reciprocal=nmsub(det,mul(estimate,estimate),add(estimate,estimate));
    for(int r=0;r<3;r++) {
        for(int c=0;c<3;c++) v[r][c]=mul(v[r][c],reciprocal);
        v[r][3]=(float)__builtin_fma(-(double)v[r][2], (double)a[2][3],
            -(double)mad(v[r][1],a[1][3],mul(v[r][0],a[0][3])));
    }
    PSMTXCopy(v,dst);
    return 1;
}
void PSMTXRotTrig(Mtx m, char axis, float s, float c)
{
    switch(axis|0x20) {
    case 'x': PSMTXIdentity(m);m[1][1]=c;m[1][2]=-s;m[2][1]=s;m[2][2]=c;break;
    case 'y': PSMTXIdentity(m);m[0][0]=c;m[0][2]=s;m[2][0]=-s;m[2][2]=c;break;
    case 'z': PSMTXIdentity(m);m[0][0]=c;m[0][1]=-s;m[1][0]=s;m[1][1]=c;break;
    }
}
void PSMTXTrans(Mtx m, float x, float y, float z)
{ PSMTXIdentity(m);m[0][3]=x;m[1][3]=y;m[2][3]=z; }
void PSMTXScale(Mtx m, float x, float y, float z)
{ PSMTXIdentity(m);m[0][0]=x;m[1][1]=y;m[2][2]=z; }
void PSMTXMultVec(Mtx m, Vec* src, Vec* dst)
{
    MU_MATH_AUDIT_IN(PSMTXMultVec, m, 12);
    MU_MATH_AUDIT_IN(PSMTXMultVec, src, 3);
    Vec in=*src,out;
    float v[3];
    for(int r=0;r<3;r++)
        v[r]=add(mad(m[r][2],in.z,mul(m[r][0],in.x)),mad(m[r][3],1.0f,mul(m[r][1],in.y)));
    out.x=v[0];out.y=v[1];out.z=v[2];*dst=out;
}
void PSMTXMultVecSR(Mtx m, Vec* src, Vec* dst)
{
    MU_MATH_AUDIT_IN(PSMTXMultVecSR, m, 12);
    MU_MATH_AUDIT_IN(PSMTXMultVecSR, src, 3);
    Vec in=*src,out;
    float v[3];
    for(int r=0;r<3;r++) v[r]=mad(m[r][2],in.z,add(mul(m[r][0],in.x),mul(m[r][1],in.y)));
    out.x=v[0];out.y=v[1];out.z=v[2];*dst=out;
}
void PSVECAdd(Vec* a, Vec* b, Vec* dst)
{ Vec v={add(a->x,b->x),add(a->y,b->y),add(a->z,b->z)};*dst=v; }
void PSVECSubtract(Vec* a, Vec* b, Vec* dst)
{ Vec v={sub(a->x,b->x),sub(a->y,b->y),sub(a->z,b->z)};*dst=v; }
void PSVECScale(Vec* a, Vec* dst, float scale)
{ Vec v={mul(a->x,scale),mul(a->y,scale),mul(a->z,scale)};*dst=v; }
void PSVECNormalize(Vec* src, Vec* dst)
{ Vec v=*src;float inv=inverse_length(length_squared(v));PSVECScale(&v,dst,inv); }
float PSVECMag(Vec* src)
{
    float squared=length_squared(*src),inv=inverse_length(squared);
    return mul(squared,inv>=0.0f ? inv:squared);
}
float PSVECDotProduct(Vec* a, Vec* b)
{ return add(mad(a->x,b->x,mul(a->y,b->y)),mul(a->z,b->z)); }
void PSVECCrossProduct(Vec* a, Vec* b, Vec* dst)
{
    Vec v={msub(a->y,b->z,mul(b->y,a->z)),
           -msub(a->x,b->z,mul(b->x,a->z)),
           -msub(a->y,b->x,mul(b->y,a->x))};
    *dst=v;
}
void PSMTXRotAxisRad(Mtx m, Vec* axis, float angle)
{
    MU_MATH_AUDIT_IN(PSMTXRotAxisRad, axis, 3);
    MU_MATH_AUDIT_IN(PSMTXRotAxisRad, &angle, 1);
    extern float sinf(float),cosf(float);
    float s=sinf(angle),c=cosf(angle),t=sub(1.0f,c);
    Vec n;PSVECNormalize(axis,&n);
    float tx=mul(n.x,t),ty=mul(n.y,t),tz=mul(n.z,t);
    float xy=mul(tx,n.y),xz=mul(tx,n.z),yz=mul(ty,n.z);
    float sx=mul(n.x,s),sy=mul(n.y,s);
    m[0][0]=add(mul(tx,n.x),c);m[0][1]=nmsub(n.z,s,xy);m[0][2]=add(xz,sy);m[0][3]=0;
    m[1][0]=mad(n.z,s,xy);m[1][1]=add(mul(ty,n.y),c);m[1][2]=add(-sx,yz);m[1][3]=0;
    m[2][0]=sub(xz,sy);m[2][1]=add(sx,yz);m[2][2]=add(mul(tz,n.z),c);m[2][3]=0;
}
void PSMTXQuat(Mtx m, Quaternion* q)
{
    MU_MATH_AUDIT_IN(PSMTXQuat, q, 4);
    float x=q->x,y=q->y,z=q->z,w=q->w;
    float xx=mul(x,x),yy=mul(y,y),zz=mul(z,z),ww=mul(w,w);
    float sum=add(mad(z,z,xx),mad(w,w,yy));
    float estimate=(float)mu_fres(sum);
    float scale=mul(mul(estimate,nmsub(sum,estimate,2.0f)),2.0f);
    float wz=mul(z,w),wy=mul(y,w),wx=mul(x,w);
    float xzwy=mad(x,z,wy),yzwx=mad(y,z,wx);
    m[0][0]=nmsub(add(yy,zz),scale,1.0f);m[0][1]=mul(msub(x,y,wz),scale);m[0][2]=mul(xzwy,scale);m[0][3]=0;
    m[1][0]=mul(mad(x,y,wz),scale);m[1][1]=nmsub(mad(z,z,xx),scale,1.0f);m[1][2]=mul(nmsub(wx,2.0f,yzwx),scale);m[1][3]=0;
    m[2][0]=mul(nmsub(wy,2.0f,xzwy),scale);m[2][1]=mul(yzwx,scale);m[2][2]=nmsub(add(xx,yy),scale,1.0f);m[2][3]=0;
}
