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

#include <nacs-spcm/spcm.h>
#include <nacs-utils/log.h>
#include <nacs-utils/mem.h>
#include <nacs-utils/thread.h>
#include <nacs-utils/timer.h>

#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

#include <cstdlib>
#include <cmath>

#include <immintrin.h>
#include <sleef.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace NaCs;

constexpr long long int sample_rate = 625ll * 1000000ll;
constexpr int cycle = 1024 / 32;

static constexpr __m512 tidxs = {0.0, 0.0625, 0.125, 0.1875, 0.25, 0.3125, 0.375, 0.4375,
                                 0.5, 0.5625, 0.625, 0.6875, 0.75, 0.8125, 0.875, 0.9375};
typedef short v32si __attribute__((vector_size(64)));
static constexpr v32si mask0 = {1, 3, 5, 7, 9, 11, 13, 15,
                                17, 19, 21, 23, 25, 27, 29, 31,
                                33, 35, 37, 39, 41, 43, 45, 47,
                                49, 51, 53, 55, 57, 59, 61, 63};

__attribute__((target("avx512f,avx512bw"), flatten))
__m512 xsinpif_pi(__m512 d)
{
    __m512i q = _mm512_cvtps_epi32(d);
    d = d - _mm512_cvtepi32_ps(q);

    __m512 s = d * d;

    auto neg = _mm512_test_epi32_mask(q, _mm512_set1_epi32(1));
    d = (__m512)_mm512_mask_xor_epi32((__m512i)d, neg, (__m512i)d,
                                      _mm512_set1_epi32(0x80000000));

    auto u = 0.024749093f * s - 0.19045785f;
    u = u * s + 0.8117177f;
    u = u * s - 1.6449335f;
    return (s * d) * u + d;
}

__attribute__((target("avx512f,avx512bw"), flatten))
__m512 xsinf(__m512 d)
{
    __m512i q = _mm512_cvtps_epi32(d * (float)M_1_PI);
    d = _mm512_cvtepi32_ps(q) * ((float)-M_PI) + d;

    __m512 s = d * d;

    auto neg = _mm512_test_epi32_mask(q, _mm512_set1_epi32(1));
    d = (__m512)_mm512_mask_xor_epi32((__m512i)d, neg, (__m512i)d,
                                      _mm512_set1_epi32(0x80000000));

    auto u = 2.608316e-6f * s - 0.0001981069f;
    u = u * s + 0.008333079f;
    u = u * s - 0.1666666f;
    return s * u * d + d;
}

__attribute__((target("avx512f,avx512bw"), flatten))
__m512i calc_sins(float phase, float freq, float amp)
{
    auto v1 = xsinf(phase + tidxs * freq);
    auto v2 = xsinf(phase + (tidxs + 1) * freq);
    return _mm512_permutex2var_epi16(_mm512_cvttps_epi32(v1 * amp), (__m512i)mask0,
                                     _mm512_cvttps_epi32(v2 * amp));
}

// Amplitude normalized to 6.7465185f9 (2^31 - 1) * pi
// Phase is in unit of pi
// Frequency of 1 means one full cycle per 32 samples.
__attribute__((target("avx512f,avx512bw"), flatten))
__m512i calc_sins2(float phase, float freq, float amp)
{
    auto v1 = xsinpif_pi(phase + tidxs * freq);
    auto v2 = xsinpif_pi(phase + (tidxs + 1) * freq);
    return _mm512_permutex2var_epi16(_mm512_cvttps_epi32(v1 * amp), (__m512i)mask0,
                                     _mm512_cvttps_epi32(v2 * amp));
}

constexpr uint64_t buff_nele =  4 / 2 * 1024ll * 1024ll * 1024ll;

struct Stream : DataPipe<int16_t> {
    Stream(float amp, double freq)
        : Stream((int16_t*)mapAnonPage(4 * 1024ll * 1024ll * 1024ll, Prot::RW),
                 amp * 6.7465185e9f, uint64_t(round(freq * 10)))
    {
    }

private:
    Stream(int16_t *base, float amp, uint64_t freq_cnt)
        : DataPipe(base, buff_nele, 4096 * 512 * 32),
          m_base(base),
          m_amp(amp),
          m_freq_cnt(freq_cnt),
          worker()
    {
        worker = std::thread(&Stream::generate, this);
    }

    NACS_NOINLINE __attribute__((target("avx512f,avx512bw"), flatten)) void generate()
    {
        const uint64_t max_phase = uint64_t(625e6 * 10);
        // const double amp_scale = 2.0588742e9f / 2^31;
        const double freq_scale = 0.1 / (625e6 / 32);
        const double phase_scale = 2 / double(max_phase);

        // // const uint64_t freq_cnt = 1390000000ull / 10; // In unit of 0.1Hz
        // const uint64_t freq_cnt = uint64_t(1.953125e8);
        // // const uint64_t freq_cnt = 1562500000ull; // In unit of 0.1Hz
        int64_t phase_cnt = 0;
        while (true) {
            size_t sz;
            auto ptr = get_write_ptr(&sz);
            // We operate on 64 byte a time
            if (sz < 64 / 2) {
                CPU::pause();
                sync_writer();
                continue;
            }
            sz &= ~(size_t)(64 / 2 - 1);
            size_t write_sz = 0;
            for (; write_sz < sz; write_sz += 64 / 2) {
                auto data = calc_sins2(float(double(phase_cnt) * phase_scale),
                                       float(double(m_freq_cnt) * freq_scale), m_amp);
                _mm512_store_si512(&ptr[write_sz], data);
                phase_cnt += m_freq_cnt * (64 / 2);
                if (phase_cnt > 0) {
                    phase_cnt -= max_phase * 4;
                    while (unlikely(phase_cnt > 0)) {
                        phase_cnt -= max_phase * 4;
                    }
                }
            }
            wrote_size(write_sz);
            CPU::wake();
        }
    }

