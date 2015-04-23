/*
 * This file was originally part of the GADGET3 code developed by
 * Volker Springel (volker.springel@h-its.org).
 */

#ifndef _VECTOR_H_
#define _VECTOR_H_

#if defined(VECTOR_SSE)
#include "vector_sse2.h"
#elif defined(VECTOR_AVX)
#include "vector_avx.h"
#elif defined(VECTOR_VSX)
#include "vector_vsx.h"
#elif defined(VECTOR_QPX)
#include "vector_qpx.h"
#else

// generic scalar code

#ifdef DOUBLEPRECISION

typedef ALIGN(32) union {
    unsigned long long i[4];
    double d[4];
} t_vector;

#else

typedef ALIGN(16) union {
    unsigned int i[4];
    float d[4];
} t_vector;

#endif

// read the first 3 components of a vector, 4th component is undefined
static inline void LOAD_VECTOR3(MyFloat * src,t_vector * v)
{
    v->d[0] = src[0];
    v->d[1] = src[1];
    v->d[2] = src[2];
}

// read all 4 components of a vector
static inline void LOAD_VECTOR4(MyFloat * src,t_vector * v)
{
    v->d[0] = src[0];
    v->d[1] = src[1];
    v->d[2] = src[2];
    v->d[3] = src[3];
}

// stores only the first 3 components of a vector (avoid, because it's slow)
static inline void STORE_VECTOR3(MyFloat * dst,t_vector * v)
{
    dst[0] = v->d[0];
    dst[1] = v->d[1];
    dst[2] = v->d[2];
}

// stores all 4 components of a vector (fast)
static inline void STORE_VECTOR4(MyFloat * dst,t_vector * v)
{
    dst[0] = v->d[0];
    dst[1] = v->d[1];
    dst[2] = v->d[2];
    dst[3] = v->d[3];
}

// initializes first 3 components of a vector (4th component becomes undefined)
static inline void SET_VECTOR3(MyFloat a,t_vector * v)
{
    v->d[0] = a;
    v->d[1] = a;
    v->d[2] = a;
}

// initializes all 4 components of a vector with the same scalar value
static inline void SET_VECTOR4(MyFloat a,t_vector * v)
{
    v->d[0] = a;
    v->d[1] = a;
    v->d[2] = a;
    v->d[3] = a;
}

// initializes first 3 components of a vector (4th component becomes undefined)
static inline void INIT_VECTOR3(MyFloat a0,MyFloat a1,MyFloat a2,t_vector * v)
{
    v->d[0] = a0;
    v->d[1] = a1;
    v->d[2] = a2;
}

// adds the first 3 components of 2 vectors (4th component undefined)
static inline void ADD_VECTOR3(t_vector * a,t_vector * b,t_vector * result)
{
    result->d[0] = a->d[0]+b->d[0];
    result->d[1] = a->d[1]+b->d[1];
    result->d[2] = a->d[2]+b->d[2];
}

// multiplies the first 3 components of 2 vectors (4th component undefined)
static inline void MUL_VECTOR3(t_vector * a,t_vector * b,t_vector * result)
{
    result->d[0] = a->d[0] * b->d[0];
    result->d[1] = a->d[1] * b->d[1];
    result->d[2] = a->d[2] * b->d[2];
}

// multiplies the first 3 components of 2 vectors (4th component undefined)
static inline void SCALE_VECTOR3(MyFloat a,t_vector * b,t_vector * v)
{
    v->d[0] = a * b->d[0];
    v->d[1] = a * b->d[1];
    v->d[2] = a * b->d[2];
}

// returns a value >0, if a(i) < b(i) for any i=1..3
static inline int ANY_COMP_LT_VECTOR3(t_vector * a,t_vector * b,int mask)
{
    return (a->d[0] < b->d[0]) | (a->d[1] < b->d[1]) | (a->d[2] < b->d[2]);
}

// returns 0, if a(i) < b(i) for any i=1..3
static inline int ALL_COMP_LT_VECTOR3(t_vector * a,t_vector * b,int mask)
{
    return (a->d[0] < b->d[0]) & (a->d[1] < b->d[1]) & (a->d[2] < b->d[2]);
}

// returns the L2 norm of the first 3 components of a vector
static inline MyDouble L2NORM_VECTOR3(t_vector * v)
{
    return v->d[0] * v->d[0] + v->d[1] * v->d[1] + v->d[2] * v->d[2];
}

#endif

#ifdef DO_NOT_BRACH_IF

// on the Power platform it is more efficient to compute all conditions first
// and then perform a single branch
// On current Intel processors this is equally fast

inline static int ngb_check_node(struct NODE * cur,t_vector * v2,t_vector * box,t_vector * hbox,MyFloat hsml)
{
    MyDouble dist  = hsml + 0.5*cur->len;
    MyDouble dist2 = dist + FACT1*cur->len;
    MyDouble d2,dx,dy,dz;
#ifdef PERIODIC
    MyDouble xtmp;
#endif
    int node = cur->u.d.sibling; // in case the node can be discarded
    
    dx = NGB_PERIODIC_LONG_X(cur->center[0] - v2->d[0], cur->center[1] - v2->d[1], cur->center[2] - v2->d[2],-1);
    dy = NGB_PERIODIC_LONG_Y(cur->center[0] - v2->d[0], cur->center[1] - v2->d[1], cur->center[2] - v2->d[2],-1);
    dz = NGB_PERIODIC_LONG_Z(cur->center[0] - v2->d[0], cur->center[1] - v2->d[1], cur->center[2] - v2->d[2],-1);
    d2 = dx * dx + dy * dy + dz * dz;
    // now test against the minimal sphere enclosing everything
    if ((dx <= dist) & (dy <= dist) & (dz <= dist) & (d2 <= dist2 * dist2)) node = cur->u.d.nextnode;     // ok, we need to open the node
    return node;
}

#else

// On older Intel and AMD processors it seems better to avoid the computations and branch early

inline static int ngb_check_node(struct NODE * cur,t_vector * v2,t_vector * box,t_vector * hbox,MyFloat hsml)
{
    MyDouble dist  = hsml + 0.5*cur->len;
    MyDouble d2,dx,dy,dz;
#ifdef PERIODIC
    MyDouble xtmp;
#endif
    int node = cur->u.d.sibling; // in case the node can be discarded
    
    dx = NGB_PERIODIC_LONG_X(cur->center[0] - v2->d[0], cur->center[1] - v2->d[1], cur->center[2] - v2->d[2],-1);
    if (dx <= dist) {
        dy = NGB_PERIODIC_LONG_Y(cur->center[0] - v2->d[0], cur->center[1] - v2->d[1], cur->center[2] - v2->d[2],-1);
        if (dy <= dist) {
            dz = NGB_PERIODIC_LONG_Z(cur->center[0] - v2->d[0], cur->center[1] - v2->d[1], cur->center[2] - v2->d[2],-1);
            if (dz <= dist) {
                // now test against the minimal sphere enclosing everything
                dist += FACT1 * cur->len;
                d2 = dx * dx + dy * dy + dz * dz;
                if (d2 <= dist * dist) node = cur->u.d.nextnode;     // ok, we need to open the node
            }
        }
    }
    return node;
}

#endif

#endif
