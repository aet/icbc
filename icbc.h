// icbc.h v1.02
// A High Quality BC1 Encoder by Ignacio Castano <castano@gmail.com>.
// 
// LICENSE:
//  MIT license at the end of this file.

#ifndef ICBC_H
#define ICBC_H

namespace icbc {

    void init_dxt1();

    float compress_dxt1(const float input_colors[16 * 4], const float input_weights[16], const float color_weights[3], bool three_color_mode, bool hq, void * output);
    float compress_dxt1_fast(const float input_colors[16 * 4], const float input_weights[16], const float color_weights[3], void * output);
    void compress_dxt1_fast(const unsigned char input_colors[16 * 4], void * output);

    enum Decoder {
        Decoder_D3D10 = 0,
        Decoder_NVIDIA = 1,
        Decoder_AMD = 2
    };

    float evaluate_dxt1_error(const unsigned char rgba_block[16 * 4], const void * block, Decoder decoder = Decoder_D3D10);

}

#endif // ICBC_H

#ifdef ICBC_IMPLEMENTATION

// Instruction level support must be chosen at compile time setting ICBC_USE_SPMD to one of these values:
#define ICBC_FLOAT  0
#define ICBC_SSE2   1
#define ICBC_SSE41  2
#define ICBC_AVX1   3
#define ICBC_AVX2   4
#define ICBC_AVX512 5
#define ICBC_NEON   -1

// AVX does not require FMA, and depending on whether it's Intel or AMD you may have FMA3 or FMA4. What a mess.
//#define ICBC_USE_FMA 3
//#define ICBC_USE_FMA 4

// Apparently rcp is not deterministic (different precision on Intel and AMD), enable if you don't care about that for small performance boost.
//#define ICBC_USE_RCP 1

#ifndef ICBC_USE_SPMD
#define ICBC_USE_SPMD 5          // SIMD version. (FLOAT=0, SSE2=1, SSE41=2, AVX1=3, AVX2=4, AVX512=5, NEON=6)
#endif

#if ICBC_USE_SPMD == ICBC_AVX2
#define ICBC_USE_AVX2_PERMUTE2 1    // Using permutevar8x32 and bitops.
#define ICBC_USE_AVX2_PERMUTE 0     // Using blendv and permutevar8x32.
#define ICBC_USE_AVX2_GATHER 0      // Using gathers for SAT lookup.
#endif

#if ICBC_USE_SPMD == ICBC_AVX512
#define ICBC_USE_AVX512_PERMUTE 1
#endif


#ifndef ICBC_DECODER
#define ICBC_DECODER 0       // 0 = d3d10, 1 = nvidia, 2 = amd
#endif


#if ICBC_USE_SPMD >= ICBC_SSE2
#include <emmintrin.h>
#endif 

#if ICBC_USE_SPMD >= ICBC_SSE41
#include <smmintrin.h>
#endif

#if ICBC_USE_SPMD >= ICBC_AVX1
#include <immintrin.h>
#endif

#if ICBC_USE_SPMD >= ICBC_AVX512 && _MSC_VER
#include <zmmintrin.h>
#endif

#if ICBC_USE_SPMD == ICBC_NEON
#include <arm_neon.h>
#endif


// Some testing knobs:
#define ICBC_FAST_CLUSTER_FIT 0     // This ignores input weights for a moderate speedup. (currently broken)
#define ICBC_PERFECT_ROUND 0        // Enable perfect rounding in scalar code path only.
#define ICBC_USE_SAT 1              // Use summed area tables.

#include <stdint.h>
#include <stdlib.h> // abs
#include <string.h> // memset
#include <math.h>   // floorf
#include <float.h>  // FLT_MAX

#ifndef ICBC_ASSERT
#if _DEBUG
#define ICBC_ASSERT assert
#include <assert.h>
#else
#define ICBC_ASSERT(x)
#endif
#endif

