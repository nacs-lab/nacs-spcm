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

#include <nacs-utils/processor.h>
#include <nacs-utils/timer.h>
#include <nacs-utils/mem.h>

#include <iostream>
#include <random>
#include <vector>

using namespace NaCs;
using namespace NaCs::Spcm;

static std::random_device rd;  // Will be used to obtain a seed for the random number engine
static std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()

static void fill_random(std::vector<float> &data, float lb, float ub)
{
    std::uniform_real_distribution<float> dis(lb, ub);
    for (auto &d: data) {
        d = dis(gen);
    }
}

template<typename Gen>
NACS_NOINLINE void benchmark_chn_sz(float *data, size_t sz, size_t rep, int nchn,
                                    channel_param_fixed *params_fixed,
                                    channel_param *params)
{
    Timer timer;
    Runner<Gen>::run_wave_fixed(data, sz, 1, nchn, params_fixed);

    timer.restart();
    Runner<Gen>::run_wave_fixed(data, sz, rep, nchn, params_fixed);
    auto fixed = timer.elapsed();

    timer.restart();
    Runner<Gen>::run_wave(data, sz, rep, nchn, params);
    auto change = timer.elapsed();

    std::cout << "  [nchn: " << nchn << ", rep: " << rep << "] "
              << "Fixed: " << double(fixed) / double(sz) / (double)rep / nchn << " ns; Change: "
              << double(change) / double(sz) / (double)rep / nchn << " ns" << std::endl;
}

template<typename Gen>
NACS_NOINLINE void benchmark_chn(float *data, size_t sz, size_t rep, int nchn)
{
    struct params {
        std::vector<float> phase;
        std::vector<float> freq;
        std::vector<float> dfreq;
        std::vector<float> amp;
        std::vector<float> damp;
        params()
        {}
        params(size_t nsteps)
            : phase(nsteps),
              freq(nsteps),
              dfreq(nsteps),
              amp(nsteps),
              damp(nsteps)
        {
            fill_random(phase, -2, 2);
            fill_random(freq, -2, 2);
            fill_random(dfreq, -2, 2);
            fill_random(amp, 0, 2);
            fill_random(damp, 0, 2);
        }
    };
    std::vector<params> vps(nchn);
    std::vector<channel_param_fixed> ps_fixed(nchn);
    std::vector<channel_param> ps(nchn);
    for (int i = 0; i < nchn; i++) {
        vps[i] = params(sz / step_size);
        ps_fixed[i] = {vps[i].phase.front(), vps[i].freq.front(), vps[i].amp.front()};
        ps[i] = {vps[i].phase.data(), vps[i].freq.data(), vps[i].dfreq.data(),
                 vps[i].amp.data(), vps[i].damp.data()};
    }
    benchmark_chn_sz<Gen>(data, sz, rep, nchn, ps_fixed.data(), ps.data());
}

template<typename Gen>
void benchmark(size_t sz, size_t rep)
{
    auto data = (float*)mapAnonPage(sz * sizeof(float), Prot::RW);
    benchmark_chn<Gen>(data, sz, rep, 1);
    benchmark_chn<Gen>(data, sz, rep / 2, 2);
    benchmark_chn<Gen>(data, sz, rep / 4, 4);
    benchmark_chn<Gen>(data, sz, rep / 10, 10);
    unmapPage(data, sz * sizeof(float));
}

int main()
{
    std::cout << "Scalar:" << std::endl;
    benchmark<ScalarGen>(2 * 4096, 4096 * 2);

    auto &host NACS_UNUSED = CPUInfo::get_host();
#if NACS_CPU_X86 || NACS_CPU_X86_64
    std::cout << "SSE2:" << std::endl;
    benchmark<SSE2Gen>(2 * 4096, 4096 * 4);
    if (host.test_feature(X86::Feature::avx)) {
        std::cout << "AVX:" << std::endl;
        benchmark<AVXGen>(2 * 4096, 4096 * 4);
    }
    if (host.test_feature(X86::Feature::avx2) && host.test_feature(X86::Feature::fma)) {
        std::cout << "AVX2:" << std::endl;
        benchmark<AVX2Gen>(2 * 4096, 4096 * 8);
    }
    if (host.test_feature(X86::Feature::avx512f) &&
        host.test_feature(X86::Feature::avx512dq)) {
        std::cout << "AVX512:" << std::endl;
        benchmark<AVX512Gen>(2 * 4096, 4096 * 16);
    }
#endif

    return 0;
}
