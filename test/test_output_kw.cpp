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

#include <exception>
#include <stdexcept>
#include <iostream>
#include <mutex>

using namespace NaCs;

static std::exception_ptr teptr = nullptr;

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
    // __m512 v1 = _mm512_set1_ps(0.0f);
    // __m512 v2 = _mm512_set1_ps(0.0f);
    auto v1 = xsinpif_pi(phase + tidxs * freq);
    auto v2 = xsinpif_pi(phase + (tidxs + 1) * freq);
    return _mm512_permutex2var_epi16(_mm512_cvttps_epi32(v1 * amp), (__m512i)mask0,
                                     _mm512_cvttps_epi32(v2 * amp));
}

__attribute__((target("avx512f,avx512bw"), flatten))
__m512i calc_sins2(std::vector<float> phase, std::vector<float> freq, std::vector<float> amp)
{
    __m512 v1 = _mm512_set1_ps(0.0f);
    __m512 v2 = _mm512_set1_ps(0.0f);
    size_t size = phase.size(); //should be the same as freq.size() and amp.size()
    for(int i = 0; i < size; ++i)
    {
        v1 += xsinpif_pi(phase[i] + tidxs * freq[i]) * amp[i];
        v2 += xsinpif_pi(phase[i] + (tidxs + 1) * freq[i]) * amp[i];
    }
    return _mm512_permutex2var_epi16(_mm512_cvttps_epi32(v1), (__m512i)mask0,
                                     _mm512_cvttps_epi32(v2));
}