namespace icbc {

///////////////////////////////////////////////////////////////////////////////////////////////////
// Basic Templates

template <typename T> inline void swap(T & a, T & b) {
    T temp(a);
    a = b;
    b = temp;
}

template <typename T> inline T max(const T & a, const T & b) {
    return (b < a) ? a : b;
}

template <typename T> inline T min(const T & a, const T & b) {
    return (a < b) ? a : b;
}

template <typename T> inline T clamp(const T & x, const T & a, const T & b) {
    return min(max(x, a), b);
}

template <typename T> inline T square(const T & a) {
    return a * a;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Basic Types

typedef uint8_t uint8;
typedef int8_t int8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint32_t uint;


struct Color16 {
    union {
        struct {
            uint16 b : 5;
            uint16 g : 6;
            uint16 r : 5;
        };
        uint16 u;
    };
};

struct Color32 {
    union {
        struct {
            uint8 b, g, r, a;
        };
        uint32 u;
    };
};

struct BlockDXT1 {
    Color16 col0;
    Color16 col1;
    uint32 indices;
};


struct Vector3 {
    float x;
    float y;
    float z;

    inline void operator+=(Vector3 v) {
        x += v.x; y += v.y; z += v.z;
    }
    inline void operator*=(Vector3 v) {
        x *= v.x; y *= v.y; z *= v.z;
    }
    inline void operator*=(float s) {
        x *= s; y *= s; z *= s;
    }
};

struct Vector4 {
    union {
        struct {
            float x, y, z, w;
        };
        Vector3 xyz;
    };
};


inline Vector3 operator*(Vector3 v, float s) {
    return { v.x * s, v.y * s, v.z * s };
}

inline Vector3 operator*(float s, Vector3 v) {
    return { v.x * s, v.y * s, v.z * s };
}

inline Vector3 operator*(Vector3 a, Vector3 b) {
    return { a.x * b.x, a.y * b.y, a.z * b.z };
}

inline float dot(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vector3 operator+(Vector3 a, Vector3 b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

inline Vector3 operator-(Vector3 a, Vector3 b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline Vector3 operator/(Vector3 v, float s) {
    return { v.x / s, v.y / s, v.z / s };
}

inline float saturate(float x) {
    return clamp(x, 0.0f, 1.0f);
}

inline Vector3 saturate(Vector3 v) {
    return { saturate(v.x), saturate(v.y), saturate(v.z) };
}

inline Vector3 min(Vector3 a, Vector3 b) {
    return { min(a.x, b.x), min(a.y, b.y), min(a.z, b.z) };
}

inline Vector3 max(Vector3 a, Vector3 b) {
    return { max(a.x, b.x), max(a.y, b.y), max(a.z, b.z) };
}

inline Vector3 round(Vector3 v) {
    return { floorf(v.x+0.5f), floorf(v.y + 0.5f), floorf(v.z + 0.5f) };
}

inline Vector3 floor(Vector3 v) {
    return { floorf(v.x), floorf(v.y), floorf(v.z) };
}

inline bool operator==(const Vector3 & a, const Vector3 & b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

inline Vector3 scalar_to_vector3(float f) {
    return {f, f, f};
}

inline float lengthSquared(Vector3 v) {
    return dot(v, v);
}

inline bool equal(float a, float b, float epsilon = 0.0001) {
    // http://realtimecollisiondetection.net/blog/?p=89
    //return fabsf(a - b) < epsilon * max(1.0f, max(fabsf(a), fabsf(b)));
    return fabsf(a - b) < epsilon;
}

inline bool equal(Vector3 a, Vector3 b, float epsilon) {
    return equal(a.x, b.x, epsilon) && equal(a.y, b.y, epsilon) && equal(a.z, b.z, epsilon);
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// SPMD

#ifndef ICBC_ALIGN_16
#if __GNUC__
#   define ICBC_ALIGN_16 __attribute__ ((__aligned__ (16)))
#else // _MSC_VER
#   define ICBC_ALIGN_16 __declspec(align(16))
#endif
#endif

#if __GNUC__
#define ICBC_FORCEINLINE inline __attribute__((always_inline))
#else
#define ICBC_FORCEINLINE __forceinline
#endif

#if ICBC_USE_SPMD == ICBC_FLOAT  // Purely scalar version.

#define VEC_SIZE 1

using VFloat = float;
using VMask = bool;

ICBC_FORCEINLINE float & lane(VFloat & v, int i) { return v; }
ICBC_FORCEINLINE VFloat vzero() { return 0.0f; }
ICBC_FORCEINLINE VFloat vbroadcast(float x) { return x; }
ICBC_FORCEINLINE VFloat vload(const float * ptr) { return *ptr; }
ICBC_FORCEINLINE VFloat vrcp(VFloat a) { return 1.0f / a; }
ICBC_FORCEINLINE VFloat vmad(VFloat a, VFloat b, VFloat c) { return a * b + c; }
ICBC_FORCEINLINE VFloat vsaturate(VFloat a) { return min(max(a, 0.0f), 1.0f); }
ICBC_FORCEINLINE VFloat vround(VFloat a) { return float(int(a + 0.5f)); }
ICBC_FORCEINLINE VFloat lane_id() { return 0; }
ICBC_FORCEINLINE VFloat vselect(VMask mask, VFloat a, VFloat b) { return mask ? b : a; }
ICBC_FORCEINLINE bool all(VMask m) { return m; }
ICBC_FORCEINLINE bool any(VMask m) { return m; }


#elif ICBC_USE_SPMD == ICBC_SSE2 || ICBC_USE_SPMD == ICBC_SSE41

#define VEC_SIZE 4

#if __GNUC__
union VFloat {
    __m128 v;
    float m128_f32[VEC_SIZE];

    VFloat() {}
    VFloat(__m128 v) : v(v) {}
    operator __m128 & () { return v; }
};
union VMask {
    __m128 m;

    VMask() {}
    VMask(__m128 m) : m(m) {}
    operator __m128 & () { return m; }
};
#else
using VFloat = __m128;
using VMask = __m128;
#endif

ICBC_FORCEINLINE float & lane(VFloat & v, int i) {
    return v.m128_f32[i];
}

ICBC_FORCEINLINE VFloat vzero() {
    return _mm_setzero_ps();
}

ICBC_FORCEINLINE VFloat vbroadcast(float x) {
    return _mm_set1_ps(x);
}

ICBC_FORCEINLINE VFloat vload(const float * ptr) {
    return _mm_load_ps(ptr);
}

ICBC_FORCEINLINE VFloat operator+(VFloat a, VFloat b) {
    return _mm_add_ps(a, b);
}

ICBC_FORCEINLINE VFloat operator-(VFloat a, VFloat b) {
    return _mm_sub_ps(a, b);
}

ICBC_FORCEINLINE VFloat operator*(VFloat a, VFloat b) {
    return _mm_mul_ps(a, b);
}

ICBC_FORCEINLINE VFloat vrcp(VFloat a) {
#if ICBC_USE_RCP
    VFloat res = _mm_rcp_ps(a);
    auto muls = _mm_mul_ps(a, _mm_mul_ps(res, res));
    return _mm_sub_ps(_mm_add_ps(res, res), muls);
#else
    return _mm_div_ps(vbroadcast(1.0f), a);
#endif
}

// a*b+c
ICBC_FORCEINLINE VFloat vmad(VFloat a, VFloat b, VFloat c) {
    return a * b + c;
}

ICBC_FORCEINLINE VFloat vsaturate(VFloat a) {
    auto zero = _mm_setzero_ps();
    auto one = _mm_set1_ps(1.0f);
    return _mm_min_ps(_mm_max_ps(a, zero), one);
}

ICBC_FORCEINLINE VFloat vround(VFloat a) {
#if ICBC_USE_SPMD == ICBC_SSE41
    return _mm_round_ps(a, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
#else
    return _mm_cvtepi32_ps(_mm_cvttps_epi32(a + vbroadcast(0.5f)));
#endif
}

ICBC_FORCEINLINE VFloat vtruncate(VFloat a) {
#if ICBC_USE_SPMD == ICBC_SSE41
    return _mm_round_ps(a, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
#else
    return _mm_cvtepi32_ps(_mm_cvttps_epi32(a));
#endif
}

ICBC_FORCEINLINE VFloat lane_id() {
    return _mm_set_ps(3, 2, 1, 0);
}

ICBC_FORCEINLINE VMask operator> (VFloat A, VFloat B) { return _mm_cmpgt_ps(A, B); }
ICBC_FORCEINLINE VMask operator>=(VFloat A, VFloat B) { return _mm_cmpge_ps(A, B); }
ICBC_FORCEINLINE VMask operator< (VFloat A, VFloat B) { return _mm_cmplt_ps(A, B); }
ICBC_FORCEINLINE VMask operator<=(VFloat A, VFloat B) { return _mm_cmple_ps(A, B); }

ICBC_FORCEINLINE VMask operator| (VMask A, VMask B) { return _mm_or_ps(A, B); }
ICBC_FORCEINLINE VMask operator& (VMask A, VMask B) { return _mm_and_ps(A, B); }
ICBC_FORCEINLINE VMask operator^ (VMask A, VMask B) { return _mm_xor_ps(A, B); }

// mask ? b : a
ICBC_FORCEINLINE VFloat vselect(VMask mask, VFloat a, VFloat b) {
#if ICBC_USE_SPMD == ICBC_SSE41
    return _mm_blendv_ps(a, b, mask);
#else
    return _mm_or_ps(_mm_andnot_ps(mask, a), _mm_and_ps(mask, b));
#endif
}

ICBC_FORCEINLINE bool all(VMask m) {
    int value = _mm_movemask_ps(m);
    return value == 0x7;
}

ICBC_FORCEINLINE bool any(VMask m) {
    int value = _mm_movemask_ps(m);
    return value != 0;
}


#elif ICBC_USE_SPMD == ICBC_AVX1 || ICBC_USE_SPMD == ICBC_AVX2

#define VEC_SIZE 8

#if __GNUC__
union VFloat {
    __m256 v;
    float m256_f32[VEC_SIZE];

    VFloat() {}
    VFloat(__m256 v) : v(v) {}
    operator __m256 & () { return v; }
};
union VMask {
    __m256 m;

    VMask() {}
    VMask(__m256 m) : m(m) {}
    operator __m256 & () { return m; }
};
#else
using VFloat = __m256;
using VMask = __m256;   // Emulate mask vector using packed float.
#endif

ICBC_FORCEINLINE float & lane(VFloat & v, int i) {
    return v.m256_f32[i];
}

ICBC_FORCEINLINE VFloat vzero() {
    return _mm256_setzero_ps();
}

ICBC_FORCEINLINE VFloat vbroadcast(float a) {
    return _mm256_broadcast_ss(&a);
}

ICBC_FORCEINLINE VFloat vload(const float * ptr) {
    return _mm256_load_ps(ptr);
}

ICBC_FORCEINLINE VFloat operator+(VFloat a, VFloat b) {
    return _mm256_add_ps(a, b);
}

ICBC_FORCEINLINE VFloat operator-(VFloat a, VFloat b) {
    return _mm256_sub_ps(a, b);
}

ICBC_FORCEINLINE VFloat operator*(VFloat a, VFloat b) {
    return _mm256_mul_ps(a, b);
}

ICBC_FORCEINLINE VFloat vrcp(VFloat a) {
#if ICBC_USE_RCP
    __m256 res = _mm256_rcp_ps(a);
    __m256 muls = _mm256_mul_ps(a, _mm256_mul_ps(res, res));
    return _mm256_sub_ps(_mm256_add_ps(res, res), muls);
#else
    return _mm256_div_ps(vbroadcast(1.0f), a);
#endif
}

// a*b+c
ICBC_FORCEINLINE VFloat vmad(VFloat a, VFloat b, VFloat c) {
#if ICBC_USE_FMA == 3
    return _mm256_fmadd_ps(a, b, c);
#elif ICBC_USE_FMA == 4
    return _mm256_macc_ps(a, b, c);
#else
    return ((a * b) + c);
#endif
}

ICBC_FORCEINLINE VFloat vsaturate(VFloat a) {
    __m256 zero = _mm256_setzero_ps();
    __m256 one = _mm256_set1_ps(1.0f);
    return _mm256_min_ps(_mm256_max_ps(a, zero), one);
}

ICBC_FORCEINLINE VFloat vround(VFloat a) {
    return _mm256_round_ps(a, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
}

ICBC_FORCEINLINE VFloat lane_id() {
    return _mm256_set_ps(7, 6, 5, 4, 3, 2, 1, 0);
}

ICBC_FORCEINLINE VMask operator> (VFloat A, VFloat B) { return _mm256_cmp_ps(A, B, _CMP_GT_OQ); }
ICBC_FORCEINLINE VMask operator>=(VFloat A, VFloat B) { return _mm256_cmp_ps(A, B, _CMP_GE_OQ); }
ICBC_FORCEINLINE VMask operator< (VFloat A, VFloat B) { return _mm256_cmp_ps(A, B, _CMP_LT_OQ); }
ICBC_FORCEINLINE VMask operator<=(VFloat A, VFloat B) { return _mm256_cmp_ps(A, B, _CMP_LE_OQ); }

ICBC_FORCEINLINE VMask operator| (VMask A, VMask B) { return _mm256_or_ps(A, B); }
ICBC_FORCEINLINE VMask operator& (VMask A, VMask B) { return _mm256_and_ps(A, B); }
ICBC_FORCEINLINE VMask operator^ (VMask A, VMask B) { return _mm256_xor_ps(A, B); }

// mask ? b : a
ICBC_FORCEINLINE VFloat vselect(VMask mask, VFloat a, VFloat b) {
    return _mm256_blendv_ps(a, b, mask);
}

ICBC_FORCEINLINE bool all(VMask m) {
    __m256 zero = _mm256_setzero_ps();
    return _mm256_testc_ps(_mm256_cmp_ps(zero, zero, _CMP_EQ_UQ), m) == 0;
}

ICBC_FORCEINLINE bool any(VMask m) {
    return _mm256_testz_ps(m, m) == 0;
}


#elif ICBC_USE_SPMD == ICBC_AVX512

#define VEC_SIZE 16

#if __GNUC__
union VFloat {
    __m512 v;
    float m512_f32[VEC_SIZE];

    VFloat() {}
    VFloat(__m512 v) : v(v) {}
    operator __m512 & () { return v; }
};
#else
using VFloat = __m512;
#endif
struct VMask { __mmask16 m; };

ICBC_FORCEINLINE float & lane(VFloat & v, int i) {
    return v.m512_f32[i];
}

ICBC_FORCEINLINE VFloat vzero() {
    return _mm512_setzero_ps();
}

ICBC_FORCEINLINE VFloat vbroadcast(float a) {
    return _mm512_set1_ps(a);
}

ICBC_FORCEINLINE VFloat vload(const float * ptr) {
    return _mm512_load_ps(ptr);
}

ICBC_FORCEINLINE VFloat vload(VMask mask, const float * ptr) {
    return _mm512_mask_load_ps(_mm512_undefined(), mask.m, ptr);
}

ICBC_FORCEINLINE VFloat vload(VMask mask, const float * ptr, float fallback) {
    return _mm512_mask_load_ps(_mm512_set1_ps(fallback), mask.m, ptr);
}

ICBC_FORCEINLINE VFloat operator+(VFloat a, VFloat b) {
    return _mm512_add_ps(a, b);
}

ICBC_FORCEINLINE VFloat operator-(VFloat a, VFloat b) {
    return _mm512_sub_ps(a, b);
}

ICBC_FORCEINLINE VFloat operator*(VFloat a, VFloat b) {
    return _mm512_mul_ps(a, b);
}

ICBC_FORCEINLINE VFloat vrcp(VFloat a) {
    // @@ Use an aproximation?
    return _mm512_div_ps(vbroadcast(1.0f), a);
}

// a*b+c
ICBC_FORCEINLINE VFloat vmad(VFloat a, VFloat b, VFloat c) {
    return _mm512_fmadd_ps(a, b, c);
}

ICBC_FORCEINLINE VFloat vsaturate(VFloat a) {
    auto zero = _mm512_setzero_ps();
    auto one = _mm512_set1_ps(1.0f);
    return _mm512_min_ps(_mm512_max_ps(a, zero), one);
}

ICBC_FORCEINLINE VFloat vround(VFloat a) {
    return _mm512_roundscale_ps(a, _MM_FROUND_TO_NEAREST_INT);
}

ICBC_FORCEINLINE VFloat lane_id() {
    return _mm512_set_ps(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
}

ICBC_FORCEINLINE VMask operator> (VFloat A, VFloat B) { return { _mm512_cmp_ps_mask(A, B, _CMP_GT_OQ) }; }
ICBC_FORCEINLINE VMask operator>=(VFloat A, VFloat B) { return { _mm512_cmp_ps_mask(A, B, _CMP_GE_OQ) }; }
ICBC_FORCEINLINE VMask operator< (VFloat A, VFloat B) { return { _mm512_cmp_ps_mask(A, B, _CMP_LT_OQ) }; }
ICBC_FORCEINLINE VMask operator<=(VFloat A, VFloat B) { return { _mm512_cmp_ps_mask(A, B, _CMP_LE_OQ) }; }

ICBC_FORCEINLINE VMask operator! (VMask A) { return { _mm512_knot(A.m) }; }
ICBC_FORCEINLINE VMask operator| (VMask A, VMask B) { return { _mm512_kor(A.m, B.m) }; }
ICBC_FORCEINLINE VMask operator& (VMask A, VMask B) { return { _mm512_kand(A.m, B.m) }; }
ICBC_FORCEINLINE VMask operator^ (VMask A, VMask B) { return { _mm512_kxor(A.m, B.m) }; }

// mask ? b : a
ICBC_FORCEINLINE VFloat vselect(VMask mask, VFloat a, VFloat b) {
    return _mm512_mask_blend_ps(mask.m, a, b);
}

ICBC_FORCEINLINE bool all(VMask mask) {
    return mask.m == 0xFFFFFFFF;
}

ICBC_FORCEINLINE bool any(VMask mask) {
    return mask.m != 0;
}

#elif ICBC_USE_SPMD == ICBC_NEON

#define VEC_SIZE 4

using VFloat = float32x4_t;
using VMask = uint32x4_t;

ICBC_FORCEINLINE float & lane(VFloat & v, int i) {
    // @@
}

ICBC_FORCEINLINE VFloat vzero() {
    // @@
}

ICBC_FORCEINLINE VFloat vbroadcast(float a) {
    return vld1q_f32(&a);
}

ICBC_FORCEINLINE VFloat vload(const float * ptr) {
    // @@
}

ICBC_FORCEINLINE VFloat operator+(VFloat a, VFloat b) {
    return vaddq_f32(a, b);
}

ICBC_FORCEINLINE VFloat operator-(VFloat a, VFloat b) {
    return vsubq_f32(a, b);
}

ICBC_FORCEINLINE VFloat operator*(VFloat a, VFloat b) {
    return vmulq_f32(a, b);
}

ICBC_FORCEINLINE VFloat vrcp(VFloat a) {
#if ICBC_USE_RCP
    return vrecpeq_f32(a);  // @@ What's the precision of this?
#else
    return vdiv_f32(vbroadcast(1.0f), a);
#endif
}

// a*b+c
ICBC_FORCEINLINE VFloat vmad(VFloat a, VFloat b, VFloat c) {
    return a*b+c; // @@
}

ICBC_FORCEINLINE VFloat vsaturate(VFloat a) {
    return vminq_f32(vmaxq_f32(a, vzero()), vbroadcast(1));
}

ICBC_FORCEINLINE VFloat vround(VFloat a) {
    return vrndq_f32(a);
}

ICBC_FORCEINLINE VFloat lane_id() {
    // @@
}

ICBC_FORCEINLINE VMask operator> (VFloat A, VFloat B) { return { _mm512_cmp_ps_mask(A, B, _CMP_GT_OQ) }; }
ICBC_FORCEINLINE VMask operator>=(VFloat A, VFloat B) { return { _mm512_cmp_ps_mask(A, B, _CMP_GE_OQ) }; }
ICBC_FORCEINLINE VMask operator< (VFloat A, VFloat B) { return { _mm512_cmp_ps_mask(A, B, _CMP_LT_OQ) }; }
ICBC_FORCEINLINE VMask operator<=(VFloat A, VFloat B) { return { _mm512_cmp_ps_mask(A, B, _CMP_LE_OQ) }; }

ICBC_FORCEINLINE VMask operator! (VMask A) { return { _mm512_knot(A.m) }; }
ICBC_FORCEINLINE VMask operator| (VMask A, VMask B) { return { _mm512_kor(A.m, B.m) }; }
ICBC_FORCEINLINE VMask operator& (VMask A, VMask B) { return { _mm512_kand(A.m, B.m) }; }
ICBC_FORCEINLINE VMask operator^ (VMask A, VMask B) { return { _mm512_kxor(A.m, B.m) }; }

// mask ? b : a
ICBC_FORCEINLINE VFloat vselect(VMask mask, VFloat a, VFloat b) {
    return vbslq_f32(mask, a, b);
}

ICBC_FORCEINLINE bool all(VMask mask) {
    // @@
}

ICBC_FORCEINLINE bool any(VMask mask) {
    // @@
}

#endif // ICBC_NEON

#if ICBC_USE_SPMD

struct VVector3 {
    VFloat x;
    VFloat y;
    VFloat z;
};

ICBC_FORCEINLINE VVector3 vbroadcast(Vector3 v) {
    VVector3 v8;
    v8.x = vbroadcast(v.x);
    v8.y = vbroadcast(v.y);
    v8.z = vbroadcast(v.z);
    return v8;
}

ICBC_FORCEINLINE VVector3 vbroadcast(float x, float y, float z) {
    VVector3 v8;
    v8.x = vbroadcast(x);
    v8.y = vbroadcast(y);
    v8.z = vbroadcast(z);
    return v8;
}


ICBC_FORCEINLINE VVector3 operator+(VVector3 a, VVector3 b) {
    VVector3 v8;
    v8.x = (a.x + b.x);
    v8.y = (a.y + b.y);
    v8.z = (a.z + b.z);
    return v8;
}

ICBC_FORCEINLINE VVector3 operator-(VVector3 a, VVector3 b) {
    VVector3 v8;
    v8.x = (a.x - b.x);
    v8.y = (a.y - b.y);
    v8.z = (a.z - b.z);
    return v8;
}

ICBC_FORCEINLINE VVector3 operator*(VVector3 a, VVector3 b) {
    VVector3 v8;
    v8.x = (a.x * b.x);
    v8.y = (a.y * b.y);
    v8.z = (a.z * b.z);
    return v8;
}

ICBC_FORCEINLINE VVector3 operator*(VVector3 a, VFloat b) {
    VVector3 v8;
    v8.x = (a.x * b);
    v8.y = (a.y * b);
    v8.z = (a.z * b);
    return v8;
}

ICBC_FORCEINLINE VVector3 vmad(VVector3 a, VVector3 b, VVector3 c) {
    VVector3 v8;
    v8.x = vmad(a.x, b.x, c.x);
    v8.y = vmad(a.y, b.y, c.y);
    v8.z = vmad(a.z, b.z, c.z);
    return v8;
}

ICBC_FORCEINLINE VVector3 vmad(VVector3 a, VFloat b, VVector3 c) {
    VVector3 v8;
    v8.x = vmad(a.x, b, c.x);
    v8.y = vmad(a.y, b, c.y);
    v8.z = vmad(a.z, b, c.z);
    return v8;
}

ICBC_FORCEINLINE VVector3 vsaturate(VVector3 v) {
    VVector3 r;
    r.x = vsaturate(v.x);
    r.y = vsaturate(v.y);
    r.z = vsaturate(v.z);
    return r;
}

/*static const float midpoints5[32];
static const float midpoints6[64];

// @@ How can we vectorize this? It requires a 32 or 64 table lookup.
/*ICBC_FORCEINLINE VFloat vround5(VFloat x) {
    VFloat q = vfloor(x);
    for (int i = 0; i < VEC_SIZE; i++) {
        lane(q, i) += (lane(x, i) > midpoints5[int(lane(q, i))]);
    }
    return q;
}

ICBC_FORCEINLINE VFloat vround6(VFloat x) {
    VFloat q = vfloor(x);
    for (int i = 0; i < VEC_SIZE; i++) {
        q[i] += (x[i] > midpoints6[int(q[i])]);
    }
    return q;
}*/

ICBC_FORCEINLINE VVector3 vround_ept(VVector3 v) {
    const VFloat rb_scale = vbroadcast(31.0f);
    const VFloat rb_inv_scale = vbroadcast(1.0f / 31.0f);
    const VFloat g_scale = vbroadcast(63.0f);
    const VFloat g_inv_scale = vbroadcast(1.0f / 63.0f);

    VVector3 r;
#if ICBC_PERFECT_ROUND    
    r.x = vround5(v.x * rb_scale) * rb_inv_scale;
    r.y = vround6(v.y * g_scale) * g_inv_scale;
    r.z = vround5(v.z * rb_scale) * rb_inv_scale;
#else
    r.x = vround(v.x * rb_scale) * rb_inv_scale;
    r.y = vround(v.y * g_scale) * g_inv_scale;
    r.z = vround(v.z * rb_scale) * rb_inv_scale;
#endif
    return r;
}

ICBC_FORCEINLINE VFloat vdot(VVector3 a, VVector3 b) {
    VFloat r;
    r = a.x * b.x + a.y * b.y + a.z * b.z;
    return r;
}

// mask ? b : a
ICBC_FORCEINLINE VVector3 vselect(VMask mask, VVector3 a, VVector3 b) {
    VVector3 r;
    r.x = vselect(mask, a.x, b.x);
    r.y = vselect(mask, a.y, b.y);
    r.z = vselect(mask, a.z, b.z);
    return r;
}

#endif // ICBC_USE_SPMD



///////////////////////////////////////////////////////////////////////////////////////////////////
// Color conversion functions.

static const float midpoints5[32] = {
    0.015686f, 0.047059f, 0.078431f, 0.111765f, 0.145098f, 0.176471f, 0.207843f, 0.241176f, 0.274510f, 0.305882f, 0.337255f, 0.370588f, 0.403922f, 0.435294f, 0.466667f, 0.5f,
    0.533333f, 0.564706f, 0.596078f, 0.629412f, 0.662745f, 0.694118f, 0.725490f, 0.758824f, 0.792157f, 0.823529f, 0.854902f, 0.888235f, 0.921569f, 0.952941f, 0.984314f, 1.0f
};

static const float midpoints6[64] = {
    0.007843f, 0.023529f, 0.039216f, 0.054902f, 0.070588f, 0.086275f, 0.101961f, 0.117647f, 0.133333f, 0.149020f, 0.164706f, 0.180392f, 0.196078f, 0.211765f, 0.227451f, 0.245098f, 
    0.262745f, 0.278431f, 0.294118f, 0.309804f, 0.325490f, 0.341176f, 0.356863f, 0.372549f, 0.388235f, 0.403922f, 0.419608f, 0.435294f, 0.450980f, 0.466667f, 0.482353f, 0.500000f, 
    0.517647f, 0.533333f, 0.549020f, 0.564706f, 0.580392f, 0.596078f, 0.611765f, 0.627451f, 0.643137f, 0.658824f, 0.674510f, 0.690196f, 0.705882f, 0.721569f, 0.737255f, 0.754902f, 
    0.772549f, 0.788235f, 0.803922f, 0.819608f, 0.835294f, 0.850980f, 0.866667f, 0.882353f, 0.898039f, 0.913725f, 0.929412f, 0.945098f, 0.960784f, 0.976471f, 0.992157f, 1.0f
};

/*void init_tables() {
    for (int i = 0; i < 31; i++) {
        float f0 = float(((i+0) << 3) | ((i+0) >> 2)) / 255.0f;
        float f1 = float(((i+1) << 3) | ((i+1) >> 2)) / 255.0f;
        midpoints5[i] = (f0 + f1) * 0.5;
    }
    midpoints5[31] = 1.0f;

    for (int i = 0; i < 63; i++) {
        float f0 = float(((i+0) << 2) | ((i+0) >> 4)) / 255.0f;
        float f1 = float(((i+1) << 2) | ((i+1) >> 4)) / 255.0f;
        midpoints6[i] = (f0 + f1) * 0.5;
    }
    midpoints6[63] = 1.0f;
}*/

static Color16 vector3_to_color16(const Vector3 & v) {

    // Truncate.
    uint r = uint(clamp(v.x * 31.0f, 0.0f, 31.0f));
	uint g = uint(clamp(v.y * 63.0f, 0.0f, 63.0f));
	uint b = uint(clamp(v.z * 31.0f, 0.0f, 31.0f));

    // Round exactly according to 565 bit-expansion.
    r += (v.x > midpoints5[r]);
    g += (v.y > midpoints6[g]);
    b += (v.z > midpoints5[b]);

    Color16 c;
    c.u = (r << 11) | (g << 5) | b;
    return c;
}

static Color32 bitexpand_color16_to_color32(Color16 c16) {
    Color32 c32;
    //c32.b = (c16.b << 3) | (c16.b >> 2);
    //c32.g = (c16.g << 2) | (c16.g >> 4);
    //c32.r = (c16.r << 3) | (c16.r >> 2);
    //c32.a = 0xFF;

    c32.u = ((c16.u << 3) & 0xf8) | ((c16.u << 5) & 0xfc00) | ((c16.u << 8) & 0xf80000);
    c32.u |= (c32.u >> 5) & 0x070007;
    c32.u |= (c32.u >> 6) & 0x000300;

    return c32;
}

inline Vector3 color_to_vector3(Color32 c) {
    return { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f };
}

inline Color32 vector3_to_color32(Vector3 v) {
    Color32 color;
    color.r = uint8(saturate(v.x) * 255 + 0.5f);
    color.g = uint8(saturate(v.y) * 255 + 0.5f);
    color.b = uint8(saturate(v.z) * 255 + 0.5f);
    color.a = 255;
    return color;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Input block processing.

inline bool is_black(Vector3 c) {
    // This large threshold seems to improve compression. This is not forcing these texels to be black, just 
    // causes them to be ignored during PCA.
    //return c.x < midpoints5[0] && c.y < midpoints6[0] && c.z < midpoints5[0];
    //return c.x < 1.0f / 32 && c.y < 1.0f / 32 && c.z < 1.0f / 32;
    return c.x < 1.0f / 8 && c.y < 1.0f / 8 && c.z < 1.0f / 8;
}

// Find similar colors and combine them together.
static int reduce_colors(const Vector4 * input_colors, const float * input_weights, int count, Vector3 * colors, float * weights, bool * any_black)
{
#if 0
    for (int i = 0; i < 16; i++) {
        colors[i] = input_colors[i].xyz;
        weights[i] = input_weights[i];
    }
    return 16;
#else
    *any_black = false;

    int n = 0;
    for (int i = 0; i < count; i++)
    {
        Vector3 ci = input_colors[i].xyz;
        float wi = input_weights[i];

        if (wi > 0) {
            const float threshold = 1.0f / 256;

            // Find matching color.
            int j;
            for (j = 0; j < n; j++) {
                if (equal(colors[j], ci, threshold)) {
                    weights[j] += wi;
                    break;
                }
            }

            // No match found. Add new color.
            if (j == n) {
                colors[n] = ci;
                weights[n] = wi;
                n++;
            }

            if (is_black(ci)) {
                *any_black = true;
            }
        }
    }

    ICBC_ASSERT(n <= count);

    return n;
#endif
}

static int skip_blacks(const Vector3 * input_colors, const float * input_weights, int count, Vector3 * colors, float * weights)
{
    int n = 0;
    for (int i = 0; i < count; i++)
    {
        Vector3 ci = input_colors[i];
        float wi = input_weights[i];

        if (is_black(ci)) {
            continue;
        }

        colors[n] = ci;
        weights[n] = wi;
        n += 1;
    }

    return n;
}



///////////////////////////////////////////////////////////////////////////////////////////////////
// PCA

static Vector3 computeCentroid(int n, const Vector3 *__restrict points, const float *__restrict weights)
{
    Vector3 centroid = { 0 };
    float total = 0.0f;

    for (int i = 0; i < n; i++)
    {
        total += weights[i];
        centroid += weights[i] * points[i];
    }
    centroid *= (1.0f / total);

    return centroid;
}

static Vector3 computeCovariance(int n, const Vector3 *__restrict points, const float *__restrict weights, float *__restrict covariance)
{
    // compute the centroid
    Vector3 centroid = computeCentroid(n, points, weights);

    // compute covariance matrix
    for (int i = 0; i < 6; i++)
    {
        covariance[i] = 0.0f;
    }

    for (int i = 0; i < n; i++)
    {
        Vector3 a = (points[i] - centroid);    // @@ I think weight should be squared, but that seems to increase the error slightly.
        Vector3 b = weights[i] * a;

        covariance[0] += a.x * b.x;
        covariance[1] += a.x * b.y;
        covariance[2] += a.x * b.z;
        covariance[3] += a.y * b.y;
        covariance[4] += a.y * b.z;
        covariance[5] += a.z * b.z;
    }

    return centroid;
}

// @@ We should be able to do something cheaper...
static Vector3 estimatePrincipalComponent(const float * __restrict matrix)
{
    const Vector3 row0 = { matrix[0], matrix[1], matrix[2] };
    const Vector3 row1 = { matrix[1], matrix[3], matrix[4] };
    const Vector3 row2 = { matrix[2], matrix[4], matrix[5] };

    float r0 = lengthSquared(row0);
    float r1 = lengthSquared(row1);
    float r2 = lengthSquared(row2);

    if (r0 > r1 && r0 > r2) return row0;
    if (r1 > r2) return row1;
    return row2;
}

static inline Vector3 firstEigenVector_PowerMethod(const float *__restrict matrix)
{
    if (matrix[0] == 0 && matrix[3] == 0 && matrix[5] == 0)
    {
        return {0};
    }

    Vector3 v = estimatePrincipalComponent(matrix);

    const int NUM = 8;
    for (int i = 0; i < NUM; i++)
    {
        float x = v.x * matrix[0] + v.y * matrix[1] + v.z * matrix[2];
        float y = v.x * matrix[1] + v.y * matrix[3] + v.z * matrix[4];
        float z = v.x * matrix[2] + v.y * matrix[4] + v.z * matrix[5];

        float norm = max(max(x, y), z);

        v = { x, y, z };
        v *= (1.0f / norm);
    }

    return v;
}

static Vector3 computePrincipalComponent_PowerMethod(int n, const Vector3 *__restrict points, const float *__restrict weights)
{
    float matrix[6];
    computeCovariance(n, points, weights, matrix);

    return firstEigenVector_PowerMethod(matrix);
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// SAT

struct SummedAreaTable {
    ICBC_ALIGN_16 float r[16];
    ICBC_ALIGN_16 float g[16];
    ICBC_ALIGN_16 float b[16];
    ICBC_ALIGN_16 float w[16];
};

int compute_sat(const Vector3 * colors, const float * weights, int count, SummedAreaTable * sat)
{
    // I've tried using a lower quality approximation of the principal direction, but the best fit line seems to produce best results.
    Vector3 principal = computePrincipalComponent_PowerMethod(count, colors, weights);

    // build the list of values
    int order[16];
    float dps[16];
    for (int i = 0; i < count; ++i)
    {
        order[i] = i;
        dps[i] = dot(colors[i], principal);
    }

    // stable sort
    for (int i = 0; i < count; ++i)
    {
        for (int j = i; j > 0 && dps[j] < dps[j - 1]; --j)
        {
            swap(dps[j], dps[j - 1]);
            swap(order[j], order[j - 1]);
        }
    }

    float w = weights[order[0]];
    sat->r[0] = colors[order[0]].x * w;
    sat->g[0] = colors[order[0]].y * w;
    sat->b[0] = colors[order[0]].z * w;
    sat->w[0] = w;

    for (int i = 1; i < count; i++) {
        float w = weights[order[i]];
        sat->r[i] = sat->r[i - 1] + colors[order[i]].x * w;
        sat->g[i] = sat->g[i - 1] + colors[order[i]].y * w;
        sat->b[i] = sat->b[i - 1] + colors[order[i]].z * w;
        sat->w[i] = sat->w[i - 1] + w;
    }

    for (int i = count; i < 16; i++) {
        sat->r[i] = FLT_MAX;
        sat->g[i] = FLT_MAX;
        sat->b[i] = FLT_MAX;
        sat->w[i] = FLT_MAX;
    }

    // Try incremental decimation:
    // for each pair, compute distance and weighted half point. Determine error. If error under threshold, collapse and repeat.

    /*if (m_count > 4)
    {
        float threshold = 1.0f / 128;

        uint j = 0;
        for (uint i = 0; i < m_count; ++i)
        {
            // @@ Compare 3D distance instead of projected distance?
            //if (j > 0 && dps[i] - dps[j - 1] < threshold) {
            if (j > 0 && lengthSquared(colors[order[i]] - colors[order[j - 1]]) < threshold*threshold) {
                m_colors[j - 1] += m_colors[i];
                m_weights[j - 1] += m_weights[i];
            }
            else {
                m_colors[j] = m_colors[i];
                m_weights[j] = m_weights[i];
                j += 1;
            }
        }

        m_count = j;

        m_xsum = { 0.0f };
        for (uint i = 0, j = 0; i < m_count; ++i)
        {
            m_xsum += m_colors[i];
        }
    }*/

    return count;    
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Cluster Fit

struct Combinations {
    uint8 c0, c1, c2, pad;
};

static ICBC_ALIGN_16 int s_fourClusterTotal[16];
static ICBC_ALIGN_16 int s_threeClusterTotal[16];
static ICBC_ALIGN_16 Combinations s_fourCluster[968 + 8];
static ICBC_ALIGN_16 Combinations s_threeCluster[152 + 8];

static void init_cluster_tables() {

    for (int t = 1, i = 0; t <= 16; t++) {
        for (int c0 = 0; c0 <= t; c0++) {
            for (int c1 = 0; c1 <= t - c0; c1++) {
                for (int c2 = 0; c2 <= t - c0 - c1; c2++) {

                    // Skip this cluster so that the total is a multiple of 8
                    if (c0 == 0 && c1 == 0 && c2 == 0) continue;

                    bool found = false;
                    if (t > 1) {
                        for (int j = 0; j < s_fourClusterTotal[t-2]; j++) {
                            if (s_fourCluster[j].c0 == c0 && s_fourCluster[j].c1 == c0+c1 && s_fourCluster[j].c2 == c0+c1+c2) {
                                found = true;
                                break;
                            }
                        }
                    }

                    if (!found) {
                        s_fourCluster[i].c0 = c0;
                        s_fourCluster[i].c1 = c0+c1;
                        s_fourCluster[i].c2 = c0+c1+c2;
                        i++;
                    }
                }
            }
        }

        s_fourClusterTotal[t - 1] = i;
    }

    // Replicate last entry.
    for (int i = 0; i < 8; i++) {
        s_fourCluster[968 + i] = s_fourCluster[968-1];
    }

    for (int t = 1, i = 0; t <= 16; t++) {
        for (int c0 = 0; c0 <= t; c0++) {
            for (int c1 = 0; c1 <= t - c0; c1++) {

                // Skip this cluster so that the total is a multiple of 8
                if (c0 == 0 && c1 == 0) continue;

                bool found = false;
                if (t > 1) {
                    for (int j = 0; j < s_threeClusterTotal[t - 2]; j++) {
                        if (s_threeCluster[j].c0 == c0 && s_threeCluster[j].c1 == c0+c1) {
                            found = true;
                            break;
                        }
                    }
                }

                if (!found) {
                    s_threeCluster[i].c0 = c0;
                    s_threeCluster[i].c1 = c0 + c1;
                    i++;
                }
            }
        }

        s_threeClusterTotal[t - 1] = i;
    }

    // Replicate last entry.
    for (int i = 0; i < 8; i++) {
        s_threeCluster[152 + i] = s_threeCluster[152 - 1];
    }
}



static void cluster_fit_three(const SummedAreaTable & sat, int count, Vector3 metric_sqr, Vector3 * start, Vector3 * end)
{
    const float r_sum = sat.r[count-1];
    const float g_sum = sat.g[count-1];
    const float b_sum = sat.b[count-1];
    const float w_sum = sat.w[count-1];

    VFloat vbesterror = vbroadcast(FLT_MAX);
    VVector3 vbeststart = { vzero(), vzero(), vzero() };
    VVector3 vbestend = { vzero(), vzero(), vzero() };

    // check all possible clusters for this total order
    const int total_order_count = s_threeClusterTotal[count - 1];

    for (int i = 0; i < total_order_count; i += VEC_SIZE)
    {
        VVector3 x0, x1;
        VFloat w0, w1;

#if ICBC_USE_AVX512_PERMUTE

        auto loadmask = lane_id() < vbroadcast(float(count));

        // Load sat in one register:
        VFloat vrsat = vload(loadmask, sat.r, FLT_MAX);
        VFloat vgsat = vload(loadmask, sat.g, FLT_MAX);
        VFloat vbsat = vload(loadmask, sat.b, FLT_MAX);
        VFloat vwsat = vload(loadmask, sat.w, FLT_MAX);

        // Load 4 uint8 per lane.
        __m512i packedClusterIndex = _mm512_load_si512((__m512i *)&s_threeCluster[i]);

        // Load index and decrement.
        auto c0 = _mm512_and_epi32(packedClusterIndex, _mm512_set1_epi32(0xFF));
        auto c0mask = _mm512_cmpgt_epi32_mask(c0, _mm512_setzero_si512());
        c0 = _mm512_sub_epi32(c0, _mm512_set1_epi32(1));

        // @@ Avoid blend_ps?
        // if upper bit set, zero, otherwise load sat entry.
        x0.x = _mm512_mask_blend_ps(c0mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c0, vrsat));
        x0.y = _mm512_mask_blend_ps(c0mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c0, vgsat));
        x0.z = _mm512_mask_blend_ps(c0mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c0, vbsat));
        w0 = _mm512_mask_blend_ps(c0mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c0, vwsat));

        auto c1 = _mm512_and_epi32(_mm512_srli_epi32(packedClusterIndex, 8), _mm512_set1_epi32(0xFF));
        auto c1mask = _mm512_cmpgt_epi32_mask(c1, _mm512_setzero_si512());
        c1 = _mm512_sub_epi32(c1, _mm512_set1_epi32(1));

        x1.x = _mm512_mask_blend_ps(c1mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c1, vrsat));
        x1.y = _mm512_mask_blend_ps(c1mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c1, vgsat));
        x1.z = _mm512_mask_blend_ps(c1mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c1, vbsat));
        w1 = _mm512_mask_blend_ps(c1mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c1, vwsat));

#elif ICBC_USE_AVX2_PERMUTE2
        // Fabian Giesen says not to mix _mm256_blendv_ps and _mm256_permutevar8x32_ps since they contend for the same resources and instead emulate blendv using bit ops.
        // On my machine (Intel Skylake) I'm not seeing any performance difference, but this may still be valuable for older CPUs.

        // Load 4 uint8 per lane.
        __m256i packedClusterIndex = _mm256_load_si256((__m256i *)&s_threeCluster[i]);

        if (count <= 8) {

            // Load sat.r in one register:
            VFloat r07 = vload(sat.r);
            VFloat g07 = vload(sat.g);
            VFloat b07 = vload(sat.b);
            VFloat w07 = vload(sat.w);

            // Load index and decrement.
            auto c0 = _mm256_and_si256(packedClusterIndex, _mm256_set1_epi32(0xFF));
            auto c0mask = _mm256_castsi256_ps(_mm256_cmpgt_epi32(c0, _mm256_setzero_si256()));
            c0 = _mm256_sub_epi32(c0, _mm256_set1_epi32(1));

            // if upper bit set, zero, otherwise load sat entry.
            x0.x = _mm256_and_ps(_mm256_permutevar8x32_ps(r07, c0), c0mask);
            x0.y = _mm256_and_ps(_mm256_permutevar8x32_ps(g07, c0), c0mask);
            x0.z = _mm256_and_ps(_mm256_permutevar8x32_ps(b07, c0), c0mask);
            w0 = _mm256_and_ps(_mm256_permutevar8x32_ps(w07, c0), c0mask);

            auto c1 = _mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 8), _mm256_set1_epi32(0xFF));
            auto c1mask = _mm256_castsi256_ps(_mm256_cmpgt_epi32(c1, _mm256_setzero_si256()));
            c1 = _mm256_sub_epi32(c1, _mm256_set1_epi32(1));

            x1.x = _mm256_and_ps(_mm256_permutevar8x32_ps(r07, c1), c1mask);
            x1.y = _mm256_and_ps(_mm256_permutevar8x32_ps(g07, c1), c1mask);
            x1.z = _mm256_and_ps(_mm256_permutevar8x32_ps(b07, c1), c1mask);
            w1 = _mm256_and_ps(_mm256_permutevar8x32_ps(w07, c1), c1mask);

        }
        else {
            //lo = vload(tab);
            //upxor = vload(tab + 8) ^ lo;
            //permute_ind = unpacked_ind - 1;
            //lookup = permutevar8x32(lo, permute_ind);
            //is_upper = permute_ind > 7;
            //lookup ^= permutevar8x32(upxor, permute_ind) & is_upper;
            //lookup &= unpacked_ind > 0;

            // Load sat.r in two registers:
            VFloat rLo = vload(sat.r); VFloat rHi = vload(sat.r + 8);
            VFloat gLo = vload(sat.g); VFloat gHi = vload(sat.g + 8);
            VFloat bLo = vload(sat.b); VFloat bHi = vload(sat.b + 8);
            VFloat wLo = vload(sat.w); VFloat wHi = vload(sat.w + 8);

            auto c0 = _mm256_and_si256(packedClusterIndex, _mm256_set1_epi32(0xFF));
            auto c0Lo = _mm256_sub_epi32(c0, _mm256_set1_epi32(1));

            // if upper bit set, zero, otherwise load sat entry.
            x0.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rLo, c0Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c0Lo));
            x0.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gLo, c0Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c0Lo));
            x0.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bLo, c0Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c0Lo));
            w0 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wLo, c0Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c0Lo));

            auto c0Hi = _mm256_sub_epi32(c0, _mm256_set1_epi32(9));

            // if upper bit set, same, otherwise load sat entry.
            x0.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rHi, c0Hi), x0.x, _mm256_castsi256_ps(c0Hi));
            x0.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gHi, c0Hi), x0.y, _mm256_castsi256_ps(c0Hi));
            x0.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bHi, c0Hi), x0.z, _mm256_castsi256_ps(c0Hi));
            w0 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wHi, c0Hi), w0, _mm256_castsi256_ps(c0Hi));

            auto c1 = _mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 8), _mm256_set1_epi32(0xFF));
            auto c1Lo = _mm256_sub_epi32(c1, _mm256_set1_epi32(1));

            // if upper bit set, zero, otherwise load sat entry.
            x1.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rLo, c1Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c1Lo));
            x1.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gLo, c1Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c1Lo));
            x1.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bLo, c1Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c1Lo));
            w1 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wLo, c1Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c1Lo));

            auto c1Hi = _mm256_sub_epi32(c1, _mm256_set1_epi32(9));

            // if upper bit set, same, otherwise load sat entry.
            x1.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rHi, c1Hi), x1.x, _mm256_castsi256_ps(c1Hi));
            x1.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gHi, c1Hi), x1.y, _mm256_castsi256_ps(c1Hi));
            x1.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bHi, c1Hi), x1.z, _mm256_castsi256_ps(c1Hi));
            w1 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wHi, c1Hi), w1, _mm256_castsi256_ps(c1Hi));
        }

