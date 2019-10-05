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

#include "../nacs-spcm/data_stream_p.h"

using namespace NaCs;
using namespace NaCs::Spcm;

#define OUT_ATTR __restrict__ __attribute__((aligned(64)))
#define PARAM_ATTR __restrict__

struct channel_param_fixed {
    float phase;
    float freq;
    float amp;
};

struct channel_param {
    const float *phase;
    const float *freq;
    const float *dfreq;
    const float *amp;
    const float *damp;
};

static NACS_INLINE void leak_data(const void *p)
{
    asm volatile ("" :: "r"(p): "memory");
}

template<typename Gen>
static NACS_INLINE void _run_wave_fixed(float *data, size_t sz, size_t rep, int nchn,
                                        const channel_param_fixed *params_fixed)
{
    // Note that this implementation does not forward the phase so it does not
    // compute a continuous sine wave.
    // However, this better represents the actual calculation.
    // The long term phase (which is converted to the initial phase) tracked with
    // an integer so we'll not have any large phase accumulation on floating point
    // numbers.
    assume(rep > 0);
    assume(sz > 0);
    for (size_t r = 0; r < rep; r++) {
        for (size_t offset = 0; offset < sz; offset += step_size) {
            leak_data(&nchn);
            leak_data(params_fixed);
            Gen::calc_wave_fixed(&data[offset], nchn, params_fixed);
        }
    }
}

template<typename Gen>
static NACS_INLINE void _run_wave(float *data, size_t sz, size_t rep, int nchn,
                                  const channel_param *params)
{
    assume(rep > 0);
    assume(sz > 0);
    for (size_t r = 0; r < rep; r++) {
        for (size_t offset = 0; offset < sz; offset += step_size) {
            leak_data(&nchn);
            leak_data(params);
            Gen::calc_wave(&data[offset], nchn, params, offset / step_size);
        }
    }
}

// The generators for non-default implementations implement this class
// to add the correct target attribute so that the inlining is allowed.
// However, since the implementation of the loop (`_run_wave` and `_run_wave_fixed`)
// shared by all implementation cannot have target attribute,
// the `cal_wave` and `cal_wave_fixed` still cannot be `always_inline` or
// the compiler will complain about not able to inline even though the function
// that got inlined into is always inlined into a function that has the correct
// target attribute and therefore can be inlined into.
// What does work, then, is to mark the runner as `flatten`,
// which is allowed to inline multiple layers of functions despite more generic target
// in the middle level.
template<typename Gen>
struct Runner {
    template<typename... Args>
    static void __attribute__((flatten)) run_wave_fixed(Args&&... args)
    {
        _run_wave_fixed<Gen>(std::forward<Args>(args)...);
    }
    template<typename... Args>
    static void __attribute__((flatten)) run_wave(Args&&... args)
    {
        _run_wave<Gen>(std::forward<Args>(args)...);
    }
};

struct ScalarGen {
    static NACS_INLINE void calc_wave_fixed(float *OUT_ATTR output, int nchns,
                                            const channel_param_fixed *PARAM_ATTR params)
    {
        assume(nchns > 0);
        for (int i = 0; i < step_size; i++) {
            float o = 0;
            for (int c = 0; c < nchns; c++) {
                auto p = params[c];
                o += scalar::calc_single_chn(i, p.phase, p.freq, p.amp);
            }
            output[i] = o;
        }
    }
    static NACS_INLINE void calc_wave(float *OUT_ATTR output, int nchns,
                                      const channel_param *PARAM_ATTR params,
                                      size_t param_idx)
    {
        assume(nchns > 0);
        for (int i = 0; i < step_size; i++) {
            float o = 0;
            for (int c = 0; c < nchns; c++) {
                auto p = params[c];
                o += scalar::calc_single_chn(i, p.phase[param_idx], p.freq[param_idx],
                                             p.amp[param_idx], p.dfreq[param_idx],
                                             p.damp[param_idx]);
            }
            output[i] = o;
        }
    }
};

#if NACS_CPU_X86 || NACS_CPU_X86_64
struct SSE2Gen {
    static inline __attribute__((target("sse2")))
    void calc_wave_fixed(float *OUT_ATTR output, int nchns,
                         const channel_param_fixed *PARAM_ATTR params)
    {
        assume(nchns > 0);
        for (int i = 0; i < step_size; i += 4) {
            auto o = _mm_set1_ps(0);
            for (int c = 0; c < nchns; c++) {
                auto p = params[c];
                o += sse2::calc_single_chn(i, p.phase, p.freq, p.amp);
            }
            _mm_store_ps(&output[i], o);
        }
    }
    static inline __attribute__((target("sse2")))
    void calc_wave(float *OUT_ATTR output, int nchns,
                   const channel_param *PARAM_ATTR params, size_t param_idx)
    {
        assume(nchns > 0);
        for (int i = 0; i < step_size; i += 4) {
            auto o = _mm_set1_ps(0);
            for (int c = 0; c < nchns; c++) {
                auto p = params[c];
                o += sse2::calc_single_chn(i, p.phase[param_idx], p.freq[param_idx],
                                           p.amp[param_idx], p.dfreq[param_idx],
                                           p.damp[param_idx]);
            }
            _mm_store_ps(&output[i], o);
        }
    }
};
template<>
struct Runner<SSE2Gen> {
    template<typename... Args>
    static void __attribute__((target("sse2"), flatten))
    run_wave_fixed(Args&&... args)
    {
        _run_wave_fixed<SSE2Gen>(std::forward<Args>(args)...);
    }
    template<typename... Args>
    static void __attribute__((target("sse2"), flatten))
    run_wave(Args&&... args)
    {
        _run_wave<SSE2Gen>(std::forward<Args>(args)...);
    }
};

