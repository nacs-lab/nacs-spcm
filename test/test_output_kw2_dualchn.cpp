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
#include <numeric>
using namespace NaCs;

typedef int16_t m512i16 __attribute__((vector_size (64)));

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

static constexpr m512i16 interweave_idx1 = {0, 32, 1, 33, 2, 34, 3, 35, 4, 36, 5, 37,
                                 6, 38, 7, 39, 8, 40, 9, 41, 10, 42, 11, 43,
                                 12, 44, 13, 45, 14, 46, 15, 47};
static constexpr m512i16 interweave_idx2 = {16, 48, 17, 49, 18, 50, 19, 51, 20, 52,
                                 21, 53, 22, 54, 23, 55, 24, 56, 25, 57,
                                 26, 58, 27, 59, 28, 60, 29, 61, 30, 62,
                                 31,63}; // idx1 and idx2 used for interweaving in multi channel output



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

// Amplitude normalized to 6.7465185f9 (2^31 - 1) * pi
// Phase is in unit of pi
// Frequency of 1 means one full cycle per 32 samples.

__attribute__((target("avx512f,avx512bw"), flatten))
__m512i calc_sins2(int64_t* phase_cnt, uint64_t* freq_cnt, float* amp, size_t nchn)
{
    const uint64_t max_phase = uint64_t(625e6 * 10);
    const double freq_scale = 0.1 / (625e6 / 32);
    const double phase_scale = 2 / double(max_phase);
    __m512 v1 = _mm512_set1_ps(0.0f);
    __m512 v2 = _mm512_set1_ps(0.0f);
    for(int i = 0; i < nchn; ++i)
    {
        float phase = float(double(phase_cnt[i] * phase_scale));
        float freq = float(double(freq_cnt[i] * freq_scale));
        v1 += xsinpif_pi(phase + tidxs * freq) * amp[i];
        v2 += xsinpif_pi(phase + (tidxs + 1) * freq) * amp[i];
        phase_cnt[i] += freq_cnt[i] * (64 / 2);
        if (phase_cnt[i] > 0) {
            phase_cnt[i] -= max_phase * 4;
            while (unlikely(phase_cnt[i] > 0)) {
                phase_cnt[i] -= max_phase * 4;
            }
        }
    }
    return _mm512_permutex2var_epi16(_mm512_cvttps_epi32(v1), (__m512i)mask0,
                                     _mm512_cvttps_epi32(v2));
}

constexpr uint64_t buff_nele =  4 / 2 * 1024ll * 1024ll * 1024ll;

struct MultiStream : DataPipe<int16_t> {
    MultiStream(float* amp, double* freq, float* phase, size_t nchn)
        : MultiStream((int16_t*)mapAnonPage(4 * 1024ll * 1024ll * 1024ll, Prot::RW),
                      amp, freq, phase, nchn)
    {
    }

private:
    MultiStream(int16_t *base, float* amp, double* freq, float* phase, size_t nchn)
        : DataPipe(base, buff_nele, 4096 * 512 * 32),
          m_base(base),
          m_amp(amp),
          m_freq_cnt(nchn, 0),
          m_phase_cnt(nchn, 0),
          nchn(nchn),
          worker()
    {
        for (int i = 0; i < nchn; ++i){
            m_phase_cnt[i] = int64_t(round(phase[i]*2147483647*2.9)); // no idea how these numbers come about, but it works...
            m_freq_cnt[i] = uint64_t(round(freq[i] * 10));
            m_amp[i] *= 6.7465185e9f;
        }
        worker = std::thread(&MultiStream::generate, this);
    }

    NACS_NOINLINE __attribute__((target("avx512f,avx512bw"), flatten)) void generate()
    {
        // const uint64_t max_phase = uint64_t(625e6 * 10);
        // const double amp_scale = 2.0588742e9f / 2^31;
        // const double freq_scale = 0.1 / (625e6 / 32);
        // const double phase_scale = 2 / double(max_phase);

        // // const uint64_t freq_cnt = 1390000000ull / 10; // In unit of 0.1Hz
        // const uint64_t freq_cnt = uint64_t(1.953125e8);
        // // const uint64_t freq_cnt = 1562500000ull; // In unit of 0.1Hz
        //std::vector<int64_t> phase_cnt(nchn,0);
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
                auto data = calc_sins2(m_phase_cnt.data(), m_freq_cnt.data(), m_amp, nchn);
                _mm512_store_si512(&ptr[write_sz], data);
            }
            wrote_size(write_sz);
            CPU::wake();
        }
    }

    int16_t *const m_base;
    float *m_amp;
    std::vector<uint64_t> m_freq_cnt;
    std::vector<int64_t> m_phase_cnt;
    //double *m_freq;
    size_t nchn;
    //std::vector<float> m_amp;
    std::thread worker;
};