#elif ICBC_USE_AVX2_PERMUTE
        // Load 4 uint8 per lane.
        __m256i packedClusterIndex = _mm256_load_si256((__m256i *)&s_threeCluster[i]);

        if (count <= 8) {

            // Load index and decrement.
            auto c0 = _mm256_sub_epi32(_mm256_and_si256(packedClusterIndex, _mm256_set1_epi32(0xFF)), _mm256_set1_epi32(1));
            auto c1 = _mm256_sub_epi32(_mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 8), _mm256_set1_epi32(0xFF)), _mm256_set1_epi32(1));

            // Load sat in one register.
            // if upper bit set, zero, otherwise load sat entry.

            VFloat r07 = vload(sat.r);
            x0.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(r07, c0), _mm256_setzero_ps(), _mm256_castsi256_ps(c0));
            x1.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(r07, c1), _mm256_setzero_ps(), _mm256_castsi256_ps(c1));

            VFloat g07 = vload(sat.g);
            x0.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(g07, c0), _mm256_setzero_ps(), _mm256_castsi256_ps(c0));
            x1.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(g07, c1), _mm256_setzero_ps(), _mm256_castsi256_ps(c1));

            VFloat b07 = vload(sat.b);
            x0.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(b07, c0), _mm256_setzero_ps(), _mm256_castsi256_ps(c0));
            x1.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(b07, c1), _mm256_setzero_ps(), _mm256_castsi256_ps(c1));

            VFloat w07 = vload(sat.w);
            w0 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(w07, c0), _mm256_setzero_ps(), _mm256_castsi256_ps(c0));
            w1 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(w07, c1), _mm256_setzero_ps(), _mm256_castsi256_ps(c1));

        }
        else {
            // Load sat.r in two registers:
            VFloat rLo = vload(sat.r); VFloat rHi = vload(sat.r + 8);
            VFloat gLo = vload(sat.g); VFloat gHi = vload(sat.g + 8);
            VFloat bLo = vload(sat.b); VFloat bHi = vload(sat.b + 8);
            VFloat wLo = vload(sat.w); VFloat wHi = vload(sat.w + 8);

            auto c0 = _mm256_and_si256(packedClusterIndex, _mm256_set1_epi32(0xFF));
            auto c0Lo = _mm256_sub_epi32(c0, _mm256_set1_epi32(1));

            // if upper bit set, zero, otherwise load sat entry.
            x0.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rLo, c0Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c0Lo));
            x0.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gLo, c0Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c0Lo));
            x0.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bLo, c0Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c0Lo));
            w0 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wLo, c0Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c0Lo));

            auto c0Hi = _mm256_sub_epi32(c0, _mm256_set1_epi32(9));

            // if upper bit set, same, otherwise load sat entry.
            x0.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rHi, c0Hi), x0.x, _mm256_castsi256_ps(c0Hi));
            x0.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gHi, c0Hi), x0.y, _mm256_castsi256_ps(c0Hi));
            x0.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bHi, c0Hi), x0.z, _mm256_castsi256_ps(c0Hi));
            w0 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wHi, c0Hi), w0, _mm256_castsi256_ps(c0Hi));

            auto c1 = _mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 8), _mm256_set1_epi32(0xFF));
            auto c1Lo = _mm256_sub_epi32(c1, _mm256_set1_epi32(1));

            // if upper bit set, zero, otherwise load sat entry.
            x1.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rLo, c1Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c1Lo));
            x1.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gLo, c1Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c1Lo));
            x1.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bLo, c1Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c1Lo));
            w1 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wLo, c1Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c1Lo));

            auto c1Hi = _mm256_sub_epi32(c1, _mm256_set1_epi32(9));

            // if upper bit set, same, otherwise load sat entry.
            x1.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rHi, c1Hi), x1.x, _mm256_castsi256_ps(c1Hi));
            x1.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gHi, c1Hi), x1.y, _mm256_castsi256_ps(c1Hi));
            x1.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bHi, c1Hi), x1.z, _mm256_castsi256_ps(c1Hi));
            w1 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wHi, c1Hi), w1, _mm256_castsi256_ps(c1Hi));
        }

