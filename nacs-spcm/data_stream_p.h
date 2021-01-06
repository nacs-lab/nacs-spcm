/*************************************************************************
 *   Copyright (c) 2019 - 2019 Yichao Yu <yyc1992@gmail.com>             *
 *                                                                       *
 *   This library is free software; you can redistribute it and/or       *
 *   modify it under the terms of the GNU Lesser General Public          *
 *   License as published by the Free Software Foundation; either        *
 *   version 3.0 of the License, or (at your option) any later version.  *
 *                                                                       *
 *   This library is distributed in the hope that it will be useful,     *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of      *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    *
 *   Lesser General Public License for more details.                     *
 *                                                                       *
 *   You should have received a copy of the GNU Lesser General Public    *
 *   License along with this library. If not,                            *
 *   see <http://www.gnu.org/licenses/>.                                 *
 *************************************************************************/

#ifndef _NACS_SPCM_DATA_STREAM_P_H
#define _NACS_SPCM_DATA_STREAM_P_H

#include "data_stream.h"

#include <nacs-utils/utils.h>

#if NACS_CPU_X86 || NACS_CPU_X86_64
#  include <immintrin.h>
#elif NACS_CPU_AARCH64
#  include <arm_neon.h>
#endif

namespace NaCs {
namespace Spcm {

namespace {

// This is the number of samples we compute on a linear amplitude and frequency slope.
constexpr int step_size = 32;

}

template<typename T>
static NACS_INLINE void accum_nonzero(T &out, T in, float s)
{
    // Due to floating point arithmetics, it's hard for the compiler to remove the
    // `+= in * s` even when `s == 0`.
    // We therefore implement the constant propagation manually so that the computation
    // is skipped when `s` is known to be `0` at compile time and there won't be
    // any branches in any case.
    // The `in * s` might be a SIMD operation but since we are using the builtin
    // compiler support for generic vector type instead of target specific
    // intrinsic functions, we shouldn't get a complaint about not able to inline
    // the operation into this function that lacks the target attribute.
    if (__builtin_constant_p(s) && s == 0)
        return;
    out += in * s;
}

namespace scalar {

// Calculate `sin(pi * d) / pi`
// By computing `sin(pi * d)` instead of `sin(d)`, we don't need to do additional scaling of `d`
// before converting it to integer. Then by computing `sin(pi * d) / pi` the first order
// expansion term is kept as `d` which saves another multiplication at the end.
// We still need to generate the correct output from the correct input in the end
// so we still need the correct input/output scaling.
// However, the input needs to be scaled anyway so we can fold the input scaling in there,
// for the output, we can scale it once after all the channels are computed instead
// of doing it once per channel.
static NACS_INLINE float sinpif_pi(float d)
{
#if NACS_CPU_X86 || NACS_CPU_X86_64
    int q = _mm_cvtss_si32(_mm_set_ss(d));
#elif NACS_CPU_AARCH64
    int q = vcvtns_s32_f32(d);
#else
    int q = d < 0 ? (int)(d - 0.5f) : (int)(d + 0.5f);
#endif
    // Now `d` is the fractional part in the range `[-0.5, 0.5]`
    d = d - (float)q;
    auto s = d * d;

    // For the original `d` in the range (0.5, 1.5), (2.5, 3.5) etc
    // their value is the same as (-0.5, 0.5) with a sign flip in the input.
    if (q & 1)
        d = -d;

    // These coefficients are numerically optimized to
    // give the smallest maximum error over [0, 4] / [-4, 4]
    // The maximum error is ~4.08e-7.
    // When only limited to [0, 0.5], the error could be reduced to ~2.9e-7
    // Either should be good enough since the output only has 16bit resolution.
    auto u = -0.17818783f * s + 0.8098674f;
    u = u * s - 1.6448531f;
    return (s * d) * u + d;
}

static constexpr float tidx_2[] = {
    0.0, 0.001953125, 0.0078125, 0.017578125, 0.03125, 0.048828125, 0.0703125, 0.095703125,
    0.125, 0.158203125, 0.1953125, 0.236328125, 0.28125, 0.330078125, 0.3828125, 0.439453125,
    0.5, 0.564453125, 0.6328125, 0.705078125, 0.78125, 0.861328125, 0.9453125, 1.033203125,
    1.125, 1.220703125, 1.3203125, 1.423828125, 1.53125, 1.642578125, 1.7578125, 1.876953125};
static_assert(sizeof(tidx_2) == sizeof(float) * step_size, "");

// Phase is in unit of pi
// Frequency of 1 means one full cycle per 32 samples.
static NACS_INLINE float calc_single_chn(int i, float phase, float freq, float amp,
                                         float dfreq=0, float damp=0)
{
    assume(0 <= i && i < step_size);
    auto tscale = 0.0625f * (float)i;
    auto tscale_2 = tidx_2[i];
    phase += tscale * freq;
    accum_nonzero(phase, tscale_2, dfreq);
    accum_nonzero(amp, tscale, damp);
    return sinpif_pi(phase) * amp;
}

} // namespace scalar

#if NACS_CPU_X86 || NACS_CPU_X86_64

// This is basically how GCC implements the corresponding intel intrinsics (`_mm*_set_ps`).
// However, the intrinsics are not implemented as `constexpr`
// and using the constructor directly makes GCC 7(.4) unhappy when used in `const`/`constexpr`
// array initializers which causes errors like
// > invalid type ‘__m512 ...’ as initializer for a vector of type ‘const __m512 ...’.
// Wrapping in a `constexpr` function works around that issue while maintaining the
// `constexpr`ness of the initializer.
template<typename T, typename... Args>
static constexpr NACS_INLINE T set_ps(Args... args)
{
    static_assert(sizeof...(args) * sizeof(float) == sizeof(T), "");
    return T{float(args)...};
}

namespace sse2 {

static NACS_INLINE __attribute__((target("sse2")))
__m128 sinpif_pi(__m128 d)
{
    __m128i q = _mm_cvtps_epi32(d);
    d = d - _mm_cvtepi32_ps(q);

    __m128 s = d * d;

    // Shift the last bit of `q` to the sign bit
    // and therefore flip the sign of `d` if `q` is odd
    d = __m128(_mm_slli_epi32(q, 31) ^ __m128i(d));

    auto u = -0.17818783f * s + 0.8098674f;
    u = u * s - 1.6448531f;
    return (s * d) * u + d;
}

static constexpr __m128 tidx[] = {
    set_ps<__m128>(0.0, 0.0625, 0.125, 0.1875), set_ps<__m128>(0.25, 0.3125, 0.375, 0.4375),
    set_ps<__m128>(0.5, 0.5625, 0.625, 0.6875), set_ps<__m128>(0.75, 0.8125, 0.875, 0.9375),
    set_ps<__m128>(1.0, 1.0625, 1.125, 1.1875), set_ps<__m128>(1.25, 1.3125, 1.375, 1.4375),
    set_ps<__m128>(1.5, 1.5625, 1.625, 1.6875), set_ps<__m128>(1.75, 1.8125, 1.875, 1.9375)};
static constexpr __m128 tidx_2[] = {
    set_ps<__m128>(0.0, 0.001953125, 0.0078125, 0.017578125),
    set_ps<__m128>(0.03125, 0.048828125, 0.0703125, 0.095703125),
    set_ps<__m128>(0.125, 0.158203125, 0.1953125, 0.236328125),
    set_ps<__m128>(0.28125, 0.330078125, 0.3828125, 0.439453125),
    set_ps<__m128>(0.5, 0.564453125, 0.6328125, 0.705078125),
    set_ps<__m128>(0.78125, 0.861328125, 0.9453125, 1.033203125),
    set_ps<__m128>(1.125, 1.220703125, 1.3203125, 1.423828125),
    set_ps<__m128>(1.53125, 1.642578125, 1.7578125, 1.876953125)};
static_assert(sizeof(tidx) == sizeof(float) * step_size, "");
static_assert(sizeof(tidx_2) == sizeof(float) * step_size, "");

// Phase is in unit of pi
// Frequency of 1 means one full cycle per 32 samples.
static NACS_INLINE __attribute__((target("sse2")))
__m128 calc_single_chn(int i, float _phase, float freq, float _amp,
                       float dfreq=0, float damp=0)
{
    assume(0 <= i && i < step_size && i % 4 == 0);
    auto tscale = tidx[i / 4];
    auto tscale_2 = tidx_2[i / 4];
    auto phase = _phase + tscale * freq;
    accum_nonzero(phase, tscale_2, dfreq);
    auto amp = _mm_set1_ps(_amp);
    accum_nonzero(amp, tscale, damp);
    return sinpif_pi(phase) * amp;
}

} // namespace sse2

namespace avx {

static NACS_INLINE __attribute__((target("avx")))
__m256 sinpif_pi(__m256 d)
{
    __m256i q = _mm256_cvtps_epi32(d);
    d = d - _mm256_cvtepi32_ps(q);

    __m256 s = d * d;

    // Shift the last bit of `q` to the sign bit
    // and therefore flip the sign of `d` if `q` is odd
    __m128i tmp[2] = {_mm256_castsi256_si128(q), _mm256_extractf128_si256(q, 1)};
    tmp[0] = _mm_slli_epi32(tmp[0], 31);
    tmp[1] = _mm_slli_epi32(tmp[1], 31);
    auto mask = _mm256_castsi128_si256(tmp[0]);
    mask = _mm256_insertf128_si256(mask, tmp[1], 1);
    d = __m256(mask ^ __m256i(d));

    auto u = -0.17818783f * s + 0.8098674f;
    u = u * s - 1.6448531f;
    return (s * d) * u + d;
}

static constexpr __m256 tidx[] = {
    set_ps<__m256>(0.0, 0.0625, 0.125, 0.1875, 0.25, 0.3125, 0.375, 0.4375),
    set_ps<__m256>(0.5, 0.5625, 0.625, 0.6875, 0.75, 0.8125, 0.875, 0.9375),
    set_ps<__m256>(1.0, 1.0625, 1.125, 1.1875, 1.25, 1.3125, 1.375, 1.4375),
    set_ps<__m256>(1.5, 1.5625, 1.625, 1.6875, 1.75, 1.8125, 1.875, 1.9375)};
static constexpr __m256 tidx_2[] = {
    set_ps<__m256>(0.0, 0.001953125, 0.0078125, 0.017578125,
                   0.03125, 0.048828125, 0.0703125, 0.095703125),
    set_ps<__m256>(0.125, 0.158203125, 0.1953125, 0.236328125,
                   0.28125, 0.330078125, 0.3828125, 0.439453125),
    set_ps<__m256>(0.5, 0.564453125, 0.6328125, 0.705078125,
                   0.78125, 0.861328125, 0.9453125, 1.033203125),
    set_ps<__m256>(1.125, 1.220703125, 1.3203125, 1.423828125,
                   1.53125, 1.642578125, 1.7578125, 1.876953125)};
static_assert(sizeof(tidx) == sizeof(float) * step_size, "");
static_assert(sizeof(tidx_2) == sizeof(float) * step_size, "");

// Phase is in unit of pi
// Frequency of 1 means one full cycle per 32 samples.
static NACS_INLINE __attribute__((target("avx")))
__m256 calc_single_chn(int i, float _phase, float freq, float _amp,
                       float dfreq=0, float damp=0)
{
    assume(0 <= i && i < step_size && i % 8 == 0);
    auto tscale = tidx[i / 8];
    auto tscale_2 = tidx_2[i / 8];
    auto phase = _phase + tscale * freq;
    accum_nonzero(phase, tscale_2, dfreq);
    auto amp = _mm256_set1_ps(_amp);
    accum_nonzero(amp, tscale, damp);
    return sinpif_pi(phase) * amp;
}

} // namespace avx

namespace avx2 {

static NACS_INLINE __attribute__((target("avx2,fma")))
__m256 sinpif_pi(__m256 d)
{
    __m256i q = _mm256_cvtps_epi32(d);
    d = d - _mm256_cvtepi32_ps(q);

    __m256 s = d * d;

    // Shift the last bit of `q` to the sign bit
    // and therefore flip the sign of `d` if `q` is odd
    d = __m256(_mm256_slli_epi32(q, 31) ^ __m256i(d));

    auto u = -0.17818783f * s + 0.8098674f;
    u = u * s - 1.6448531f;
    return (s * d) * u + d;
}

static constexpr __m256 tidx[] = {
    set_ps<__m256>(0.0, 0.0625, 0.125, 0.1875, 0.25, 0.3125, 0.375, 0.4375),
    set_ps<__m256>(0.5, 0.5625, 0.625, 0.6875, 0.75, 0.8125, 0.875, 0.9375),
    set_ps<__m256>(1.0, 1.0625, 1.125, 1.1875, 1.25, 1.3125, 1.375, 1.4375),
    set_ps<__m256>(1.5, 1.5625, 1.625, 1.6875, 1.75, 1.8125, 1.875, 1.9375)};
static constexpr __m256 tidx_2[] = {
    set_ps<__m256>(0.0, 0.001953125, 0.0078125, 0.017578125,
                   0.03125, 0.048828125, 0.0703125, 0.095703125),
    set_ps<__m256>(0.125, 0.158203125, 0.1953125, 0.236328125,
                   0.28125, 0.330078125, 0.3828125, 0.439453125),
    set_ps<__m256>(0.5, 0.564453125, 0.6328125, 0.705078125,
                   0.78125, 0.861328125, 0.9453125, 1.033203125),
    set_ps<__m256>(1.125, 1.220703125, 1.3203125, 1.423828125,
                   1.53125, 1.642578125, 1.7578125, 1.876953125)};
static_assert(sizeof(tidx) == sizeof(float) * step_size, "");
static_assert(sizeof(tidx_2) == sizeof(float) * step_size, "");

// Phase is in unit of pi
// Frequency of 1 means one full cycle per 32 samples.
static NACS_INLINE __attribute__((target("avx2,fma")))
__m256 calc_single_chn(int i, float _phase, float freq, float _amp,
                       float dfreq=0, float damp=0)
{
    assume(0 <= i && i < step_size && i % 8 == 0);
    auto tscale = tidx[i / 8];
    auto tscale_2 = tidx_2[i / 8];
    auto phase = _phase + tscale * freq;
    accum_nonzero(phase, tscale_2, dfreq);
    auto amp = _mm256_set1_ps(_amp);
    accum_nonzero(amp, tscale, damp);
    return sinpif_pi(phase) * amp;
}

} // namespace avx2

namespace avx512 {

static NACS_INLINE __attribute__((target("avx512f,avx512dq")))
__m512 sinpif_pi(__m512 d)
{
    __m512i q = _mm512_cvtps_epi32(d);
    d = d - _mm512_cvtepi32_ps(q);

    __m512 s = d * d;

    d = __m512(_mm512_slli_epi32(q, 31) ^ __m512i(d));

    auto u = -0.17818783f * s + 0.8098674f;
    u = u * s - 1.6448531f;
    return (s * d) * u + d;
}

static constexpr __m512 tidx[] = {
    set_ps<__m512>(0.0, 0.0625, 0.125, 0.1875, 0.25, 0.3125, 0.375, 0.4375,
                   0.5, 0.5625, 0.625, 0.6875, 0.75, 0.8125, 0.875, 0.9375),
    set_ps<__m512>(1.0, 1.0625, 1.125, 1.1875, 1.25, 1.3125, 1.375, 1.4375,
                   1.5, 1.5625, 1.625, 1.6875, 1.75, 1.8125, 1.875, 1.9375)};
static constexpr __m512 tidx_2[] = {
    set_ps<__m512>(0.0, 0.001953125, 0.0078125, 0.017578125,
                   0.03125, 0.048828125, 0.0703125, 0.095703125,
                   0.125, 0.158203125, 0.1953125, 0.236328125,
                   0.28125, 0.330078125, 0.3828125, 0.439453125),
    set_ps<__m512>(0.5, 0.564453125, 0.6328125, 0.705078125,
                   0.78125, 0.861328125, 0.9453125, 1.033203125,
                   1.125, 1.220703125, 1.3203125, 1.423828125,
                   1.53125, 1.642578125, 1.7578125, 1.876953125)};
static_assert(sizeof(tidx) == sizeof(float) * step_size, "");
static_assert(sizeof(tidx_2) == sizeof(float) * step_size, "");

// Phase is in unit of pi
// Frequency of 1 means one full cycle per 32 samples.
static NACS_INLINE __attribute__((target("avx512f,avx512dq")))
__m512 calc_single_chn(int i, float _phase, float freq, float _amp,
                       float dfreq=0, float damp=0)
{
    assume(0 <= i && i < step_size && i % 16 == 0);
    auto tscale = tidx[i / 16];
    auto tscale_2 = tidx_2[i / 16];
    auto phase = _phase + tscale * freq;
    accum_nonzero(phase, tscale_2, dfreq);
    auto amp = _mm512_set1_ps(_amp);
    accum_nonzero(amp, tscale, damp);
    return sinpif_pi(phase) * amp;
}

} // namespace avx512
#endif

}
}

#endif
