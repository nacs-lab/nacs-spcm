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

#include "calc_wave_helper.h"

#include <nacs-utils/mem.h>
#include <nacs-utils/number.h>
#include <nacs-utils/processor.h>
#include <nacs-utils/timer.h>

#include <assert.h>

#include <cmath>
#include <iostream>
#include <random>
#include <vector>

using namespace NaCs;
using namespace NaCs::Spcm;

static double calc_wave_fixed(float *output, int nchns, const channel_param_fixed *params)
{
    assert(nchns > 0);
    double total_amp = 0;
    for (int i = 0; i < step_size; i++) {
        double o = 0;
        for (int c = 0; c < nchns; c++) {
            auto p = params[c];
            auto phase = (double)p.phase + (double)p.freq * (double)i / 16;
            o += std::sin(phase * M_PI) / M_PI * (double)p.amp;
            total_amp += p.amp;
        }
        output[i] = (float)o;
    }
    return total_amp;
}

static double calc_wave(float *output, int nchns, const channel_param *params)
{
    assert(nchns > 0);
    double total_amp = 0;
    for (int i = 0; i < step_size; i++) {
        double o = 0;
        for (int c = 0; c < nchns; c++) {
            auto p = params[c];
            auto phase = (double)p.phase[0] + (double)p.freq[0] * (double)i / 16;
            phase += (double)p.dfreq[0] * (double)(i * i) / 512;
            auto amp = (double)p.amp[0] + (double)p.damp[0] * (double)i / 16;
            o += std::sin(phase * M_PI) / M_PI * amp;
            total_amp += p.amp[0] + max(0, p.damp[0]);
        }
        output[i] = (float)o;
    }
    return total_amp;
}

static bool approx_array(const float *a1, const float *a2, size_t sz, double tol)
{
    for (size_t i = 0; i < sz; i++) {
        auto diff = std::abs(a1[i] - a2[i]);
        if (!(diff < tol)) {
            return false;
        }
    }
    return true;
}

template<typename Gen>
static void test_gen_fixed(const float *expected, float *buff, int nchn,
                           const channel_param_fixed *params_fixed, double tol)
{
    memset(buff, 0, step_size * sizeof(float));
    Runner<Gen>::run_wave_fixed(buff, step_size, 1, nchn, params_fixed);
    assert(approx_array(expected, buff, step_size, tol));
}

template<typename Gen>
static void test_gen(const float *expected, float *buff, int nchn,
                     const channel_param *params, double tol)
{
    memset(buff, 0, step_size * sizeof(float));
    Runner<Gen>::run_wave(buff, step_size, 1, nchn, params);
    assert(approx_array(expected, buff, step_size, tol));
}

static void test_fixed_param(float *buff1, float *buff2,
                             int nchn, const channel_param_fixed *params_fixed)
{
    // The 0.5e-5 tolarance to max amplitude is about 6x better than what we need.
    // We only need 2^-15 ~ 3e-5.
    auto tol = calc_wave_fixed(buff1, nchn, params_fixed) * 0.5e-5;
    test_gen_fixed<ScalarGen>(buff1, buff2, nchn, params_fixed, tol);

    auto &host NACS_UNUSED = CPUInfo::get_host();
#if NACS_CPU_X86 || NACS_CPU_X86_64
    test_gen_fixed<SSE2Gen>(buff1, buff2, nchn, params_fixed, tol);
    if (host.test_feature(X86::Feature::avx)) {
        test_gen_fixed<AVXGen>(buff1, buff2, nchn, params_fixed, tol);
    }
    if (host.test_feature(X86::Feature::avx2) && host.test_feature(X86::Feature::fma)) {
        test_gen_fixed<AVX2Gen>(buff1, buff2, nchn, params_fixed, tol);
    }
    if (host.test_feature(X86::Feature::avx512f) &&
        host.test_feature(X86::Feature::avx512dq)) {
        test_gen_fixed<AVX512Gen>(buff1, buff2, nchn, params_fixed, tol);
    }
#endif
}

static void test_param(float *buff1, float *buff2, int nchn, const channel_param *params)
{
    // The 0.5e-5 tolarance to max amplitude is about 6x better than what we need.
    // We only need 2^-15 ~ 3e-5.
    auto tol = calc_wave(buff1, nchn, params) * 0.5e-5;
    test_gen<ScalarGen>(buff1, buff2, nchn, params, tol);

    auto &host NACS_UNUSED = CPUInfo::get_host();
#if NACS_CPU_X86 || NACS_CPU_X86_64
    test_gen<SSE2Gen>(buff1, buff2, nchn, params, tol);
    if (host.test_feature(X86::Feature::avx)) {
        test_gen<AVXGen>(buff1, buff2, nchn, params, tol);
    }
    if (host.test_feature(X86::Feature::avx2) && host.test_feature(X86::Feature::fma)) {
        test_gen<AVX2Gen>(buff1, buff2, nchn, params, tol);
    }
    if (host.test_feature(X86::Feature::avx512f) &&
        host.test_feature(X86::Feature::avx512dq)) {
        test_gen<AVX512Gen>(buff1, buff2, nchn, params, tol);
    }
#endif
}

static std::random_device rd;  // Will be used to obtain a seed for the random number engine
static std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()

static void test_fixed_nchn(float *buff1, float *buff2, int nchn, int rep)
{
    std::vector<channel_param_fixed> ps(nchn);
    std::uniform_real_distribution<float> pf_dis(-2, 2);
    std::uniform_real_distribution<float> a_dis(0, 2);
    for (int j = 0; j < rep; j++) {
        for (int i = 0; i < nchn; i++)
            ps[i] = {pf_dis(gen), pf_dis(gen), a_dis(gen)};
        test_fixed_param(buff1, buff2, nchn, ps.data());
    }
}

static void test_nchn(float *buff1, float *buff2, int nchn, int rep)
{
    struct param {
        float phase;
        float freq;
        float dfreq;
        float amp;
        float damp;
    };
    std::vector<param> real_ps(nchn);
    std::vector<channel_param> ps(nchn);
    for (int i = 0; i < nchn; i++)
        ps[i] = {&real_ps[i].phase, &real_ps[i].freq, &real_ps[i].dfreq,
                 &real_ps[i].amp, &real_ps[i].damp};
    std::uniform_real_distribution<float> pf_dis(-2, 2);
    std::uniform_real_distribution<float> a_dis(0, 2);
    for (int j = 0; j < rep; j++) {
        for (int i = 0; i < nchn; i++)
            real_ps[i] = {pf_dis(gen), pf_dis(gen), pf_dis(gen), a_dis(gen), a_dis(gen)};
        test_param(buff1, buff2, nchn, ps.data());
    }
}

int main()
{
    static_assert(4096 > step_size * sizeof(float), "");
    auto buff1 = (float*)mapAnonPage(4096, Prot::RW);
    auto buff2 = (float*)mapAnonPage(4096, Prot::RW);
    auto t0 = getTime();
    do {
        test_fixed_nchn(buff1, buff2, 1, 1000);
        test_fixed_nchn(buff1, buff2, 2, 500);
        test_fixed_nchn(buff1, buff2, 4, 250);
        test_fixed_nchn(buff1, buff2, 10, 100);

        test_nchn(buff1, buff2, 1, 1000);
        test_nchn(buff1, buff2, 2, 500);
        test_nchn(buff1, buff2, 4, 250);
        test_nchn(buff1, buff2, 10, 100);
    } while (getElapse(t0) < 10ull * 1000 * 1000 * 1000);
    unmapPage(buff1, 4096);
    unmapPage(buff2, 4096);
    return 0;
}