#elif ICBC_USE_AVX2_GATHER // @@ Make this work with the 16-element SAT

        // Load 4 uint8 per lane.
        __m256i packedClusterIndex = _mm256_load_si256((__m256i *)&s_threeCluster[i]);

        // Load SAT elements.
        float * base = (float *)sat.x;

        __m256i c0 = _mm256_slli_epi32(packedClusterIndex, 2);
        c0 = _mm256_and_si256(c0, _mm256_set1_epi32(0x3FC));

        x0.x = _mm256_i32gather_ps(base + 0, c0, 4);
        x0.y = _mm256_i32gather_ps(base + 1, c0, 4);
        x0.z = _mm256_i32gather_ps(base + 2, c0, 4);
        w0 = _mm256_i32gather_ps(base + 3, c0, 4);

        __m256i c1 = _mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 6), _mm256_set1_epi32(0x3FC));
        x1.x = _mm256_i32gather_ps(base + 0, c1, 4);
        x1.y = _mm256_i32gather_ps(base + 1, c1, 4);
        x1.z = _mm256_i32gather_ps(base + 2, c1, 4);
        w1 = _mm256_i32gather_ps(base + 3, c1, 4);

#else
        // Plain scalar path
        x0.x = vzero(); x0.y = vzero(); x0.z = vzero(); w0 = vzero();
        x1.x = vzero(); x1.y = vzero(); x1.z = vzero(); w1 = vzero();

        for (int l = 0; l < VEC_SIZE; l++) {
            uint c0 = s_threeCluster[i + l].c0;
            if (c0) {
                c0 -= 1;
                lane(x0.x, l) = sat.r[c0];
                lane(x0.y, l) = sat.g[c0];
                lane(x0.z, l) = sat.b[c0];
                lane(w0, l) = sat.w[c0];
            }

            uint c1 = s_threeCluster[i + l].c1;
            if (c1) {
                c1 -= 1;
                lane(x1.x, l) = sat.r[c1];
                lane(x1.y, l) = sat.g[c1];
                lane(x1.z, l) = sat.b[c1];
                lane(w1, l) = sat.w[c1];
            }
        }
#endif

        VFloat w2 = vbroadcast(w_sum) - w1;
        x1 = x1 - x0;
        w1 = w1 - w0;

        VFloat alphabeta_sum = w1 * vbroadcast(0.25f);
        VFloat alpha2_sum = w0 + alphabeta_sum;
        VFloat beta2_sum = w2 + alphabeta_sum;
        VFloat factor = vrcp(alpha2_sum * beta2_sum - alphabeta_sum * alphabeta_sum);

        VVector3 alphax_sum = x0 + x1 * vbroadcast(0.5f);
        VVector3 betax_sum = vbroadcast(r_sum, g_sum, b_sum) - alphax_sum;

        VVector3 a = (alphax_sum * beta2_sum - betax_sum * alphabeta_sum) * factor;
        VVector3 b = (betax_sum * alpha2_sum - alphax_sum * alphabeta_sum) * factor;

        // clamp to the grid
        a = vsaturate(a);
        b = vsaturate(b);
        a = vround_ept(a);
        b = vround_ept(b);

        // compute the error
        VVector3 e1 = vmad(a * a, alpha2_sum, vmad(b * b, beta2_sum, (a * b * alphabeta_sum - a * alphax_sum - b * betax_sum) * vbroadcast(2.0f)));

        // apply the metric to the error term
        VFloat error = vdot(e1, vbroadcast(metric_sqr));


        // keep the solution if it wins
        auto mask = (error < vbesterror);

        // I could mask the unused lanes here, but instead I set the invalid SAT entries to FLT_MAX.
        //mask = (mask & (vbroadcast(total_order_count) >= tid8(i))); // This doesn't seem to help. Is it OK to consider elements out of bounds?

        vbesterror = vselect(mask, vbesterror, error);
        vbeststart = vselect(mask, vbeststart, a);
        vbestend = vselect(mask, vbestend, b);
    }

    // Is there a better way to do this reduction?
    float besterror = FLT_MAX;    
    int bestindex;
    for (int i = 0; i < VEC_SIZE; i++) {
        if (lane(vbesterror, i) < besterror) {
            besterror = lane(vbesterror, i);
            bestindex = i;
        }
    }

    // declare variables
    Vector3 beststart;
    beststart.x = lane(vbeststart.x, bestindex);
    beststart.y = lane(vbeststart.y, bestindex);
    beststart.z = lane(vbeststart.z, bestindex);

    Vector3 bestend;    
    bestend.x = lane(vbestend.x, bestindex);
    bestend.y = lane(vbestend.y, bestindex);
    bestend.z = lane(vbestend.z, bestindex);

    *start = beststart;
    *end = bestend;
}


static void cluster_fit_four(const SummedAreaTable & sat, int count, Vector3 metric_sqr, Vector3 * start, Vector3 * end)
{
    const float r_sum = sat.r[count-1];
    const float g_sum = sat.g[count-1];
    const float b_sum = sat.b[count-1];
    const float w_sum = sat.w[count-1];

    VFloat vbesterror = vbroadcast(FLT_MAX);
    VVector3 vbeststart = { vzero(), vzero(), vzero() };
    VVector3 vbestend = { vzero(), vzero(), vzero() };

    // check all possible clusters for this total order
    const int total_order_count = s_fourClusterTotal[count - 1];

    for (int i = 0; i < total_order_count; i += VEC_SIZE)
    {
        VVector3 x0, x1, x2;
        VFloat w0, w1, w2;

        /*
        // Another approach would be to load and broadcast one color at a time like I do in my old CUDA implementation.
        uint akku = 0;

        // Compute alpha & beta for this permutation.
        #pragma unroll
        for (int i = 0; i < 16; i++)
        {
            const uint bits = permutation >> (2*i);

            alphax_sum += alphaTable4[bits & 3] * colors[i];
            akku += prods4[bits & 3];
        }

        float alpha2_sum = float(akku >> 16);
        float beta2_sum = float((akku >> 8) & 0xff);
        float alphabeta_sum = float(akku & 0xff);
        float3 betax_sum = 9.0f * color_sum - alphax_sum;
        */

#if ICBC_USE_AVX512_PERMUTE

        auto loadmask = lane_id() < vbroadcast(float(count));

        // Load sat in one register:
        VFloat vrsat = vload(loadmask, sat.r, FLT_MAX);
        VFloat vgsat = vload(loadmask, sat.g, FLT_MAX);
        VFloat vbsat = vload(loadmask, sat.b, FLT_MAX);
        VFloat vwsat = vload(loadmask, sat.w, FLT_MAX);

        // Load 4 uint8 per lane.
        __m512i packedClusterIndex = _mm512_load_si512((__m512i *)&s_fourCluster[i]);

        // Load index and decrement.
        auto c0 = _mm512_and_epi32(packedClusterIndex, _mm512_set1_epi32(0xFF));
        auto c0mask = _mm512_cmpgt_epi32_mask(c0, _mm512_setzero_si512());
        c0 = _mm512_sub_epi32(c0, _mm512_set1_epi32(1));

        // if upper bit set, zero, otherwise load sat entry.
        x0.x = _mm512_mask_blend_ps(c0mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c0, vrsat));
        x0.y = _mm512_mask_blend_ps(c0mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c0, vgsat));
        x0.z = _mm512_mask_blend_ps(c0mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c0, vbsat));
        w0 = _mm512_mask_blend_ps(c0mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c0, vwsat));

        auto c1 = _mm512_and_epi32(_mm512_srli_epi32(packedClusterIndex, 8), _mm512_set1_epi32(0xFF));
        auto c1mask = _mm512_cmpgt_epi32_mask(c1, _mm512_setzero_si512());
        c1 = _mm512_sub_epi32(c1, _mm512_set1_epi32(1));

        x1.x = _mm512_mask_blend_ps(c1mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c1, vrsat));
        x1.y = _mm512_mask_blend_ps(c1mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c1, vgsat));
        x1.z = _mm512_mask_blend_ps(c1mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c1, vbsat));
        w1 = _mm512_mask_blend_ps(c1mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c1, vwsat));

        auto c2 = _mm512_and_epi32(_mm512_srli_epi32(packedClusterIndex, 16), _mm512_set1_epi32(0xFF));
        auto c2mask = _mm512_cmpgt_epi32_mask(c2, _mm512_setzero_si512());
        c2 = _mm512_sub_epi32(c2, _mm512_set1_epi32(1));

        x2.x = _mm512_mask_blend_ps(c2mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c2, vrsat));
        x2.y = _mm512_mask_blend_ps(c2mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c2, vgsat));
        x2.z = _mm512_mask_blend_ps(c2mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c2, vbsat));
        w2 = _mm512_mask_blend_ps(c2mask, _mm512_setzero_ps(), _mm512_permutexvar_ps(c2, vwsat));

#elif ICBC_USE_AVX2_PERMUTE2
        // Fabian Giesen says not to mix _mm256_blendv_ps and _mm256_permutevar8x32_ps since they contend for the same resources and instead emulate blendv using bit ops.

        // Load 4 uint8 per lane.
        __m256i packedClusterIndex = _mm256_load_si256((__m256i *)&s_fourCluster[i]);

        if (count <= 8) {
            // Load sat.r in one register:
            VFloat r07 = vload(sat.r);
            VFloat g07 = vload(sat.g);
            VFloat b07 = vload(sat.b);
            VFloat w07 = vload(sat.w);

            // Load index and decrement.
            auto c0 = _mm256_and_si256(packedClusterIndex, _mm256_set1_epi32(0xFF));
            auto c0mask = _mm256_castsi256_ps(_mm256_cmpgt_epi32(c0, _mm256_setzero_si256()));
            c0 = _mm256_sub_epi32(c0, _mm256_set1_epi32(1));

            // Load sat entry, -1 returns 0.
            x0.x = _mm256_and_ps(_mm256_permutevar8x32_ps(r07, c0), c0mask);
            x0.y = _mm256_and_ps(_mm256_permutevar8x32_ps(g07, c0), c0mask);
            x0.z = _mm256_and_ps(_mm256_permutevar8x32_ps(b07, c0), c0mask);
            w0 = _mm256_and_ps(_mm256_permutevar8x32_ps(w07, c0), c0mask);

            auto c1 = _mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 8), _mm256_set1_epi32(0xFF));
            auto c1mask = _mm256_castsi256_ps(_mm256_cmpgt_epi32(c1, _mm256_setzero_si256()));
            c1 = _mm256_sub_epi32(c1, _mm256_set1_epi32(1));

            x1.x = _mm256_and_ps(_mm256_permutevar8x32_ps(r07, c1), c1mask);
            x1.y = _mm256_and_ps(_mm256_permutevar8x32_ps(g07, c1), c1mask);
            x1.z = _mm256_and_ps(_mm256_permutevar8x32_ps(b07, c1), c1mask);
            w1 = _mm256_and_ps(_mm256_permutevar8x32_ps(w07, c1), c1mask);

            auto c2 = _mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 16), _mm256_set1_epi32(0xFF));
            auto c2mask = _mm256_castsi256_ps(_mm256_cmpgt_epi32(c2, _mm256_setzero_si256()));
            c2 = _mm256_sub_epi32(c2, _mm256_set1_epi32(1));

            x2.x = _mm256_and_ps(_mm256_permutevar8x32_ps(r07, c2), c2mask);
            x2.y = _mm256_and_ps(_mm256_permutevar8x32_ps(g07, c2), c2mask);
            x2.z = _mm256_and_ps(_mm256_permutevar8x32_ps(b07, c2), c2mask);
            w2 = _mm256_and_ps(_mm256_permutevar8x32_ps(w07, c2), c2mask);
        }
        else {
            // Load sat.r in two registers:
            VFloat rLo = vload(sat.r); VFloat rHi = vload(sat.r + 8);
            VFloat gLo = vload(sat.g); VFloat gHi = vload(sat.g + 8);
            VFloat bLo = vload(sat.b); VFloat bHi = vload(sat.b + 8);
            VFloat wLo = vload(sat.w); VFloat wHi = vload(sat.w + 8);

            auto c0 = _mm256_and_si256(packedClusterIndex, _mm256_set1_epi32(0xFF));
            auto c0LoMask = _mm256_castsi256_ps(_mm256_cmpgt_epi32(c0, _mm256_setzero_si256()));
            auto c0Lo = _mm256_sub_epi32(c0, _mm256_set1_epi32(1));

            // if upper bit set, zero, otherwise load sat entry.
            x0.x = _mm256_and_ps(_mm256_permutevar8x32_ps(rLo, c0Lo), c0LoMask);
            x0.y = _mm256_and_ps(_mm256_permutevar8x32_ps(gLo, c0Lo), c0LoMask);
            x0.z = _mm256_and_ps(_mm256_permutevar8x32_ps(bLo, c0Lo), c0LoMask);
            w0 = _mm256_and_ps(_mm256_permutevar8x32_ps(wLo, c0Lo), c0LoMask);

            auto c0Hi = _mm256_sub_epi32(c0, _mm256_set1_epi32(9));

            // if upper bit set, same, otherwise load sat entry.
            x0.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rHi, c0Hi), x0.x, _mm256_castsi256_ps(c0Hi));
            x0.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gHi, c0Hi), x0.y, _mm256_castsi256_ps(c0Hi));
            x0.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bHi, c0Hi), x0.z, _mm256_castsi256_ps(c0Hi));
            w0 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wHi, c0Hi), w0, _mm256_castsi256_ps(c0Hi));

            auto c1 = _mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 8), _mm256_set1_epi32(0xFF));
            auto c1LoMask = _mm256_castsi256_ps(_mm256_cmpgt_epi32(c1, _mm256_setzero_si256()));
            auto c1Lo = _mm256_sub_epi32(c1, _mm256_set1_epi32(1));

            // if upper bit set, zero, otherwise load sat entry.
            x1.x = _mm256_and_ps(_mm256_permutevar8x32_ps(rLo, c1Lo), c1LoMask);
            x1.y = _mm256_and_ps(_mm256_permutevar8x32_ps(gLo, c1Lo), c1LoMask);
            x1.z = _mm256_and_ps(_mm256_permutevar8x32_ps(bLo, c1Lo), c1LoMask);
            w1 = _mm256_and_ps(_mm256_permutevar8x32_ps(wLo, c1Lo), c1LoMask);

            auto c1Hi = _mm256_sub_epi32(c1, _mm256_set1_epi32(9));

            // if upper bit set, same, otherwise load sat entry.
            x1.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rHi, c1Hi), x1.x, _mm256_castsi256_ps(c1Hi));
            x1.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gHi, c1Hi), x1.y, _mm256_castsi256_ps(c1Hi));
            x1.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bHi, c1Hi), x1.z, _mm256_castsi256_ps(c1Hi));
            w1 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wHi, c1Hi), w1, _mm256_castsi256_ps(c1Hi));

            auto c2 = _mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 16), _mm256_set1_epi32(0xFF));
            auto c2LoMask = _mm256_castsi256_ps(_mm256_cmpgt_epi32(c2, _mm256_setzero_si256()));
            auto c2Lo = _mm256_sub_epi32(c2, _mm256_set1_epi32(1));

            // if upper bit set, zero, otherwise load sat entry.
            x2.x = _mm256_and_ps(_mm256_permutevar8x32_ps(rLo, c2Lo), c2LoMask);
            x2.y = _mm256_and_ps(_mm256_permutevar8x32_ps(gLo, c2Lo), c2LoMask);
            x2.z = _mm256_and_ps(_mm256_permutevar8x32_ps(bLo, c2Lo), c2LoMask);
            w2 = _mm256_and_ps(_mm256_permutevar8x32_ps(wLo, c2Lo), c2LoMask);

            auto c2Hi = _mm256_sub_epi32(c2, _mm256_set1_epi32(9));

            // if upper bit set, same, otherwise load sat entry.
            x2.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rHi, c2Hi), x2.x, _mm256_castsi256_ps(c2Hi));
            x2.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gHi, c2Hi), x2.y, _mm256_castsi256_ps(c2Hi));
            x2.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bHi, c2Hi), x2.z, _mm256_castsi256_ps(c2Hi));
            w2 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wHi, c2Hi), w2, _mm256_castsi256_ps(c2Hi));
    }