struct AVXGen {
    static inline __attribute__((target("avx")))
    void calc_wave_fixed(float *OUT_ATTR output, int nchns,
                         const channel_param_fixed *PARAM_ATTR params)
    {
        assume(nchns > 0);
        for (int i = 0; i < step_size; i += 8) {
            auto o = _mm256_set1_ps(0);
            for (int c = 0; c < nchns; c++) {
                auto p = params[c];
                o += avx::calc_single_chn(i, p.phase, p.freq, p.amp);
            }
            _mm256_store_ps(&output[i], o);
        }
    }
    static inline __attribute__((target("avx")))
    void calc_wave(float *OUT_ATTR output, int nchns,
                   const channel_param *PARAM_ATTR params, size_t param_idx)
    {
        assume(nchns > 0);
        for (int i = 0; i < step_size; i += 8) {
            auto o = _mm256_set1_ps(0);
            for (int c = 0; c < nchns; c++) {
                auto p = params[c];
                o += avx::calc_single_chn(i, p.phase[param_idx], p.freq[param_idx],
                                          p.amp[param_idx], p.dfreq[param_idx],
                                          p.damp[param_idx]);
            }
            _mm256_store_ps(&output[i], o);
        }
    }
};
template<>
struct Runner<AVXGen> {
    template<typename... Args>
    static void __attribute__((target("avx"), flatten))
    run_wave_fixed(Args&&... args)
    {
        _run_wave_fixed<AVXGen>(std::forward<Args>(args)...);
    }
    template<typename... Args>
    static void __attribute__((target("avx"), flatten))
    run_wave(Args&&... args)
    {
        _run_wave<AVXGen>(std::forward<Args>(args)...);
    }
};

struct AVX2Gen {
    static inline __attribute__((target("avx2,fma")))
    void calc_wave_fixed(float *OUT_ATTR output, int nchns,
                         const channel_param_fixed *PARAM_ATTR params)
    {
        assume(nchns > 0);
        for (int i = 0; i < step_size; i += 8) {
            auto o = _mm256_set1_ps(0);
            for (int c = 0; c < nchns; c++) {
                auto p = params[c];
                o += avx2::calc_single_chn(i, p.phase, p.freq, p.amp);
            }
            _mm256_store_ps(&output[i], o);
        }
    }
    static inline __attribute__((target("avx2,fma")))
    void calc_wave(float *OUT_ATTR output, int nchns,
                   const channel_param *PARAM_ATTR params, size_t param_idx)
    {
        assume(nchns > 0);
        for (int i = 0; i < step_size; i += 8) {
            auto o = _mm256_set1_ps(0);
            for (int c = 0; c < nchns; c++) {
                auto p = params[c];
                o += avx2::calc_single_chn(i, p.phase[param_idx], p.freq[param_idx],
                                           p.amp[param_idx], p.dfreq[param_idx],
                                           p.damp[param_idx]);
            }
            _mm256_store_ps(&output[i], o);
        }
    }
};
template<>
struct Runner<AVX2Gen> {
    template<typename... Args>
    static void __attribute__((target("avx2,fma"), flatten))
    run_wave_fixed(Args&&... args)
    {
        _run_wave_fixed<AVX2Gen>(std::forward<Args>(args)...);
    }
    template<typename... Args>
    static void __attribute__((target("avx2,fma"), flatten))
    run_wave(Args&&... args)
    {
        _run_wave<AVX2Gen>(std::forward<Args>(args)...);
    }
};

struct AVX512Gen {
    static inline __attribute__((target("avx512f,avx512dq")))
    void calc_wave_fixed(float *OUT_ATTR output, int nchns,
                         const channel_param_fixed *PARAM_ATTR params)
    {
        assume(nchns > 0);
        for (int i = 0; i < step_size; i += 16) {
            auto o = _mm512_set1_ps(0);
            for (int c = 0; c < nchns; c++) {
                auto p = params[c];
                o += avx512::calc_single_chn(i, p.phase, p.freq, p.amp);
            }
            _mm512_store_ps(&output[i], o);
        }
    }
    static inline __attribute__((target("avx512f,avx512dq")))
    void calc_wave(float *OUT_ATTR output, int nchns,
                   const channel_param *PARAM_ATTR params, size_t param_idx)
    {
        assume(nchns > 0);
        for (int i = 0; i < step_size; i += 16) {
            auto o = _mm512_set1_ps(0);
            for (int c = 0; c < nchns; c++) {
                auto p = params[c];
                o += avx512::calc_single_chn(i, p.phase[param_idx], p.freq[param_idx],
                                             p.amp[param_idx], p.dfreq[param_idx],
                                             p.damp[param_idx]);
            }
            _mm512_store_ps(&output[i], o);
        }
    }
};
template<>
struct Runner<AVX512Gen> {
    template<typename... Args>
    static void __attribute__((target("avx512f,avx512dq"), flatten))
    run_wave_fixed(Args&&... args)
    {
        _run_wave_fixed<AVX512Gen>(std::forward<Args>(args)...);
    }
    template<typename... Args>
    static void __attribute__((target("avx512f,avx512dq"), flatten))
    run_wave(Args&&... args)
    {
        _run_wave<AVX512Gen>(std::forward<Args>(args)...);
    }
};
#endif