__attribute__((target("avx512f,avx512bw"), flatten))
void calc_sins2(float phase, float freq, float amp, __m512 *v1, __m512 *v2)
{
    // __m512 v1 = _mm512_set1_ps(0.0f);
    // __m512 v2 = _mm512_set1_ps(0.0f);
    *v1 = xsinpif_pi(phase + tidxs * freq) * amp;
    *v2 = xsinpif_pi(phase + (tidxs + 1) * freq) * amp;
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
            // Log::log("1 ");
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
struct FloatStream {
    FloatStream(float amp, double freq)
        : FloatStream((int16_t*)mapAnonPage(4 * 1024ll * 1024ll * 1024ll, Prot::RW),
                      (int16_t*)mapAnonPage(4 * 1024ll * 1024ll * 1024ll, Prot::RW),
                      amp * 6.7465185e9f, uint64_t(round(freq * 10)))
    {
    }
    inline const int16_t *get_read_ptr1(size_t *sz1){
        return dp1.get_read_ptr(sz1);
    }
    inline const int16_t *get_read_ptr2(size_t *sz2){
        return dp2.get_read_ptr(sz2);
    }
    inline void read_size(size_t sz1, size_t sz2){
        dp1.read_size(sz1);
        dp2.read_size(sz2);
    }
    inline void sync_reader1(){
        dp1.sync_reader();
    }
    inline void sync_reader2(){
        dp2.sync_reader();
    }
    inline SpinLock *getLock(){
        return &spinlock;
    }
private:
    FloatStream(int16_t *base, int16_t *base2, float amp, uint64_t freq_cnt)
        : m_base(base),
          m_base2(base2),
          m_amp(amp),
          m_freq_cnt(freq_cnt),
          dp1(base, buff_nele, 4096 * 512 * 32),
          dp2(base2, buff_nele, 4096 * 512 * 32),
          spinlock(),
          worker()
    {
        //dp1 = DataPipe<int16_t>::DataPipe(base, buff_nele, 4096 * 512 * 32);
        //dp2 = DataPipe<int16_t>::DataPipe(base2, buff_nele, 4096 * 512 * 32);
        worker = std::thread(&FloatStream::generate, this);
    }

    NACS_NOINLINE __attribute__((target("avx512f,avx512bw"), flatten)) void generate()
    {
        //try{
        const uint64_t max_phase = uint64_t(625e6 * 10);
        // const double amp_scale = 2.0588742e9f / 2^31;
        const double freq_scale = 0.1 / (625e6 / 32);
        const double phase_scale = 2 / double(max_phase);

        // // const uint64_t freq_cnt = 1390000000ull / 10; // In unit of 0.1Hz
        // const uint64_t freq_cnt = uint64_t(1.953125e8);
        // // const uint64_t freq_cnt = 1562500000ull; // In unit of 0.1Hz
        int64_t phase_cnt = 0;
        while (true) {
            //Log::log("1");
            size_t sz, sz2, min_sz;
            spinlock.lock();
            auto ptr1 = dp1.get_write_ptr(&sz);
            auto ptr2 = dp2.get_write_ptr(&sz2);
            spinlock.unlock();
            //Log::log("%d ", sz2);
            // We operate on 64 byte a time
            if ((sz < 64 / 2) || (sz2 < 64 / 2)) {
                CPU::pause();
                {
                    std::lock_guard<SpinLock> guard(spinlock);
                    dp1.sync_writer();
                    dp2.sync_writer();
                }
                continue;
            }
            //Log::log("%d ", sz);
            sz &= ~(size_t)(64 / 2 - 1);
            sz2 &= ~(size_t)(64 / 2 - 1);
            min_sz = std::min(sz, sz2);
            size_t write_sz = 0;
            for (; write_sz < min_sz; write_sz += 64 / 2) {
                __m512 v1, v2;
                calc_sins2(float(double(phase_cnt) * phase_scale),
                           float(double(m_freq_cnt) * freq_scale), m_amp, &v1, &v2);
                {
                    std::lock_guard<SpinLock> guard(spinlock);
                    _mm512_store_ps(&ptr1[write_sz], v1);
                    _mm512_store_ps(&ptr2[write_sz], v2);
                }
                phase_cnt += m_freq_cnt * (64 / 2);
                if (phase_cnt > 0) {
                    phase_cnt -= max_phase * 4;
                    while (unlikely(phase_cnt > 0)) {
                        phase_cnt -= max_phase * 4;
                    }
                }
            }
            {
                std::lock_guard<SpinLock> guard(spinlock);
                dp1.wrote_size(write_sz);
                dp2.wrote_size(write_sz);
            }
            CPU::wake();
        }
        //}
        //catch(...){
        //Log::log("here");
        //  teptr = std::current_exception();
        //}
    }
    int16_t *const m_base;
    int16_t *const m_base2;
    const float m_amp;
    const uint64_t m_freq_cnt;
    DataPipe<int16_t> dp1;
    DataPipe<int16_t> dp2;
    SpinLock spinlock;
    std::thread worker;
};
struct MultiThreadStream : DataPipe<int16_t> {
    MultiThreadStream(std::vector<float> amp, std::vector<double> freq)
        : MultiThreadStream((int16_t*)mapAnonPage(4 * 1024ll * 1024ll * 1024ll, Prot::RW),
                      amp, freq)
    {
    }
    ~MultiThreadStream(){
        std::vector<FloatStream*>().swap(Streams); // free up memory
    }
    inline SpinLock *getLock(){
        return &spinlock;
    }

private:
    MultiThreadStream(int16_t *base, std::vector<float> amp, std::vector<double> freq)
        : DataPipe(base, buff_nele, 4096 * 512 * 32),
          m_base(base),
          spinlock(),
          worker()
    {
        for(int i = 0; i < amp.size(); ++i){
            FloatStream *fsptr;
            fsptr = new FloatStream(amp[i],freq[i]);
            Streams.push_back(fsptr);
        }
        worker = std::thread(&MultiThreadStream::generate, this);
    }

    NACS_NOINLINE __attribute__((target("avx512f,avx512bw"), flatten)) void generate()
    {
        //try{
        while (true){
            //Log::log("1 ");
            size_t sz;
            size_t stream_sz = Streams.size();
            spinlock.lock();
            auto ptr = get_write_ptr(&sz);
            spinlock.unlock();
            // We operate on 64 byte a time
            if (sz < 64 / 2) {
                CPU::pause();
                {
                    std::lock_guard<SpinLock> guard(spinlock);
                    sync_writer();
                }
                continue;
            }
            // Log::log("%d ",sz);
            sz &= ~(size_t)(64 / 2 - 1); // now, we make sure the data gen is ready
            int j = 0;
            size_t float_stream_sz;
            size_t float_stream_sz1;
            size_t float_stream_sz2;
            std::vector<const int16_t*> ptrs1;
            std::vector<const int16_t*> ptrs2;
            while (j < stream_sz){
                const int16_t* this_ptr1;
                const int16_t* this_ptr2;
                SpinLock* f_spinlock = (*Streams[j]).getLock();
                {
                    std::lock_guard<SpinLock> guard(*f_spinlock);
                    this_ptr1 = (*Streams[j]).get_read_ptr1(&float_stream_sz1);
                    this_ptr2 = (*Streams[j]).get_read_ptr2(&float_stream_sz2);
                }
                float_stream_sz = std::min(float_stream_sz1, float_stream_sz2);
                if (!(float_stream_sz < sz)){
                    {
                        std::lock_guard<SpinLock> guard(*f_spinlock);
                        ptrs1.push_back(this_ptr1);
                        ptrs2.push_back(this_ptr2);
                    }
                    j += 1;
                    //Log::log("%d",j);
                } else {
                    CPU::pause();
                    for(int k = 0; k < stream_sz; ++k){
                        SpinLock* f_spinlock2 = (*Streams[k]).getLock();
                        std::lock_guard<SpinLock> guard(*f_spinlock2);
                        (*Streams[k]).sync_reader1();
                        (*Streams[k]).sync_reader2();
                    }
                }
            }
            size_t write_sz = 0;
            for (; write_sz < sz; write_sz += 64 / 2) {
                __m512 v1 = _mm512_set1_ps(0.0f);
                __m512 v2 = _mm512_set1_ps(0.0f);
                for(int i = 0; i < stream_sz; ++i)
                {
                    __m512 this_v1;
                    __m512 this_v2;
                    {
                        SpinLock* f_spinlock = (*Streams[i]).getLock();
                        std::lock_guard<SpinLock> guard(*f_spinlock);
                        this_v1 = *(__m512*)ptrs1[i];
                        this_v2 = *(__m512*)ptrs2[i];
                    }
                    v1 += this_v1;
                    v2 += this_v2;
                    ptrs1[i] += 64 / 2;
                    ptrs2[i] += 64 / 2;
                }
                __m512i data = _mm512_permutex2var_epi16(_mm512_cvttps_epi32(v1), (__m512i)mask0,
                                     _mm512_cvttps_epi32(v2));
                {
                    std::lock_guard<SpinLock> guard(spinlock);
                    _mm512_store_si512(&ptr[write_sz], data);
                }
                //Log::log("1");
            }
            {
                std::lock_guard<SpinLock> guard(spinlock);
                wrote_size(write_sz);
            }
            for(int i = 0; i < stream_sz; ++i){
                SpinLock* f_spinlock = (*Streams[i]).getLock();
                std::lock_guard<SpinLock> guard(*f_spinlock);
                (*Streams[i]).read_size(write_sz, write_sz);
                //Log::log("%d %d %d ", y, write_sz, x);
            }
            CPU::wake();
        }
        //}
        //catch(...)
        //{
        //    teptr = std::current_exception();
        //}
    }
    int16_t *const m_base;
    std::vector<FloatStream*> Streams;
    std::vector<float> m_amp;
    std::vector<uint64_t> m_freq_cnt;
    SpinLock spinlock;
    std::thread worker;
};
struct MultiStream : DataPipe<int16_t> {
    MultiStream(std::vector<float> amp, std::vector<double> freq)
        : MultiStream((int16_t*)mapAnonPage(4 * 1024ll * 1024ll * 1024ll, Prot::RW),
                      amp, freq)
    {
    }

private:
    MultiStream(int16_t *base, std::vector<float> amp, std::vector<double> freq)
        : DataPipe(base, buff_nele, 4096 * 512 * 32),
          m_base(base),
          worker()
    {
        std::transform(amp.begin(), amp.end(), amp.begin(), [](float f){return f * 6.7465185e9f;});
        std::vector<uint64_t> freq_cnt(freq.size(),0);
        std::transform(freq.begin(),freq.end(), freq_cnt.begin(), [](double u){return uint64_t(round(u * 10));});
        m_amp = amp;
        m_freq_cnt = freq_cnt;
        worker = std::thread(&MultiStream::generate, this);
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
        std::vector<int64_t> phase_cnt(m_amp.size(),0);
        std::vector<float> m_freq_cntf(m_freq_cnt.size(),0);
        std::transform(m_freq_cnt.begin(), m_freq_cnt.end(), m_freq_cntf.begin(),[freq_scale](uint64_t u){return float(double(u) * freq_scale);});
        std::vector<float> phase_cntf(m_amp.size(),0);
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
                std::transform(phase_cnt.begin(), phase_cnt.end(), phase_cntf.begin(), [phase_scale](int64_t phase){return float(double(phase) * phase_scale);});
                auto data = calc_sins2(phase_cntf, m_freq_cntf, m_amp);
                _mm512_store_si512(&ptr[write_sz], data);
                for(int i = 0; i < m_freq_cnt.size(); ++i){
                    phase_cnt[i] += m_freq_cnt[i] * (64 / 2);
                    if (phase_cnt[i] > 0) {
                        phase_cnt[i] -= max_phase * 4;
                        while (unlikely(phase_cnt[i] > 0)) {
                            phase_cnt[i] -= max_phase * 4;
                        }
                    }
                }
            }
            wrote_size(write_sz);
            CPU::wake();
        }
    }

    int16_t *const m_base;
    std::vector<float> m_amp;
    std::vector<uint64_t> m_freq_cnt;
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

    //Stream stream(0.5f, 500e3);
    std::vector<float> amps = {0.3f}; //,0.03f}; //,0.1f,0.02f};
    std::vector<double> freqs = {500e3}; //, 500.001e3}; //,499.995e3,500.002e3};
    MultiThreadStream stream(amps, freqs);
    SpinLock *spinlock = stream.getLock();

    size_t buff_sz;
    (*spinlock).lock();
    auto buff_ptr = stream.get_read_buff(&buff_sz);
    (*spinlock).unlock();
    hdl.def_transfer(SPCM_BUF_DATA, SPCM_DIR_PCTOCARD, 4096 * 32,
                     (void*)buff_ptr, 0, 2 * buff_sz);
    // bool last_p_set = false;
    // int16_t last_p = 0;
    auto send_data = [&] {
        size_t sz;
        const int16_t *ptr;
        while (true){
            //if (teptr) {
            //try{
            //    std::rethrow_exception(teptr);
            //}
            //catch(const std::exception &ex)
            //{
            //    std::cerr << "Thread exited with exception: " << ex.what() << "\n";
            //}}
            {
                std::lock_guard<SpinLock> guard(*spinlock);
                ptr = stream.get_read_ptr(&sz);
            }
            if (sz < 4096 / 2) {
                CPU::pause();
                {
                    std::lock_guard<SpinLock> guard(*spinlock);
                    stream.sync_reader();
                }
                continue;
            }
            break;
        }
        (void)ptr;
        //Log::log("%d ", sz);
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
        {
            std::lock_guard<SpinLock> guard(*spinlock);
            stream.read_size(count / 2);
        }
        CPU::wake();
    };
    send_data();
    Log::log("Done with Initial Data send \n");
    hdl.cmd(M2CMD_DATA_STARTDMA | M2CMD_DATA_WAITDMA);
    hdl.set_param(SPC_TRIG_ORMASK, SPC_TMASK_SOFTWARE);
    hdl.cmd(M2CMD_CARD_START | M2CMD_CARD_ENABLETRIGGER);
    hdl.force_trigger();
    hdl.check_error();
    uint32_t status;
    hdl.get_param(SPC_M2STATUS, &status);
    Log::log("Status: 0x%x\n", status);
    while (true){
        send_data();
    }
    return 0;
}