#elif ICBC_USE_AVX2_PERMUTE
        // Load 4 uint8 per lane.
        __m256i packedClusterIndex = _mm256_load_si256((__m256i *)&s_fourCluster[i]);

        if (count <= 8) {
            // Load index and decrement.
            auto c0 = _mm256_and_si256(packedClusterIndex, _mm256_set1_epi32(0xFF));
            c0 = _mm256_sub_epi32(c0, _mm256_set1_epi32(1));

            auto c1 = _mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 8), _mm256_set1_epi32(0xFF));
            c1 = _mm256_sub_epi32(c1, _mm256_set1_epi32(1));

            auto c2 = _mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 16), _mm256_set1_epi32(0xFF));
            c2 = _mm256_sub_epi32(c2, _mm256_set1_epi32(1));

            // if upper bit set, zero, otherwise load sat entry.
            VFloat r07 = vload(sat.r);
            x0.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(r07, c0), _mm256_setzero_ps(), _mm256_castsi256_ps(c0));
            x1.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(r07, c1), _mm256_setzero_ps(), _mm256_castsi256_ps(c1));
            x2.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(r07, c2), _mm256_setzero_ps(), _mm256_castsi256_ps(c2));

            VFloat g07 = vload(sat.g);
            x0.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(g07, c0), _mm256_setzero_ps(), _mm256_castsi256_ps(c0));
            x1.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(g07, c1), _mm256_setzero_ps(), _mm256_castsi256_ps(c1));
            x2.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(g07, c2), _mm256_setzero_ps(), _mm256_castsi256_ps(c2));

            VFloat b07 = vload(sat.b);
            x0.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(b07, c0), _mm256_setzero_ps(), _mm256_castsi256_ps(c0));
            x1.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(b07, c1), _mm256_setzero_ps(), _mm256_castsi256_ps(c1));
            x2.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(b07, c2), _mm256_setzero_ps(), _mm256_castsi256_ps(c2));

            VFloat w07 = vload(sat.w);
            w0 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(w07, c0), _mm256_setzero_ps(), _mm256_castsi256_ps(c0));
            w1 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(w07, c1), _mm256_setzero_ps(), _mm256_castsi256_ps(c1));
            w2 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(w07, c2), _mm256_setzero_ps(), _mm256_castsi256_ps(c2));

        }
        else {
            // Unpack indices.           
            auto c0 = _mm256_and_si256(packedClusterIndex, _mm256_set1_epi32(0xFF));
            auto c0Lo = _mm256_sub_epi32(c0, _mm256_set1_epi32(1));
            auto c0Hi = _mm256_sub_epi32(c0, _mm256_set1_epi32(9));

            auto c1 = _mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 8), _mm256_set1_epi32(0xFF));
            auto c1Lo = _mm256_sub_epi32(c1, _mm256_set1_epi32(1));
            auto c1Hi = _mm256_sub_epi32(c1, _mm256_set1_epi32(9));

            auto c2 = _mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 16), _mm256_set1_epi32(0xFF));
            auto c2Lo = _mm256_sub_epi32(c2, _mm256_set1_epi32(1));
            auto c2Hi = _mm256_sub_epi32(c2, _mm256_set1_epi32(9));

            // if upper bit set, zero, otherwise load sat entry.
            VFloat rLo = vload(sat.r);
            x0.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rLo, c0Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c0Lo));
            x1.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rLo, c1Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c1Lo));
            x2.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rLo, c2Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c2Lo));

            VFloat rHi = vload(sat.r + 8);
            x0.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rHi, c0Hi), x0.x, _mm256_castsi256_ps(c0Hi));
            x1.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rHi, c1Hi), x1.x, _mm256_castsi256_ps(c1Hi));
            x2.x = _mm256_blendv_ps(_mm256_permutevar8x32_ps(rHi, c2Hi), x2.x, _mm256_castsi256_ps(c2Hi));

            VFloat gLo = vload(sat.g);
            x0.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gLo, c0Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c0Lo));
            x1.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gLo, c1Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c1Lo));
            x2.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gLo, c2Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c2Lo));

            VFloat gHi = vload(sat.g + 8);
            x0.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gHi, c0Hi), x0.y, _mm256_castsi256_ps(c0Hi));
            x1.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gHi, c1Hi), x1.y, _mm256_castsi256_ps(c1Hi));
            x2.y = _mm256_blendv_ps(_mm256_permutevar8x32_ps(gHi, c2Hi), x2.y, _mm256_castsi256_ps(c2Hi));

            VFloat bLo = vload(sat.b);
            x0.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bLo, c0Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c0Lo));
            x1.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bLo, c1Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c1Lo));
            x2.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bLo, c2Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c2Lo));

            VFloat bHi = vload(sat.b + 8);
            x0.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bHi, c0Hi), x0.z, _mm256_castsi256_ps(c0Hi));
            x1.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bHi, c1Hi), x1.z, _mm256_castsi256_ps(c1Hi));
            x2.z = _mm256_blendv_ps(_mm256_permutevar8x32_ps(bHi, c2Hi), x2.z, _mm256_castsi256_ps(c2Hi));

            VFloat wLo = vload(sat.w);
            w0 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wLo, c0Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c0Lo));
            w1 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wLo, c1Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c1Lo));
            w2 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wLo, c2Lo), _mm256_setzero_ps(), _mm256_castsi256_ps(c2Lo));

            VFloat wHi = vload(sat.w + 8);
            w0 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wHi, c0Hi), w0, _mm256_castsi256_ps(c0Hi));
            w1 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wHi, c1Hi), w1, _mm256_castsi256_ps(c1Hi));
            w2 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(wHi, c2Hi), w2, _mm256_castsi256_ps(c2Hi));
        }

#elif ICBC_USE_AVX2_GATHER

        // Load 4 uint8 per lane.
        __m256i packedClusterIndex = _mm256_load_si256((__m256i *)&s_fourCluster[i]);

#if 0 // Masked gathers.
        auto c0 = _mm256_and_si256(packedClusterIndex, _mm256_set1_epi32(0xFF));
        auto c0mask = _mm256_castsi256_ps(_mm256_cmpgt_epi32(c0, _mm256_setzero_si256()));
        c0 = _mm256_sub_epi32(c0, _mm256_set1_epi32(1));

        x0.x = _mm256_mask_i32gather_ps(_mm256_setzero_ps(), sat.r, c0, c0mask, 4);
        x0.y = _mm256_mask_i32gather_ps(_mm256_setzero_ps(), sat.g, c0, c0mask, 4);
        x0.z = _mm256_mask_i32gather_ps(_mm256_setzero_ps(), sat.b, c0, c0mask, 4);
        w0 = _mm256_mask_i32gather_ps(_mm256_setzero_ps(), sat.w, c0, c0mask, 4);

        auto c1 = _mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 8), _mm256_set1_epi32(0xFF));
        auto c1mask = _mm256_castsi256_ps(_mm256_cmpgt_epi32(c0, _mm256_setzero_si256()));
        c1 = _mm256_sub_epi32(c1, _mm256_set1_epi32(1));

        x1.x = _mm256_mask_i32gather_ps(_mm256_setzero_ps(), sat.r, c1, c1mask, 4);
        x1.y = _mm256_mask_i32gather_ps(_mm256_setzero_ps(), sat.g, c1, c1mask, 4);
        x1.z = _mm256_mask_i32gather_ps(_mm256_setzero_ps(), sat.b, c1, c1mask, 4);
        w1 = _mm256_mask_i32gather_ps(_mm256_setzero_ps(), sat.w, c1, c1mask, 4);

        auto c2 = _mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 16), _mm256_set1_epi32(0xFF));
        auto c2mask = _mm256_castsi256_ps(_mm256_cmpgt_epi32(c0, _mm256_setzero_si256()));
        c2 = _mm256_sub_epi32(c0, _mm256_set1_epi32(1));

        x2.x = _mm256_mask_i32gather_ps(_mm256_setzero_ps(), sat.r, c2, c2mask, 4);
        x2.y = _mm256_mask_i32gather_ps(_mm256_setzero_ps(), sat.g, c2, c2mask, 4);
        x2.z = _mm256_mask_i32gather_ps(_mm256_setzero_ps(), sat.b, c2, c2mask, 4);
        w2 = _mm256_mask_i32gather_ps(_mm256_setzero_ps(), sat.w, c2, c2mask, 4);
#else
        // Load SAT elements.
        float * base = (float *)sat.x;

        __m256i c0 = _mm256_slli_epi32(packedClusterIndex, 2);
        c0 = _mm256_and_si256(c0, _mm256_set1_epi32(0x3FC));

        x0.x = _mm256_i32gather_ps(base + 0, c0, 4);
        x0.y = _mm256_i32gather_ps(base + 1, c0, 4);
        x0.z = _mm256_i32gather_ps(base + 2, c0, 4);
        w0 = _mm256_i32gather_ps(base + 3, c0, 4);

        __m256i c1 = _mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 6), _mm256_set1_epi32(0x3FC));
        x1.x = _mm256_i32gather_ps(base + 0, c1, 4);
        x1.y = _mm256_i32gather_ps(base + 1, c1, 4);
        x1.z = _mm256_i32gather_ps(base + 2, c1, 4);
        w1 = _mm256_i32gather_ps(base + 3, c1, 4);

        __m256i c2 = _mm256_and_si256(_mm256_srli_epi32(packedClusterIndex, 14), _mm256_set1_epi32(0x3FC));
        x2.x = _mm256_i32gather_ps(base + 0, c2, 4);
        x2.y = _mm256_i32gather_ps(base + 1, c2, 4);
        x2.z = _mm256_i32gather_ps(base + 2, c2, 4);
        w2 = _mm256_i32gather_ps(base + 3, c2, 4);
#endif

#else
        // Scalar path
        x0.x = vzero(); x0.y = vzero(); x0.z = vzero(); w0 = vzero();
        x1.x = vzero(); x1.y = vzero(); x1.z = vzero(); w1 = vzero();
        x2.x = vzero(); x2.y = vzero(); x2.z = vzero(); w2 = vzero();

        for (int l = 0; l < VEC_SIZE; l++) {
            uint c0 = s_fourCluster[i + l].c0;
            if (c0) {
                c0 -= 1;
                lane(x0.x, l) = sat.r[c0];
                lane(x0.y, l) = sat.g[c0];
                lane(x0.z, l) = sat.b[c0];
                lane(w0, l) = sat.w[c0];
            }

            uint c1 = s_fourCluster[i + l].c1;
            if (c1) {
                c1 -= 1;
                lane(x1.x, l) = sat.r[c1];
                lane(x1.y, l) = sat.g[c1];
                lane(x1.z, l) = sat.b[c1];
                lane(w1, l) = sat.w[c1];
            }

            uint c2 = s_fourCluster[i + l].c2;
            if (c2) {
                c2 -= 1;
                lane(x2.x, l) = sat.r[c2];
                lane(x2.y, l) = sat.g[c2];
                lane(x2.z, l) = sat.b[c2];
                lane(w2, l) = sat.w[c2];
            }
        }
#endif

        VFloat w3 = vbroadcast(w_sum) - w2;
        x2 = x2 - x1;
        x1 = x1 - x0;
        w2 = w2 - w1;
        w1 = w1 - w0;

        VFloat alpha2_sum = vmad(w2, vbroadcast(1.0f / 9.0f), vmad(w1, vbroadcast(4.0f / 9.0f), w0));
        VFloat beta2_sum  = vmad(w1, vbroadcast(1.0f / 9.0f), vmad(w2, vbroadcast(4.0f / 9.0f), w3));

        VFloat alphabeta_sum = (w1 + w2) * vbroadcast(2.0f / 9.0f);
        VFloat factor = vrcp(alpha2_sum * beta2_sum - alphabeta_sum * alphabeta_sum);

        VVector3 alphax_sum = vmad(x2, vbroadcast(1.0f / 3.0f), vmad(x1, vbroadcast(2.0f / 3.0f), x0));
        VVector3 betax_sum = vbroadcast(r_sum, g_sum, b_sum) - alphax_sum;

        VVector3 a = (alphax_sum * beta2_sum - betax_sum * alphabeta_sum) * factor;
        VVector3 b = (betax_sum * alpha2_sum - alphax_sum * alphabeta_sum) * factor;

        // clamp to the grid
        a = vsaturate(a);
        b = vsaturate(b);
        a = vround_ept(a);
        b = vround_ept(b);

        // compute the error
        VVector3 e1 = vmad(a * a, alpha2_sum, vmad(b * b, beta2_sum, (a * b * alphabeta_sum - a * alphax_sum - b * betax_sum) * vbroadcast(2.0f)));

        // apply the metric to the error term
        VFloat error = vdot(e1, vbroadcast(metric_sqr));

        // keep the solution if it wins
        auto mask = (error < vbesterror);
        
        // We could mask the unused lanes here, but instead set the invalid SAT entries to FLT_MAX.
        //mask = (mask & (vbroadcast(total_order_count) >= tid8(i))); // This doesn't seem to help. Is it OK to consider elements out of bounds?

        vbesterror = vselect(mask, vbesterror, error);
        vbeststart = vselect(mask, vbeststart, a);
        vbestend = vselect(mask, vbestend, b);
    }

    // Is there a better way to do this reduction?
    float besterror = FLT_MAX;    
    int bestindex;
    for (int i = 0; i < VEC_SIZE; i++) {
        if (lane(vbesterror, i) < besterror) {
            besterror = lane(vbesterror, i);
            bestindex = i;
        }
    }

    Vector3 beststart;
    Vector3 bestend;
    beststart.x = lane(vbeststart.x, bestindex);
    beststart.y = lane(vbeststart.y, bestindex);
    beststart.z = lane(vbeststart.z, bestindex);
    bestend.x = lane(vbestend.x, bestindex);
    bestend.y = lane(vbestend.y, bestindex);
    bestend.z = lane(vbestend.z, bestindex);

    *start = beststart;
    *end = bestend;
}


#if 0 || ICBC_FAST_CLUSTER_FIT

struct Precomp {
    //uint8 c0, c1, c2, c3;
    float alpha2_sum;
    float beta2_sum;
    float alphabeta_sum;
    float factor;
};

static ICBC_ALIGN_16 Precomp s_fourElement[969];
static ICBC_ALIGN_16 Precomp s_threeElement[153];