__attribute__((target("avx512f,avx512bw"), flatten))
void write_to_buffer_int(const int16_t **msptrs, size_t nthreads, int16_t* write_buf,
                     size_t* curr_pos, uint64_t bytes_to_write) {
    size_t write_pos = 0;
    for (; write_pos < (bytes_to_write / 2); write_pos += 64/2){ //bytes_to_write/2 is number of int16_t pointer positions to advance
            // check for overflow
        *curr_pos += 64 / 2; // move 64 bytes or one __m512i object that has been stored.
        if (*curr_pos >= buff_nele){
            *curr_pos = 0;
        }
        int16_t* curr_ptr = write_buf + *curr_pos;
        __m512i data = _mm512_set1_epi16(0);
        for (int i = 0; i < nthreads; ++i){
            data = _mm512_add_epi16(data, *(__m512i*)msptrs[i]);
            msptrs[i] += 32;
        }
        _mm512_stream_si512((__m512i*)curr_ptr, data);
    }
}

__attribute__((target("avx512f,avx512bw"), flatten))
void write_to_buffer_int_multi_chn(const int16_t **msptrs1, size_t nthreads1,
                                   const int16_t **msptrs2, size_t nthreads2,
                                   int16_t* write_buf, size_t* curr_pos,
                                   uint64_t bytes_to_write) {
    size_t write_pos = 0;
    for (; write_pos < (bytes_to_write / 2); write_pos += 64) { 
        *curr_pos += 64; // store two __m512i at a time, which is 64 samples
        if (*curr_pos >= buff_nele) {
            *curr_pos = 0;
        }
        int16_t* curr_ptr1 = write_buf + *curr_pos;
        int16_t* curr_ptr2 = write_buf + *curr_pos + 32;
        __m512i data1 = _mm512_set1_epi16(0);
        for (int i = 0; i < nthreads1; ++i) {
            data1 = _mm512_add_epi16(data1, *(__m512i*)msptrs1[i]);
            msptrs1[i] += 32;
        }
        __m512i data2 = _mm512_set1_epi16(0);
        for (int i = 0; i < nthreads2; ++i) {
            data2 = _mm512_add_epi16(data2, *(__m512i*)msptrs2[i]);
            msptrs2[i] += 32;
        }
        // refer to stackoverflow.com/questions/63822215/interleaved-merging-of-2-avx-512-vector-elements-c-intrinsic
        // Also, can use documentation on _mm512_mask_permutex2var_epi16
        // goal is to merge the two __m512i in an interleaved way
        //__m512i out1 = _mm512_set1_epi16(0);
        //__m512i out2 = _mm512_set1_epi16(0);
        __m512i out1, out2;
        out1 = _mm512_mask_permutex2var_epi16(data1, 0xFFFFFFFF, (__m512i) interweave_idx1, data2);
        out2 = _mm512_mask_permutex2var_epi16(data1, 0xFFFFFFFF, (__m512i) interweave_idx2, data2);
        _mm512_stream_si512((__m512i*)curr_ptr1, out1);
        _mm512_stream_si512((__m512i*)curr_ptr2, out2);
    }

}