    int16_t *const m_base;
    const float m_amp;
    const uint64_t m_freq_cnt;
    std::thread worker;
};

int main()
{
    NaCs::Spcm::Spcm hdl("/dev/spcm0");
    hdl.ch_enable(CHANNEL0); // only one channel activated
    hdl.enable_out(0, true);
    hdl.set_amp(0, 2500);
    hdl.set_param(SPC_CLOCKMODE, SPC_CM_INTPLL);
    hdl.set_param(SPC_CARDMODE, SPC_REP_FIFO_SINGLE); // set the FIFO single replay mode
    hdl.write_setup();
    // starting with firmware version V9 we can program the hardware
    // buffer size to reduce the latency
    auto ver = hdl.pci_version();
    Log::log("Version: %d.%d\n", ver.first, ver.second);
    Log::log("Max rate: %f MS/s\n", double(hdl.max_sample_rate()) / 1e6);
    Log::log("1\n");

    int32_t lib_ver;
    hdl.get_param(SPC_GETDRVVERSION, &lib_ver);

    int32_t max_dac;
    hdl.get_param(SPC_MIINST_MAXADCVALUE, &max_dac);
    if ((lib_ver & 0xffff) >= 3738)
        max_dac--;
    printf("max_dac: %d\n", max_dac);

    int64_t rate = int64_t(625e6);
    hdl.set_param(SPC_CLOCKMODE, SPC_CM_INTPLL);
    // hdl.set_param(SPC_CLOCKMODE, SPC_CM_EXTREFCLOCK);
    // hdl.set_param(SPC_REFERENCECLOCK, 100 * 1000 * 1000);

    hdl.set_param(SPC_SAMPLERATE, rate);
    hdl.get_param(SPC_SAMPLERATE, &rate);
    printf("Sampling rate set to %.1lf MHz\n", (double)rate / MEGA(1));

    hdl.set_param(SPC_TRIG_ORMASK, SPC_TMASK_SOFTWARE);
    hdl.set_param(SPC_TRIG_ANDMASK, 0);
    hdl.set_param(SPC_TRIG_CH_ORMASK0, 0);
    hdl.set_param(SPC_TRIG_CH_ORMASK1, 0);
    hdl.set_param(SPC_TRIG_CH_ANDMASK0, 0);
    hdl.set_param(SPC_TRIG_CH_ANDMASK1, 0);

    // Enable output (since M4i).
    hdl.set_param(SPC_ENABLEOUT0, 1);

    hdl.set_param(SPC_AMP0, 2500); // Amp
    hdl.set_param(SPC_FILTER0, 0);

    Stream stream(0.9f, 50e6);

    size_t buff_sz;
    auto buff_ptr = stream.get_read_buff(&buff_sz);
    hdl.def_transfer(SPCM_BUF_DATA, SPCM_DIR_PCTOCARD, 4096 * 32,
                     (void*)buff_ptr, 0, 2 * buff_sz);
    // bool last_p_set = false;
    // int16_t last_p = 0;
    auto send_data = [&] {
        size_t sz;
        const int16_t *ptr;
        while (true) {
            ptr = stream.get_read_ptr(&sz);
            if (sz < 4096 / 2) {
                CPU::pause();
                stream.sync_reader();
                continue;
            }
            break;
        }
        (void)ptr;

        sz = sz & ~(size_t)2047;
        // read out the available bytes that are free again
        uint64_t count = 0;
        hdl.get_param(SPC_DATA_AVAIL_USER_LEN, &count);
        hdl.check_error();
        // printf("count=%zu\n", count);
        count = std::min(count, uint64_t(sz * 2));
        if (!count)
            return;
        if (count & 1)
            abort();
        // printf("ptr=%p, count=%zu\n", ptr, count);
        // for (size_t i = 0; i < count / 2; i++) {
        //     if (!last_p_set) {
        //         last_p_set = true;
        //         last_p = ptr[i];
        //         continue;
        //     }
        //     auto v = ptr[i];
        //     if (std::abs((int)v - last_p) > 100) {
        //         printf("%zd, %p, %p, %d, %d\n", i, ptr, stream.buffer(), last_p, v);
        //         for (size_t j = 0; j < count / 2; j++) {
        //             if (i + 100 < j || j + 100 < i)
        //                 continue;
        //             if (j == i) {
        //                 printf("[%d], ", ptr[j]);
        //             }
        //             else if (j == i - 1) {
        //                 printf("<%d>, ", ptr[j]);
        //             }
        //             else {
        //                 printf("%d, ", ptr[j]);
        //             }
        //         }
        //         printf("\n");
        //         abort();
        //     }
        //     last_p = v;
        // }
        hdl.set_param(SPC_DATA_AVAIL_CARD_LEN, count);
        hdl.check_error();
        stream.read_size(count / 2);
        CPU::wake();
    };
    send_data();
    hdl.cmd(M2CMD_DATA_STARTDMA | M2CMD_DATA_WAITDMA);
    hdl.set_param(SPC_TRIG_ORMASK, SPC_TMASK_SOFTWARE);
    hdl.cmd(M2CMD_CARD_START | M2CMD_CARD_ENABLETRIGGER);
    hdl.force_trigger();
    hdl.check_error();
    uint32_t status;
    hdl.get_param(SPC_M2STATUS, &status);
    Log::log("Status: 0x%x\n", status);
    while (true)
        send_data();
    return 0;
}