static void init_lsqr_tables() {

    // Precompute least square factors for all possible 4-cluster configurations.
    for (int c0 = 0, i = 0; c0 <= 16; c0++) {
        for (int c1 = 0; c1 <= 16 - c0; c1++) {
            for (int c2 = 0; c2 <= 16 - c0 - c1; c2++, i++) {
                int c3 = 16 - c0 - c1 - c2;

                //s_fourElement[i].c0 = c0;
                //s_fourElement[i].c1 = c0 + c1;
                //s_fourElement[i].c2 = c0 + c1 + c2;
                //s_fourElement[i].c3 = 16;

                //int alpha2_sum = c0 * 9 + c1 * 4 + c2;
                //int beta2_sum = c3 * 9 + c2 * 4 + c1;
                //int alphabeta_sum = (c1 + c2) * 2;
                //float factor = float(9.0 / (alpha2_sum * beta2_sum - alphabeta_sum * alphabeta_sum));

                float alpha2_sum = c0 + c1 * (4.0f / 9.0f) + c2 * (1.0f / 9.0f);
                float beta2_sum = c3 + c2 * (4.0f / 9.0f) + c1 * (1.0f / 9.0f);
                float alphabeta_sum = (c1 + c2) * (2.0f / 9.0f);
                float factor = float(1.0 / (alpha2_sum * beta2_sum - alphabeta_sum * alphabeta_sum));

                s_fourElement[i].alpha2_sum = (float)alpha2_sum;
                s_fourElement[i].beta2_sum = (float)beta2_sum;
                s_fourElement[i].alphabeta_sum = (float)alphabeta_sum;
                s_fourElement[i].factor = factor;
            }
        }
    }

    // Precompute least square factors for all possible 3-cluster configurations.
    for (int c0 = 0, i = 0; c0 <= 16; c0++) {
        for (int c1 = 0; c1 <= 16 - c0; c1++, i++) {
            int c2 = 16 - c1 - c0;

            //s_threeElement[i].c0 = c0;
            //s_threeElement[i].c1 = c1;
            //s_threeElement[i].c2 = c2;

            //int alpha2_sum = 4 * c0 + c1;
            //int beta2_sum = 4 * c2 + c1;
            //int alphabeta_sum = c1;
            //float factor = float(4.0 / (alpha2_sum * beta2_sum - alphabeta_sum * alphabeta_sum));

            float alpha2_sum = c0 + c1 * 0.25f;
            float beta2_sum = c2 + c1 * 0.25f;
            float alphabeta_sum = c1 * 0.25f;
            float factor = float(1.0 / (alpha2_sum * beta2_sum - alphabeta_sum * alphabeta_sum));

            s_threeElement[i].alpha2_sum = (float)alpha2_sum;
            s_threeElement[i].beta2_sum = (float)beta2_sum;
            s_threeElement[i].alphabeta_sum = (float)alphabeta_sum;
            s_threeElement[i].factor = factor;
        }
    }
}

// This is the ideal way to round, but it's too expensive to do this in the inner loop.
inline Vector3 round565(const Vector3 & v) {
    static const Vector3 grid = { 31.0f, 63.0f, 31.0f };
    static const Vector3 gridrcp = { 1.0f / 31.0f, 1.0f / 63.0f, 1.0f / 31.0f };

    Vector3 q = floor(grid * v);
    q.x += (v.x > midpoints5[int(q.x)]);
    q.y += (v.y > midpoints6[int(q.y)]);
    q.z += (v.z > midpoints5[int(q.z)]);
    q *= gridrcp;
    return q;
}

void ClusterFit::setColorSet(const Vector4 * colors, const Vector3 & metric)
{
    m_metricSqr = metric * metric;

    m_count = 16;

    static const float weights[16] = {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1};
    Vector3 vc[16];
    for (int i = 0; i < 16; i++) vc[i] = colors[i].xyz;

    // I've tried using a lower quality approximation of the principal direction, but the best fit line seems to produce best results.
    Vector3 principal = computePrincipalComponent_PowerMethod(16, vc, weights);

    // build the list of values
    int order[16];
    float dps[16];
    for (uint i = 0; i < 16; ++i)
    {
        order[i] = i;
        dps[i] = dot(colors[i].xyz, principal);

        if (colors[i].x < midpoints5[0] && colors[i].y < midpoints6[0] && colors[i].z < midpoints5[0]) anyBlack = true;
    }

    // stable sort
    for (uint i = 0; i < 16; ++i)
    {
        for (uint j = i; j > 0 && dps[j] < dps[j - 1]; --j)
        {
            swap(dps[j], dps[j - 1]);
            swap(order[j], order[j - 1]);
        }
    }

    for (uint i = 0; i < 16; ++i)
    {
        int p = order[i];
        m_colors[i] = colors[p].xyz;
        m_weights[i] = 1.0f;
    }
}

void ClusterFit::fastCompress3(Vector3 * start, Vector3 * end)
{
    ICBC_ASSERT(m_count == 16);
    const Vector3 grid = { 31.0f, 63.0f, 31.0f };
    const Vector3 gridrcp = { 1.0f / 31.0f, 1.0f / 63.0f, 1.0f / 31.0f };

    // declare variables
    Vector3 beststart = { 0.0f };
    Vector3 bestend = { 0.0f };
    float besterror = FLT_MAX;

    Vector3 x0 = { 0.0f };

    // check all possible clusters for this total order
    for (uint c0 = 0, i = 0; c0 <= 16; c0++)
    {
        Vector3 x1 = { 0.0f };

        for (uint c1 = 0; c1 <= 16 - c0; c1++, i++)
        {
            float const alpha2_sum = s_threeElement[i].alpha2_sum;
            float const beta2_sum = s_threeElement[i].beta2_sum;
            float const alphabeta_sum = s_threeElement[i].alphabeta_sum;
            float const factor = s_threeElement[i].factor;

            Vector3 const alphax_sum = x0 + x1 * 0.5f;
            Vector3 const betax_sum = Vector3(r_sum, g_sum, b_sum) - alphax_sum;

            Vector3 a = (alphax_sum * beta2_sum - betax_sum * alphabeta_sum) * factor;
            Vector3 b = (betax_sum * alpha2_sum - alphax_sum * alphabeta_sum) * factor;

            // clamp to the grid
            a = saturate(a);
            b = saturate(b);
#if ICBC_PERFECT_ROUND
            a = round565(a);
            b = round565(b);
#else
            a = round(grid * a) * gridrcp;
            b = round(grid * b) * gridrcp;
#endif

            // compute the error
            Vector3 e1 = a * a*alpha2_sum + b * b*beta2_sum + 2.0f*(a*b*alphabeta_sum - a * alphax_sum - b * betax_sum);

            // apply the metric to the error term
            float error = dot(e1, m_metricSqr);

            // keep the solution if it wins
            if (error < besterror)
            {
                besterror = error;
                beststart = a;
                bestend = b;
            }

            x1 += m_colors[c0 + c1];
        }

        x0 += m_colors[c0];
    }

    *start = beststart;
    *end = bestend;
}

void ClusterFit::fastCompress4(Vector3 * start, Vector3 * end)
{
    ICBC_ASSERT(m_count == 16);
    const Vector3 grid = { 31.0f, 63.0f, 31.0f };
    const Vector3 gridrcp = { 1.0f / 31.0f, 1.0f / 63.0f, 1.0f / 31.0f };

    // declare variables
    Vector3 beststart = { 0.0f };
    Vector3 bestend = { 0.0f };
    float besterror = FLT_MAX;

#if !ICBC_USE_SAT
    Vector3 x0 = { 0.0f };

    // check all possible clusters for this total order
    for (uint c0 = 0, i = 0; c0 <= 16; c0++)
    {
        Vector3 x1 = { 0.0f };

        for (uint c1 = 0; c1 <= 16 - c0; c1++)
        {
            Vector3 x2 = { 0.0f };

            for (uint c2 = 0; c2 <= 16 - c0 - c1; c2++, i++)
            {
                float const alpha2_sum = s_fourElement[i].alpha2_sum;
                float const beta2_sum = s_fourElement[i].beta2_sum;
                float const alphabeta_sum = s_fourElement[i].alphabeta_sum;
                float const factor = s_fourElement[i].factor;

                Vector3 const alphax_sum = x0 + x1 * (2.0f / 3.0f) + x2 * (1.0f / 3.0f);
                Vector3 const betax_sum = m_xsum - alphax_sum;

                Vector3 a = (alphax_sum * beta2_sum - betax_sum * alphabeta_sum) * factor;
                Vector3 b = (betax_sum * alpha2_sum - alphax_sum * alphabeta_sum) * factor;

                // clamp to the grid
                a = saturate(a);
                b = saturate(b);
#if ICBC_PERFECT_ROUND
                a = round565(a);
                b = round565(b);
#else
                a = round(grid * a) * gridrcp;
                b = round(grid * b) * gridrcp;
#endif
                // @@ It would be much more accurate to evaluate the error exactly. If we computed the error exactly, then
                //    we could bake the *factor in alpha2_sum, beta2_sum and alphabeta_sum.

                // compute the error
                Vector3 e1 = a * a*alpha2_sum + b * b*beta2_sum + 2.0f*(a*b*alphabeta_sum - a * alphax_sum - b * betax_sum);

                // apply the metric to the error term
                float error = dot(e1, m_metricSqr);

                // keep the solution if it wins
                if (error < besterror)
                {
                    besterror = error;
                    beststart = a;
                    bestend = b;
                }

                x2 += m_colors[c0 + c1 + c2];
            }

            x1 += m_colors[c0 + c1];
        }

        x0 += m_colors[c0];
    }
#else
    // This could be done in set colors.
    Vector3 x_sat[17];
    //float w_sat[17];

    Vector3 x_sum = { 0 };
    //float w_sum = 0;
    for (uint i = 0; i < 17; i++) {
        x_sat[i] = x_sum;
        //w_sat[i] = w_sum;
        x_sum += m_colors[i];
        //w_sum += m_weights[i];
    }

    // Ideas:
    // - Use this method with weighted colors. Use only precomputed counts and summed area tables. Compute alpha/beta factors on the fly.
    // - We can precompute tables for reduced number of colors. Are these tables subset of each other? Can we simply change the sum? Yes!
    // - Only do SIMD version of this loop. Lay down table and sat optimally for AVX access patterns.

    // check all possible clusters for this total order
    for (uint i = 0; i < 969; i+=1)
    {
        int c0 = s_fourElement[i].c0;
        int c01 = s_fourElement[i].c1;
        int c012 = s_fourElement[i].c2;

        Vector3 x0 = x_sat[c0];
        //float w0 = w_sat[c0];

        Vector3 x1 = x_sat[c01] - x_sat[c0];
        //float w1 = w_sat[c01] - w_sat[c0];

        Vector3 x2 = x_sat[c012] - x_sat[c01];
        //float w2 = w_sat[c012] - w_sat[c01];

        float const alpha2_sum = s_fourElement[i].alpha2_sum;
        float const beta2_sum = s_fourElement[i].beta2_sum;
        float const alphabeta_sum = s_fourElement[i].alphabeta_sum;
        float const factor = s_fourElement[i].factor;

        Vector3 const alphax_sum = x0 + x1 * (2.0f / 3.0f) + x2 * (1.0f / 3.0f);
        Vector3 const betax_sum = m_xsum - alphax_sum;

        Vector3 a = (alphax_sum * beta2_sum - betax_sum * alphabeta_sum) * factor;
        Vector3 b = (betax_sum * alpha2_sum - alphax_sum * alphabeta_sum) * factor;

        // clamp to the grid
        a = saturate(a);
        b = saturate(b);
#if ICBC_PERFECT_ROUND
        a = round565(a);
        b = round565(b);
#else
        a = round(grid * a) * gridrcp;
        b = round(grid * b) * gridrcp;
#endif
        // @@ It would be much more accurate to evaluate the error exactly. 

        // compute the error
        Vector3 e1 = a * a*alpha2_sum + b * b*beta2_sum +2.0f*(a*b*alphabeta_sum - a * alphax_sum - b * betax_sum);

        // apply the metric to the error term
        float error = dot(e1, m_metricSqr);

        // keep the solution if it wins
        if (error < besterror)
        {
            besterror = error;
            beststart = a;
            bestend = b;
        }
    }
#endif

    *start = beststart;
    *end = bestend;
}
#endif // ICBC_FAST_CLUSTER_FIT



///////////////////////////////////////////////////////////////////////////////////////////////////
// Palette evaluation.

// D3D10
inline void evaluate_palette4_d3d10(Color16 c0, Color16 c1, Color32 palette[4]) {
    palette[2].r = (2 * palette[0].r + palette[1].r) / 3;
    palette[2].g = (2 * palette[0].g + palette[1].g) / 3;
    palette[2].b = (2 * palette[0].b + palette[1].b) / 3;
    palette[2].a = 0xFF;

    palette[3].r = (2 * palette[1].r + palette[0].r) / 3;
    palette[3].g = (2 * palette[1].g + palette[0].g) / 3;
    palette[3].b = (2 * palette[1].b + palette[0].b) / 3;
    palette[3].a = 0xFF;
}
inline void evaluate_palette3_d3d10(Color16 c0, Color16 c1, Color32 palette[4]) {
    palette[2].r = (palette[0].r + palette[1].r) / 2;
    palette[2].g = (palette[0].g + palette[1].g) / 2;
    palette[2].b = (palette[0].b + palette[1].b) / 2;
    palette[2].a = 0xFF;
    palette[3].u = 0;
}
static void evaluate_palette_d3d10(Color16 c0, Color16 c1, Color32 palette[4]) {
    palette[0] = bitexpand_color16_to_color32(c0);
    palette[1] = bitexpand_color16_to_color32(c1);
    if (c0.u > c1.u) {
        evaluate_palette4_d3d10(c0, c1, palette);
    }
    else {
        evaluate_palette3_d3d10(c0, c1, palette);
    }
}

// NV
inline void evaluate_palette4_nv(Color16 c0, Color16 c1, Color32 palette[4]) {
    int gdiff = palette[1].g - palette[0].g;
    palette[2].r = ((2 * c0.r + c1.r) * 22) / 8;
    palette[2].g = (256 * palette[0].g + gdiff / 4 + 128 + gdiff * 80) / 256;
    palette[2].b = ((2 * c0.b + c1.b) * 22) / 8;
    palette[2].a = 0xFF;

    palette[3].r = ((2 * c1.r + c0.r) * 22) / 8;
    palette[3].g = (256 * palette[1].g - gdiff / 4 + 128 - gdiff * 80) / 256;
    palette[3].b = ((2 * c1.b + c0.b) * 22) / 8;
    palette[3].a = 0xFF;
}
inline void evaluate_palette3_nv(Color16 c0, Color16 c1, Color32 palette[4]) {
    int gdiff = palette[1].g - palette[0].g;
    palette[2].r = ((c0.r + c1.r) * 33) / 8;
    palette[2].g = (256 * palette[0].g + gdiff / 4 + 128 + gdiff * 128) / 256;
    palette[2].b = ((c0.b + c1.b) * 33) / 8;
    palette[2].a = 0xFF;
    palette[3].u = 0;
}
static void evaluate_palette_nv(Color16 c0, Color16 c1, Color32 palette[4]) {
    palette[0] = bitexpand_color16_to_color32(c0);
    palette[1] = bitexpand_color16_to_color32(c1);

    if (c0.u > c1.u) {
        evaluate_palette4_nv(c0, c1, palette);
    }
    else {
        evaluate_palette3_nv(c0, c1, palette);
    }
}

// AMD
inline void evaluate_palette4_amd(Color16 c0, Color16 c1, Color32 palette[4]) {
    palette[2].r = (43 * palette[0].r + 21 * palette[1].r + 32) / 8;
    palette[2].g = (43 * palette[0].g + 21 * palette[1].g + 32) / 8;
    palette[2].b = (43 * palette[0].b + 21 * palette[1].b + 32) / 8;
    palette[2].a = 0xFF;

    palette[3].r = (43 * palette[1].r + 21 * palette[0].r + 32) / 8;
    palette[3].g = (43 * palette[1].g + 21 * palette[0].g + 32) / 8;
    palette[3].b = (43 * palette[1].b + 21 * palette[0].b + 32) / 8;
    palette[3].a = 0xFF;
}
inline void evaluate_palette3_amd(Color16 c0, Color16 c1, Color32 palette[4]) {
    palette[2].r = (c0.r + c1.r + 1) / 2;
    palette[2].g = (c0.g + c1.g + 1) / 2;
    palette[2].b = (c0.b + c1.b + 1) / 2;
    palette[2].a = 0xFF;
    palette[3].u = 0;
}
static void evaluate_palette_amd(Color16 c0, Color16 c1, Color32 palette[4]) {
    palette[0] = bitexpand_color16_to_color32(c0);
    palette[1] = bitexpand_color16_to_color32(c1);

    if (c0.u > c1.u) {
        evaluate_palette4_amd(c0, c1, palette);
    }
    else {
        evaluate_palette3_amd(c0, c1, palette);
    }
}

// Use ICBC_DECODER to determine decoder used.
inline void evaluate_palette4(Color16 c0, Color16 c1, Color32 palette[4]) {
#if ICBC_DECODER == Decoder_D3D10
    evaluate_palette4_d3d10(c0, c1, palette);
#elif ICBC_DECODER == Decoder_NVIDIA
    evaluate_palette4_nv(c0, c1, palette);
#elif ICBC_DECODER == Decoder_AMD
    evaluate_palette4_amd(c0, c1, palette);
#endif
}
inline void evaluate_palette3(Color16 c0, Color16 c1, Color32 palette[4]) {
#if ICBC_DECODER == Decoder_D3D10
    evaluate_palette3_d3d10(c0, c1, palette);
#elif ICBC_DECODER == Decoder_NVIDIA
    evaluate_palette3_nv(c0, c1, palette);
#elif ICBC_DECODER == Decoder_AMD
    evaluate_palette3_amd(c0, c1, palette);
#endif
}
inline void evaluate_palette(Color16 c0, Color16 c1, Color32 palette[4]) {
#if ICBC_DECODER == Decoder_D3D10
    evaluate_palette_d3d10(c0, c1, palette);
#elif ICBC_DECODER == Decoder_NVIDIA
    evaluate_palette_nv(c0, c1, palette);
#elif ICBC_DECODER == Decoder_AMD
    evaluate_palette_amd(c0, c1, palette);
#endif
}