int main()
{
    NaCs::Spcm::Spcm hdl("/dev/spcm0");
    hdl.ch_enable(CHANNEL0|CHANNEL1); // only one channel activated
    hdl.enable_out(0, true);
    hdl.enable_out(1, true);
    hdl.set_amp(0, 2500);
    hdl.set_amp(1, 2500);
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

    hdl.set_param(SPC_ENABLEOUT1, 1);
    hdl.set_param(SPC_AMP1, 2500); // Amp
    hdl.set_param(SPC_FILTER1, 0);
    
    /*
    float amp0 = 0.1f;
    std::vector<float> amps = {amp0+0.013+0.003,amp0+0.006-0.0005,amp0-0.013+0.002,amp0-0.012+0.001,amp0-0.006,amp0-0.012-0.002,amp0-0.0045,amp0+0.002,amp0-0.00,amp0-0.013+0.0055};
    std::vector<double> freqs = {95e6,102e6,109e6,116e6,123e6,130e6,137e6,144e6,151e6,158e6};
    std::vector<float> phases = {1.1030484,0.57133858,0.15728503,0.881126,0.74086594,0.81601378,0.48109314,0.23145855,0.37910408,0.66274212};
    */
    
    std::vector<float> amps = {0.1f, 0.05f, 0.02f, 0.04f, 0.02f, 0.01f, 0.02f, 0.01f, 0.05f, 0.03f, 0.01f, 0.01f, 0.01f, 0.005f, 0.01f, 0.01f};
    std::vector<double> freqs = {123e6, 127e6, 121e6, 125e6, 120e6, 117e6, 115e6, 90e6, 150e6, 70e6, 80e6, 200e6, 220e6, 170e6, 110e6, 105e6};
    std::vector<float> phases = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    std::vector<float> amps2 = {0.1f, 0.05f, 0.02f, 0.04f, 0.02f, 0.01f, 0.02f, 0.01f, 0.07f, 0.01f, 0.005f, 0.02f, 0.005f, 0.005f, 0.002f, 0.01f};
    std::vector<double> freqs2 = {120e6, 115e6, 110e6, 116e6, 112e6, 118e6, 124e6, 140e6, 111e6, 40e6, 56e6, 75e6, 30e6, 41e6, 77e6, 66e6};
    std::vector<float> phases2 = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    
    /*
    float amps_sum = std::accumulate(amps.begin(), amps.end(), 0.0f);
    float amp_max = 0.9999f;
    if (amps_sum > amp_max)//(amps_sum > 0)//
        std::transform(amps.begin(), amps.end(), amps.begin(),
                       [amps_sum,amp_max](float f){return f/(amps_sum)*amp_max;});
    */
    
    std::vector<MultiStream*> Streams;
    //int nchn = amps.size();
    int nchn = 12;
    int n_per_thread = 4;
    for (int i = 0; i < nchn; i += n_per_thread){
        int this_n;
        if ((i + n_per_thread) > nchn) {
            this_n = ((int) nchn) - i;
        } else {
            this_n = n_per_thread;
        }
        MultiStream *fsptr;
        fsptr = new MultiStream(amps.data() + i, freqs.data() + i, phases.data() + i, (size_t) this_n);
        Streams.push_back(fsptr);
    }

    size_t nthreads = Streams.size();

    std::vector<MultiStream*> Streams2;
    //int nchn2 = amps2.size();
    int nchn2 = 12;
    int n_per_thread2 = 4;
    for (int i = 0; i < nchn2; i += n_per_thread2){
        int this_n;
        if ((i + n_per_thread2) > nchn2) {
            this_n = ((int) nchn2) - i;
        } else {
            this_n = n_per_thread2;
        }
        MultiStream *fsptr;
        fsptr = new MultiStream(amps2.data() + i, freqs2.data() + i, phases2.data() + i, (size_t) this_n);
        Streams2.push_back(fsptr);
    }

    size_t nthreads2 = Streams.size();

    
    // set up transfer buffer
    int16_t* buff_ptr = (int16_t*)mapAnonPage(4 * 1024ll * 1024ll * 1024ll, Prot::RW);
    size_t curr_pos = 0;
    size_t buff_sz = buff_nele;
    hdl.def_transfer(SPCM_BUF_DATA, SPCM_DIR_PCTOCARD, 4096 * 32,
                     (void*)buff_ptr, 0, 2 * buff_sz); //buff_nele is the size of the buffer of int16_t types. Mult by 2 to get number of bytes

// bool last_p_set = false;
    // int16_t last_p = 0;
    std::vector<const int16_t*> fsptrs;
    fsptrs.reserve(nthreads);
    std::vector<const int16_t*> fsptrs2;
    fsptrs2.reserve(nthreads2);
    auto send_data = [&] {
        size_t min_sz = buff_nele * 4; // cannot possibly be this big
        size_t this_sz;
        int j = 0;
        while (j < nthreads){ // wait for MultiFloatStreams
            // ptr = stream.get_read_ptr(&sz);
            //if (sz < 4096 / 2) {
            //    CPU::pause();
            //    stream.sync_reader();
            //    continue;
            //}
            //break;
            const int16_t* this_fsptr;
            this_fsptr = (*Streams[j]).get_read_ptr(&this_sz);
            if (this_sz >= 4096 / 2 / 2){ // wait for half as many ready values 
                if (min_sz > this_sz){
                    min_sz = this_sz;
                }
                fsptrs.push_back(this_fsptr);
                j += 1;
            } else {
                CPU::pause();
                (*Streams[j]).sync_reader();
            }
        }
        j = 0;
        while (j < nthreads2) {
            const int16_t* this_fsptr;
            this_fsptr = (*Streams2[j]).get_read_ptr(&this_sz);
            if (this_sz >= 4096 / 2 / 2){
                if (min_sz > this_sz) {
                    min_sz = this_sz;
                }
                fsptrs2.push_back(this_fsptr);
                j += 1;
            } else {
                CPU::pause();
                (*Streams2[j]).sync_reader();
            }
        }
        //Log::log("%d ", sz);
        min_sz = min_sz & ~(size_t)1023; // 1024 is half the number of bytes to read
        // read out the available bytes that are free again
        uint64_t count = 0;
        hdl.get_param(SPC_DATA_AVAIL_USER_LEN, &count);
        hdl.check_error();
        // printf("count=%zu\n", count);
        uint64_t avail = count;
        count = std::min(count, uint64_t(min_sz * 2) * 2); // min_sz * 2 to actually get number of bytes cause of int16_t pointers, multiply by another 2 since each entry needs 2 outputs, cause of the interweaving
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
        //}
        //now prepare data for card.
        //size_t write_pos = 0;
        //for (; write_pos < (count / 2); write_pos += 64/2){ //count/2 is number of int16_t pointer positions to advance
            // check for overflow
        // curr_pos += 64 / 2; // move 64 bytes or one __m512i object that has been stored.
        // if (curr_pos > buff_nele){
        //      curr_pos = 0;
        //  }
        //  int16_t* curr_ptr = buff_ptr + curr_pos;
        //  __m512 v1 = _mm512_set1_ps(0.0f);
        //  __m512 v2 = _mm512_set1_ps(0.0f);
        //  for (int i = 0; i < nthreads; ++i){
        //      __m512 this_v1 = *(__m512*)fsptrs[i];
        //      __m512 this_v2 = *((__m512*)fsptrs[i] + 1);
        //      v1 += this_v1;
        //      v2 += this_v2;
        //      fsptrs[i] += 64;
        //  }
        //  __m512i data = _mm512_permutex2var_epi16(_mm512_cvttps_epi32(v1), (__m512i)mask0,
        //                           _mm512_cvttps_epi32(v2));
        //  _mm512_store_si512(curr_ptr, data);
        //}
        write_to_buffer_int_multi_chn(fsptrs.data(), nthreads, fsptrs2.data(), nthreads2,
                                     buff_ptr, &curr_pos, count);
        // tell card data is ready
//        Log::log("avail: %lu ", avail);
//        Log::log("count: %lu ", count);
        hdl.set_param(SPC_DATA_AVAIL_CARD_LEN, count);
        try{
            hdl.check_error();
        }
        catch (std::exception& test)
        {
            //          Log::log("avail: %lu ", avail);
            //  Log::log("count: %lu ", count);
            //Log::log(test.what());
            throw test;
        }
        // tell datagen that data has been read
        for (int i = 0; i < nthreads; ++i){
            (*Streams[i]).read_size(count / 2 / 2); // count is number of bytes. With only one channel, we have to advance count /2 . With two channels, we advance count / 4
        }
        for (int i = 0; i < nthreads; ++i) {
            (*Streams2[i]).read_size(count / 2 / 2);
        }
        fsptrs.clear();
        fsptrs2.clear();
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