static void evaluate_palette(Color16 c0, Color16 c1, Vector3 palette[4]) {
    Color32 palette32[4];
    evaluate_palette(c0, c1, palette32);

    for (int i = 0; i < 4; i++) {
        palette[i] = color_to_vector3(palette32[i]);
    }
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Error evaluation.

// Different ways of estimating the error.

static float evaluate_mse(const Vector3 & p, const Vector3 & c, const Vector3 & w) {
    Vector3 d = (p - c) * w * 255;
    return dot(d, d);
}

static float evaluate_mse(const Color32 & p, const Vector3 & c, const Vector3 & w) {
    Vector3 d = (color_to_vector3(p) - c) * w * 255;
    return dot(d, d);
}


/*static float evaluate_mse(const Vector3 & p, const Vector3 & c, const Vector3 & w) {
    return ww.x * square(p.x-c.x) + ww.y * square(p.y-c.y) + ww.z * square(p.z-c.z);
}*/

static int evaluate_mse(const Color32 & p, const Color32 & c) {
    return (square(int(p.r)-c.r) + square(int(p.g)-c.g) + square(int(p.b)-c.b));
}

/*static float evaluate_mse(const Vector3 palette[4], const Vector3 & c, const Vector3 & w) {
    float e0 = evaluate_mse(palette[0], c, w);
    float e1 = evaluate_mse(palette[1], c, w);
    float e2 = evaluate_mse(palette[2], c, w);
    float e3 = evaluate_mse(palette[3], c, w);
    return min(min(e0, e1), min(e2, e3));
}*/

static int evaluate_mse(const Color32 palette[4], const Color32 & c) {
    int e0 = evaluate_mse(palette[0], c);
    int e1 = evaluate_mse(palette[1], c);
    int e2 = evaluate_mse(palette[2], c);
    int e3 = evaluate_mse(palette[3], c);
    return min(min(e0, e1), min(e2, e3));
}

// Returns MSE error in [0-255] range.
static int evaluate_mse(const BlockDXT1 * output, Color32 color, int index) {
    Color32 palette[4];
    evaluate_palette(output->col0, output->col1, palette);

    return evaluate_mse(palette[index], color);
}

// Returns weighted MSE error in [0-255] range.
static float evaluate_palette_error(Color32 palette[4], const Color32 * colors, const float * weights, int count) {
    
    float total = 0.0f;
    for (int i = 0; i < count; i++) {
        total += weights[i] * evaluate_mse(palette, colors[i]);
    }

    return total;
}

static float evaluate_palette_error(Color32 palette[4], const Color32 * colors, int count) {

    float total = 0.0f;
    for (int i = 0; i < count; i++) {
        total += evaluate_mse(palette, colors[i]);
    }

    return total;
}

static float evaluate_mse(const Vector4 input_colors[16], const float input_weights[16], const Vector3 & color_weights, const BlockDXT1 * output) {
    Color32 palette[4];
    evaluate_palette(output->col0, output->col1, palette);

    // evaluate error for each index.
    float error = 0.0f;
    for (int i = 0; i < 16; i++) {
        int index = (output->indices >> (2 * i)) & 3;
        error += input_weights[i] * evaluate_mse(palette[index], input_colors[i].xyz, color_weights);
    }
    return error;
}

float evaluate_dxt1_error(const uint8 rgba_block[16*4], const BlockDXT1 * block, Decoder decoder) {
    Color32 palette[4];
    if (decoder == Decoder_NVIDIA) {
        evaluate_palette_nv(block->col0, block->col1, palette);
    }
    else if (decoder == Decoder_AMD) {
        evaluate_palette_amd(block->col0, block->col1, palette);
    }
    else {
        evaluate_palette(block->col0, block->col1, palette);
    }

    // evaluate error for each index.
    float error = 0.0f;
    for (int i = 0; i < 16; i++) {
        int index = (block->indices >> (2 * i)) & 3;
        Color32 c;
        c.r = rgba_block[4 * i + 0];
        c.g = rgba_block[4 * i + 1];
        c.b = rgba_block[4 * i + 2];
        c.a = 255;
        error += evaluate_mse(palette[index], c);
    }
    return error;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Index selection

static uint compute_indices4(const Vector4 input_colors[16], const Vector3 & color_weights, const Vector3 palette[4]) {
    
    uint indices = 0;
    for (int i = 0; i < 16; i++) {
        float d0 = evaluate_mse(palette[0], input_colors[i].xyz, color_weights);
        float d1 = evaluate_mse(palette[1], input_colors[i].xyz, color_weights);
        float d2 = evaluate_mse(palette[2], input_colors[i].xyz, color_weights);
        float d3 = evaluate_mse(palette[3], input_colors[i].xyz, color_weights);

        uint b0 = d0 > d3;
        uint b1 = d1 > d2;
        uint b2 = d0 > d2;
        uint b3 = d1 > d3;
        uint b4 = d2 > d3;

        uint x0 = b1 & b2;
        uint x1 = b0 & b3;
        uint x2 = b0 & b4;

        indices |= (x2 | ((x0 | x1) << 1)) << (2 * i);
    }

    return indices;
}


static uint compute_indices4(const Vector3 input_colors[16], const Vector3 palette[4]) {

    uint indices = 0;
    for (int i = 0; i < 16; i++) {
        float d0 = evaluate_mse(palette[0], input_colors[i], {1,1,1});
        float d1 = evaluate_mse(palette[1], input_colors[i], {1,1,1});
        float d2 = evaluate_mse(palette[2], input_colors[i], {1,1,1});
        float d3 = evaluate_mse(palette[3], input_colors[i], {1,1,1});

        uint b0 = d0 > d3;
        uint b1 = d1 > d2;
        uint b2 = d0 > d2;
        uint b3 = d1 > d3;
        uint b4 = d2 > d3;

        uint x0 = b1 & b2;
        uint x1 = b0 & b3;
        uint x2 = b0 & b4;

        indices |= (x2 | ((x0 | x1) << 1)) << (2 * i);
    }

    return indices;
}


static uint compute_indices(const Vector4 input_colors[16], const Vector3 & color_weights, const Vector3 palette[4]) {
    
    uint indices = 0;
    for (int i = 0; i < 16; i++) {
        float d0 = evaluate_mse(palette[0], input_colors[i].xyz, color_weights);
        float d1 = evaluate_mse(palette[1], input_colors[i].xyz, color_weights);
        float d2 = evaluate_mse(palette[2], input_colors[i].xyz, color_weights);
        float d3 = evaluate_mse(palette[3], input_colors[i].xyz, color_weights);

        uint index;
        if (d0 < d1 && d0 < d2 && d0 < d3) index = 0;
        else if (d1 < d2 && d1 < d3) index = 1;
        else if (d2 < d3) index = 2;
        else index = 3;

		indices |= index << (2 * i);
	}

	return indices;
}


static void output_block3(const Vector4 input_colors[16], const Vector3 & color_weights, const Vector3 & v0, const Vector3 & v1, BlockDXT1 * block)
{
    Color16 color0 = vector3_to_color16(v0);
    Color16 color1 = vector3_to_color16(v1);

    if (color0.u > color1.u) {
        swap(color0, color1);
    }

    Vector3 palette[4];
    evaluate_palette(color0, color1, palette);

    block->col0 = color0;
    block->col1 = color1;
    block->indices = compute_indices(input_colors, color_weights, palette);
}

static void output_block4(const Vector4 input_colors[16], const Vector3 & color_weights, const Vector3 & v0, const Vector3 & v1, BlockDXT1 * block)
{
    Color16 color0 = vector3_to_color16(v0);
    Color16 color1 = vector3_to_color16(v1);

    if (color0.u < color1.u) {
        swap(color0, color1);
    }

    Vector3 palette[4];
    evaluate_palette(color0, color1, palette);

    block->col0 = color0;
    block->col1 = color1;
    block->indices = compute_indices4(input_colors, color_weights, palette);
}


static void output_block4(const Vector3 input_colors[16], const Vector3 & v0, const Vector3 & v1, BlockDXT1 * block)
{
    Color16 color0 = vector3_to_color16(v0);
    Color16 color1 = vector3_to_color16(v1);

    if (color0.u < color1.u) {
        swap(color0, color1);
    }

    Vector3 palette[4];
    evaluate_palette(color0, color1, palette);

    block->col0 = color0;
    block->col1 = color1;
    block->indices = compute_indices4(input_colors, palette);
}

// Least squares fitting of color end points for the given indices. @@ Take weights into account.
static bool optimize_end_points4(uint indices, const Vector4 * colors, /*const float * weights,*/ int count, Vector3 * a, Vector3 * b)
{
    float alpha2_sum = 0.0f;
    float beta2_sum = 0.0f;
    float alphabeta_sum = 0.0f;
    Vector3 alphax_sum = { 0,0,0 };
    Vector3 betax_sum = { 0,0,0 };

    for (int i = 0; i < count; i++)
    {
        const uint bits = indices >> (2 * i);

        float beta = float(bits & 1);
        if (bits & 2) beta = (1 + beta) / 3.0f;
        float alpha = 1.0f - beta;

        alpha2_sum += alpha * alpha;
        beta2_sum += beta * beta;
        alphabeta_sum += alpha * beta;
        alphax_sum += alpha * colors[i].xyz;
        betax_sum += beta * colors[i].xyz;
    }

    float denom = alpha2_sum * beta2_sum - alphabeta_sum * alphabeta_sum;
    if (equal(denom, 0.0f)) return false;

    float factor = 1.0f / denom;

    *a = saturate((alphax_sum * beta2_sum - betax_sum * alphabeta_sum) * factor);
    *b = saturate((betax_sum * alpha2_sum - alphax_sum * alphabeta_sum) * factor);

    return true;
}

// Least squares optimization with custom factors.
// This allows us passing the standard [1, 0, 2/3 1/3] weights by default, but also use alternative mappings when the number of clusters is not 4.
static bool optimize_end_points4(uint indices, const Vector3 * colors, int count, float factors[4], Vector3 * a, Vector3 * b)
{
    float alpha2_sum = 0.0f;
    float beta2_sum = 0.0f;
    float alphabeta_sum = 0.0f;
    Vector3 alphax_sum = { 0,0,0 };
    Vector3 betax_sum = { 0,0,0 };

    for (int i = 0; i < count; i++)
    {
        const uint idx = (indices >> (2 * i)) & 3;
        float alpha = factors[idx];
        float beta = 1 - alpha;

        alpha2_sum += alpha * alpha;
        beta2_sum += beta * beta;
        alphabeta_sum += alpha * beta;
        alphax_sum += alpha * colors[i];
        betax_sum += beta * colors[i];
    }

    float denom = alpha2_sum * beta2_sum - alphabeta_sum * alphabeta_sum;
    if (equal(denom, 0.0f)) return false;

    float factor = 1.0f / denom;

    *a = saturate((alphax_sum * beta2_sum - betax_sum * alphabeta_sum) * factor);
    *b = saturate((betax_sum * alpha2_sum - alphax_sum * alphabeta_sum) * factor);

    return true;
}

static bool optimize_end_points4(uint indices, const Vector3 * colors, int count, Vector3 * a, Vector3 * b)
{
    float factors[4] = { 1, 0, 2.f / 3, 1.f / 3 };
    return optimize_end_points4(indices, colors, count, factors, a, b);
}


// Least squares fitting of color end points for the given indices. @@ This does not support black/transparent index. @@ Take weights into account.
static bool optimize_end_points3(uint indices, const Vector3 * colors, /*const float * weights,*/ int count, Vector3 * a, Vector3 * b)
{
    float alpha2_sum = 0.0f;
    float beta2_sum = 0.0f;
    float alphabeta_sum = 0.0f;
    Vector3 alphax_sum = { 0,0,0 };
    Vector3 betax_sum = { 0,0,0 };

    for (int i = 0; i < count; i++)
    {
        const uint bits = indices >> (2 * i);

        float beta = float(bits & 1);
        if (bits & 2) beta = 0.5f;
        float alpha = 1.0f - beta;

        alpha2_sum += alpha * alpha;
        beta2_sum += beta * beta;
        alphabeta_sum += alpha * beta;
        alphax_sum += alpha * colors[i];
        betax_sum += beta * colors[i];
    }

    float denom = alpha2_sum * beta2_sum - alphabeta_sum * alphabeta_sum;
    if (equal(denom, 0.0f)) return false;

    float factor = 1.0f / denom;

    *a = saturate((alphax_sum * beta2_sum - betax_sum * alphabeta_sum) * factor);
    *b = saturate((betax_sum * alpha2_sum - alphax_sum * alphabeta_sum) * factor);

    return true;
}


// find minimum and maximum colors based on bounding box in color space
inline static void fit_colors_bbox(const Vector3 * colors, int count, Vector3 * __restrict c0, Vector3 * __restrict c1)
{
    *c0 = { 0,0,0 };
    *c1 = { 1,1,1 };

    for (int i = 0; i < count; i++) {
        *c0 = max(*c0, colors[i]);
        *c1 = min(*c1, colors[i]);
    }
}

inline static void select_diagonal(const Vector3 * colors, int count, Vector3 * __restrict c0, Vector3 * __restrict c1)
{
    Vector3 center = (*c0 + *c1) * 0.5f;

    /*Vector3 center = colors[0];
    for (int i = 1; i < count; i++) {
        center = center * float(i-1) / i + colors[i] / i;
    }*/
    /*Vector3 center = colors[0];
    for (int i = 1; i < count; i++) {
        center += colors[i];
    }
    center /= count;*/

    float cov_xz = 0.0f;
    float cov_yz = 0.0f;
    for (int i = 0; i < count; i++) {
        Vector3 t = colors[i] - center;
        cov_xz += t.x * t.z;
        cov_yz += t.y * t.z;
    }

    float x0 = c0->x;
    float y0 = c0->y;
    float x1 = c1->x;
    float y1 = c1->y;

    if (cov_xz < 0) {
        swap(x0, x1);
    }
    if (cov_yz < 0) {
        swap(y0, y1);
    }

    *c0 = { x0, y0, c0->z };
    *c1 = { x1, y1, c1->z };
}

inline static void inset_bbox(Vector3 * __restrict c0, Vector3 * __restrict c1)
{
    float bias = (8.0f / 255.0f) / 16.0f;
    Vector3 inset = (*c0 - *c1) / 16.0f - scalar_to_vector3(bias);
    *c0 = saturate(*c0 - inset);
    *c1 = saturate(*c1 + inset);
}



// Single color lookup tables from:
// https://github.com/nothings/stb/blob/master/stb_dxt.h
static uint8 s_match5[256][2];
static uint8 s_match6[256][2];

static inline int Lerp13(int a, int b)
{
    // replace "/ 3" by "* 0xaaab) >> 17" if your compiler sucks or you really need every ounce of speed.
    return (a * 2 + b) / 3;
}

static void PrepareOptTable(uint8 * table, const uint8 * expand, int size)
{
    for (int i = 0; i < 256; i++) {
        int bestErr = 256 * 100;

        for (int min = 0; min < size; min++) {
            for (int max = 0; max < size; max++) {
                int mine = expand[min];
                int maxe = expand[max];

                int err = abs(Lerp13(maxe, mine) - i) * 100;

                // DX10 spec says that interpolation must be within 3% of "correct" result,
                // add this as error term. (normally we'd expect a random distribution of
                // +-1.5% error, but nowhere in the spec does it say that the error has to be
                // unbiased - better safe than sorry).
                err += abs(max - min) * 3;

                if (err < bestErr) {
                    bestErr = err;
                    table[i * 2 + 0] = max;
                    table[i * 2 + 1] = min;
                }
            }
        }
    }
}

static void init_dxt1_tables()
{
    // Prepare single color lookup tables.
    uint8 expand5[32];
    uint8 expand6[64];
    for (int i = 0; i < 32; i++) expand5[i] = (i << 3) | (i >> 2);
    for (int i = 0; i < 64; i++) expand6[i] = (i << 2) | (i >> 4);

    PrepareOptTable(&s_match5[0][0], expand5, 32);
    PrepareOptTable(&s_match6[0][0], expand6, 64);
}

// Single color compressor, based on:
// https://mollyrocket.com/forums/viewtopic.php?t=392
static void compress_dxt1_single_color_optimal(Color32 c, BlockDXT1 * output)
{
    output->col0.r = s_match5[c.r][0];
    output->col0.g = s_match6[c.g][0];
    output->col0.b = s_match5[c.b][0];
    output->col1.r = s_match5[c.r][1];
    output->col1.g = s_match6[c.g][1];
    output->col1.b = s_match5[c.b][1];
    output->indices = 0xaaaaaaaa;
    
    if (output->col0.u < output->col1.u)
    {
        swap(output->col0.u, output->col1.u);
        output->indices ^= 0x55555555;
    }
}


// Compress block using the average color.
static float compress_dxt1_single_color(const Vector3 * colors, const float * weights, int count, const Vector3 & color_weights, BlockDXT1 * output)
{
    // Compute block average.
    Vector3 color_sum = { 0,0,0 };
    float weight_sum = 0;

    for (int i = 0; i < count; i++) {
        color_sum += colors[i] * weights[i];
        weight_sum += weights[i];
    }

    // Compress optimally.
    compress_dxt1_single_color_optimal(vector3_to_color32(color_sum / weight_sum), output);

    // Decompress block color.
    Color32 palette[4];
    evaluate_palette(output->col0, output->col1, palette);

    Vector3 block_color = color_to_vector3(palette[output->indices & 0x3]);

    // Evaluate error.
    float error = 0;
    for (int i = 0; i < count; i++) {
        error += weights[i] * evaluate_mse(block_color, colors[i], color_weights);
    }
    return error;
}

static float compress_dxt1_cluster_fit(const Vector4 input_colors[16], const float input_weights[16], const Vector3 * colors, const float * weights, int count, const Vector3 & color_weights, bool three_color_mode, bool use_transparent_black, BlockDXT1 * output)
{
    Vector3 metric_sqr = color_weights * color_weights;

    SummedAreaTable sat;
    int sat_count = compute_sat(colors, weights, count, &sat);

    Vector3 start, end;
    cluster_fit_four(sat, sat_count, metric_sqr, &start, &end);

    output_block4(input_colors, color_weights, start, end, output);

    float best_error = evaluate_mse(input_colors, input_weights, color_weights, output);

    if (three_color_mode) {
        if (use_transparent_black) {
            Vector3 tmp_colors[16];
            float tmp_weights[16];
            int tmp_count = skip_blacks(colors, weights, count, tmp_colors, tmp_weights);
            if (!tmp_count) return best_error;

            sat_count = compute_sat(tmp_colors, tmp_weights, tmp_count, &sat);
        }

        cluster_fit_three(sat, sat_count, metric_sqr, &start, &end);

        BlockDXT1 three_color_block;
        output_block3(input_colors, color_weights, start, end, &three_color_block);

        float three_color_error = evaluate_mse(input_colors, input_weights, color_weights, &three_color_block);

        if (three_color_error < best_error) {
            best_error = three_color_error;
            *output = three_color_block;
        }
    }

    return best_error;

    /*
    ClusterFit fit;
    fit.setColorSet(colors, weights, count, color_weights);

    // start & end are in [0, 1] range.
    Vector3 start, end;
    fit.compress4(&start, &end);
    
    output_block4(input_colors, color_weights, start, end, output);

    float best_error = evaluate_mse(input_colors, input_weights, color_weights, output);

    if (three_color_mode) {
        if (fit.anyBlack) {
            Vector3 tmp_colors[16];
            float tmp_weights[16];
            int tmp_count = skip_blacks(colors, weights, count, tmp_colors, tmp_weights);
            if (!tmp_count) return FLT_MAX;

            fit.setColorSet(tmp_colors, tmp_weights, tmp_count, color_weights);

            fit.compress3(&start, &end);
        }
        else {
            fit.compress3(&start, &end);
        }

        BlockDXT1 three_color_block;
        output_block3(input_colors, color_weights, start, end, &three_color_block);

        float three_color_error = evaluate_mse(input_colors, input_weights, color_weights, &three_color_block);

        if (three_color_error < best_error) {
            best_error = three_color_error;
            *output = three_color_block;
        }
    }

    return best_error;
    */
}


static float refine_endpoints(const Vector4 input_colors[16], const float input_weights[16], const Vector3 & color_weights, bool three_color_mode, float input_error, BlockDXT1 * output) {
    // TODO:
    // - Optimize palette evaluation when updating only one channel.
    // - try all diagonals.

    // Things that don't help:
    // - Alternate endpoint updates.
    // - Randomize order.
    // - If one direction does not improve, test opposite direction next.

    static const int8 deltas[16][3] = {
        {1,0,0},
        {0,1,0},
        {0,0,1},

        {-1,0,0},
        {0,-1,0},
        {0,0,-1},

        {1,1,0},
        {1,0,1},
        {0,1,1},

        {-1,-1,0},
        {-1,0,-1},
        {0,-1,-1},

        {-1,1,0},
        //{-1,0,1},

        {1,-1,0},
        {0,-1,1},

        //{1,0,-1},
        {0,1,-1},
    };

    float best_error = input_error;

    int lastImprovement = 0;
    for (int i = 0; i < 256; i++) {
        BlockDXT1 refined = *output;
        int8 delta[3] = { deltas[i % 16][0], deltas[i % 16][1], deltas[i % 16][2] };

        if ((i / 16) & 1) {
            refined.col0.r += delta[0];
            refined.col0.g += delta[1];
            refined.col0.b += delta[2];
        }
        else {
            refined.col1.r += delta[0];
            refined.col1.g += delta[1];
            refined.col1.b += delta[2];
        }

        if (!three_color_mode) {
            if (refined.col0.u == refined.col1.u) refined.col1.g += 1;
            if (refined.col0.u < refined.col1.u) swap(refined.col0.u, refined.col1.u);
        }

        Vector3 palette[4];
        evaluate_palette(output->col0, output->col1, palette);

        refined.indices = compute_indices(input_colors, color_weights, palette);

        float refined_error = evaluate_mse(input_colors, input_weights, color_weights, &refined);
        if (refined_error < best_error) {
            best_error = refined_error;
            *output = refined;
            lastImprovement = i;
        }

        // Early out if the last 32 steps didn't improve error.
        if (i - lastImprovement > 32) break;
    }

    return best_error;
}


static float compress_dxt1(const Vector4 input_colors[16], const float input_weights[16], const Vector3 & color_weights, bool three_color_mode, bool hq, BlockDXT1 * output)
{
    Vector3 colors[16];
    float weights[16];
    bool use_transparent_black = false;
    int count = reduce_colors(input_colors, input_weights, 16, colors, weights, &use_transparent_black);

    if (count == 0) {
        // Output trivial block.
        output->col0.u = 0;
        output->col1.u = 0;
        output->indices = 0;
        return 0;
    }

    // Cluster fit cannot handle single color blocks, so encode them optimally.
    if (count == 1) {
        compress_dxt1_single_color_optimal(vector3_to_color32(colors[0]), output);
        return evaluate_mse(input_colors, input_weights, color_weights, output);
    }

    // Quick end point selection.
    Vector3 c0, c1;
    fit_colors_bbox(colors, count, &c0, &c1);
    inset_bbox(&c0, &c1);
    select_diagonal(colors, count, &c0, &c1);
    output_block4(input_colors, color_weights, c0, c1, output);

    float error = evaluate_mse(input_colors, input_weights, color_weights, output);

    // Refine color for the selected indices.
    if (optimize_end_points4(output->indices, input_colors, 16, &c0, &c1)) {
        BlockDXT1 optimized_block;
        output_block4(input_colors, color_weights, c0, c1, &optimized_block);

        float optimized_error = evaluate_mse(input_colors, input_weights, color_weights, &optimized_block);
        if (optimized_error < error) {
            error = optimized_error;
            *output = optimized_block;
        }
    }
    //float error = FLT_MAX;

    // @@ Use current endpoints as input for initial PCA approximation?

    // Try cluster fit.
    BlockDXT1 cluster_fit_output;
    float cluster_fit_error = compress_dxt1_cluster_fit(input_colors, input_weights, colors, weights, count, color_weights, three_color_mode, use_transparent_black, &cluster_fit_output);
    if (cluster_fit_error < error) {
        *output = cluster_fit_output;
        error = cluster_fit_error;
    }

    if (hq) {
        error = refine_endpoints(input_colors, input_weights, color_weights, three_color_mode, error, output);
    }

    return error;
}


// 
static bool centroid_end_points(uint indices, const Vector3 * colors, /*const float * weights,*/ float factor[4], Vector3 * c0, Vector3 * c1) {

    *c0 = { 0,0,0 };
    *c1 = { 0,0,0 };
    float w0_sum = 0;
    float w1_sum = 0;

    for (int i = 0; i < 16; i++) {
        int idx = (indices >> (2 * i)) & 3;
        float w0 = factor[idx];// * weights[i];
        float w1 = (1 - factor[idx]);// * weights[i];

        *c0 += colors[i] * w0;   w0_sum += w0;
        *c1 += colors[i] * w1;   w1_sum += w1;
    }

    *c0 *= (1.0f / w0_sum);
    *c1 *= (1.0f / w1_sum);

    return true;
}



static float compress_dxt1_test(const Vector4 input_colors[16], const float input_weights[16], const Vector3 & color_weights, BlockDXT1 * output)
{
    Vector3 colors[16];
    for (int i = 0; i < 16; i++) {
        colors[i] = input_colors[i].xyz;
    }
    int count = 16;

    // Quick end point selection.
    Vector3 c0, c1;
    fit_colors_bbox(colors, count, &c0, &c1);
    if (c0 == c1) {
        compress_dxt1_single_color_optimal(vector3_to_color32(c0), output);
        return evaluate_mse(input_colors, input_weights, color_weights, output);
    }
    inset_bbox(&c0, &c1);
    select_diagonal(colors, count, &c0, &c1);

    output_block4(colors, c0, c1, output);
    float best_error = evaluate_mse(input_colors, input_weights, color_weights, output);


    // Given an index assignment, we can compute end points in two different ways:
    // - least squares optimization.
    // - centroid.
    // Are these different? The first finds the end points that minimize the least squares error.
    // The second averages the input colors

    while (true) {
        float last_error = best_error;
        uint last_indices = output->indices;

        int cluster_counts[4] = { 0, 0, 0, 0 };
        for (int i = 0; i < 16; i++) {
            int idx = (output->indices >> (2 * i)) & 3;
            cluster_counts[idx] += 1;
        }
        int n = 0;
        for (int i = 0; i < 4; i++) n += int(cluster_counts[i] != 0);

        if (n == 4) {
            float factors[4] = { 1.0f, 0.0f, 2.0f / 3, 1.0f / 3 };
            if (optimize_end_points4(last_indices, colors, 16, factors, &c0, &c1)) {
                BlockDXT1 refined_block;
                output_block4(colors, c0, c1, &refined_block);
                float new_error = evaluate_mse(input_colors, input_weights, color_weights, &refined_block);
                if (new_error < best_error) {
                    best_error = new_error;
                    *output = refined_block;
                }
            }
        }
        else if (n == 3) {
            // 4 options:
            static const float tables[4][3] = {
                { 0, 2.f/3, 1.f/3 },    // 0, 1/3, 2/3
                { 1, 0,     1.f/3 },    // 0, 1/3, 1
                { 1, 0,     2.f/3 },    // 0, 2/3, 1
                { 1, 2.f/3, 1.f/3 },    // 1/2, 2/3, 1
            };

            for (int k = 0; k < 4; k++) {
                // Remap tables:
                float factors[4];
                for (int i = 0, j = 0; i < 4; i++) {
                    factors[i] = tables[k][j];
                    if (cluster_counts[i] != 0) j += 1;
                }
                if (optimize_end_points4(last_indices, colors, 16, factors, &c0, &c1)) {
                    BlockDXT1 refined_block;
                    output_block4(colors, c0, c1, &refined_block);
                    float new_error = evaluate_mse(input_colors, input_weights, color_weights, &refined_block);
                    if (new_error < best_error) {
                        best_error = new_error;
                        *output = refined_block;
                    }
                }
            }

            // @@ And 1 3-color block:
            // 0, 1/2, 1
        }
        else if (n == 2) {

            // 6 options:
            static const float tables[6][2] = {
                { 0, 1.f/3 },       // 0, 1/3
                { 0, 2.f/3 },       // 0, 2/3
                { 1, 0 },           // 0, 1
                { 2.f/3, 1.f/3 },   // 1/3, 2/3
                { 1, 1.f/3 },       // 1/3, 1
                { 1, 2.f/3 },       // 2/3, 1
            };

            for (int k = 0; k < 6; k++) {
                // Remap tables:
                float factors[4];
                for (int i = 0, j = 0; i < 4; i++) {
                    factors[i] = tables[k][j];
                    if (cluster_counts[i] != 0) j += 1;
                }
                if (optimize_end_points4(last_indices, colors, 16, factors, &c0, &c1)) {
                    BlockDXT1 refined_block;
                    output_block4(colors, c0, c1, &refined_block);
                    float new_error = evaluate_mse(input_colors, input_weights, color_weights, &refined_block);
                    if (new_error < best_error) {
                        best_error = new_error;
                        *output = refined_block;
                    }
                }
            }

            // @@ And 2 3-color blocks:
            // 0, 0.5
            // 0.5, 1
            // 0, 1     // This is equivalent to the 4 color mode.
        }

        // If error has not improved, stop.
        //if (best_error == last_error) break;

        // If error has not improved or indices haven't changed, stop.
        if (output->indices == last_indices || best_error < last_error) break;
    }

    if (false) {
        best_error = refine_endpoints(input_colors, input_weights, color_weights, false, best_error, output);
    }

    return best_error;
}



static float compress_dxt1_fast(const Vector4 input_colors[16], const float input_weights[16], const Vector3 & color_weights, BlockDXT1 * output)
{
    Vector3 colors[16];
    for (int i = 0; i < 16; i++) {
        colors[i] = input_colors[i].xyz;
    }
    int count = 16;

    /*float error = FLT_MAX;
    error = compress_dxt1_single_color(colors, input_weights, count, color_weights, output);

    if (error == 0.0f || count == 1) {
        // Early out.
        return error;
    }*/

    // Quick end point selection.
    Vector3 c0, c1;
    fit_colors_bbox(colors, count, &c0, &c1);
    if (c0 == c1) {
        compress_dxt1_single_color_optimal(vector3_to_color32(c0), output);
        return evaluate_mse(input_colors, input_weights, color_weights, output);
    }
    inset_bbox(&c0, &c1);
    select_diagonal(colors, count, &c0, &c1);
    output_block4(input_colors, color_weights, c0, c1, output);

    // Refine color for the selected indices.
    if (optimize_end_points4(output->indices, input_colors, 16, &c0, &c1)) {
        output_block4(input_colors, color_weights, c0, c1, output);
    }

    return evaluate_mse(input_colors, input_weights, color_weights, output);
}


static void compress_dxt1_fast(const uint8 input_colors[16*4], BlockDXT1 * output) {

    Vector3 vec_colors[16];
    for (int i = 0; i < 16; i++) {
        vec_colors[i] = { input_colors[4 * i + 0] / 255.0f, input_colors[4 * i + 1] / 255.0f, input_colors[4 * i + 2] / 255.0f };
    }

    // Quick end point selection.
    Vector3 c0, c1;
    //fit_colors_bbox(colors, count, &c0, &c1);
    //select_diagonal(colors, count, &c0, &c1);
    fit_colors_bbox(vec_colors, 16, &c0, &c1);
    if (c0 == c1) {
        compress_dxt1_single_color_optimal(vector3_to_color32(c0), output);
        return;
    }
    inset_bbox(&c0, &c1);
    select_diagonal(vec_colors, 16, &c0, &c1);
    output_block4(vec_colors, c0, c1, output);

    // Refine color for the selected indices.
    if (optimize_end_points4(output->indices, vec_colors, 16, &c0, &c1)) {
        output_block4(vec_colors, c0, c1, output);
    }
}

// Public API

void init_dxt1() {
    init_dxt1_tables();
    init_cluster_tables();
#if ICBC_FAST_CLUSTER_FIT
    init_lsqr_tables();
#endif
}

float compress_dxt1(const float input_colors[16 * 4], const float input_weights[16], const float rgb[3], bool three_color_mode, bool hq, void * output) {
    return compress_dxt1((Vector4*)input_colors, input_weights, { rgb[0], rgb[1], rgb[2] }, three_color_mode, hq, (BlockDXT1*)output);
}

float compress_dxt1_fast(const float input_colors[16 * 4], const float input_weights[16], const float rgb[3], void * output) {
    return compress_dxt1_fast((Vector4*)input_colors, input_weights, { rgb[0], rgb[1], rgb[2] }, (BlockDXT1*)output);
}

void compress_dxt1_fast(const unsigned char input_colors[16 * 4], void * output) {
    compress_dxt1_fast(input_colors, (BlockDXT1*)output);
}

void compress_dxt1_test(const float input_colors[16 * 4], const float input_weights[16], const float rgb[3], void * output) {
    compress_dxt1_test((Vector4*)input_colors, input_weights, { rgb[0], rgb[1], rgb[2] }, (BlockDXT1*)output);
}

float evaluate_dxt1_error(const unsigned char rgba_block[16 * 4], const void * dxt_block, Decoder decoder/*=Decoder_D3D10*/) {
    return evaluate_dxt1_error(rgba_block, (BlockDXT1 *)dxt_block, decoder);
}

} // icbc

// Do not polute preprocessor definitions.
#undef ICBC_DECODER
#undef ICBC_USE_SPMD
#undef ICBC_ASSERT

#endif // ICBC_IMPLEMENTATION

// Version History:
// v1.00 - Initial release.
// v1.01 - Added SPMD code path with AVX support.
// v1.02 - Removed SIMD code path.

// Copyright (c) 2020 Ignacio Castano <castano@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to	deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
