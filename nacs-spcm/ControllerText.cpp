//

#include "ControllerText.h"
#include "Config.h"

#include <iostream>
#include <nacs-utils/log.h>
#include <chrono>
#include <thread>

using namespace NaCs;

using namespace std::chrono_literals;

namespace Spcm {
// TIME TRACKING STUFF

typedef int16_t m512i16 __attribute__((vector_size (64)));

static constexpr m512i16 interweave_idx1 = {0, 32, 1, 33, 2, 34, 3, 35, 4, 36, 5, 37,
                                 6, 38, 7, 39, 8, 40, 9, 41, 10, 42, 11, 43,
                                 12, 44, 13, 45, 14, 46, 15, 47};
static constexpr m512i16 interweave_idx2 = {16, 48, 17, 49, 18, 50, 19, 51, 20, 52,
                                 21, 53, 22, 54, 23, 55, 24, 56, 25, 57,
                                 26, 58, 27, 59, 28, 60, 29, 61, 30, 62,
                                 31,63}; // idx1 and idx2 used for interweaving in multi channel output


// must be called by the worker thread with the lock hold.
NACS_EXPORT() void ControllerText::loadSeq(uint32_t idx, Cmd *p, size_t sz){
    uint32_t nwrote;
    do {
        nwrote = copy_cmds(idx, p, sz);
        p += nwrote;
        sz -= nwrote;
    } while (sz > 0);
    flush_cmd(idx);
    //std::cout << "now distributing" << std::endl;
    distribute_cmds(idx);
    //std::cout << "after command distribution" << std::endl;
}

__attribute__((target("avx512f,avx512bw"), flatten))
NACS_EXPORT() YAML::Node ControllerText::testCompute(size_t nele, size_t buff_sz_nele)
{
    uint64_t m_output_cnt = 0;
    size_t notif_size = 4096;
    // set up buffer to write into
    int16_t* buff_ptr = (int16_t*)mapAnonPage(buff_sz_nele * 2, Prot::RW);
    size_t buff_pos = 0;
    uint64_t initial_clock = cycleclock();
    uint64_t count;
    YAML::Node res;
    for (int i = 0; i < n_phys_chn; ++i) {
        (*m_stm_mngrs[m_out_chns[i]]).start_streams();
        (*m_stm_mngrs[m_out_chns[i]]).start_worker();
    }
    while (m_output_cnt < nele) {
        std::vector<const int16_t*> ptrs(n_phys_chn, nullptr);
        size_t sz;
        size_t min_sz = 8 * 1024ll * 1024ll * 1024ll; // cannot be this large.
        for (int i = 0; i < n_phys_chn; ++i)
        {
        retry:
            ptrs[i] = (*m_stm_mngrs[m_out_chns[i]]).get_output(sz);
            //std::cout << "sz: " << sz << std::endl;
            if (sz < notif_size / 2 / n_phys_chn) { // 4096
                // data not ready
                //if (sz > 0)
                //     (*m_stm_mngrs[m_out_chns[i]]).sync_reader();
                CPU::pause();
                //toCont = true;
                goto retry;
            }
            if (sz < min_sz) {
                min_sz = sz;
            }
        }
        min_sz = min_sz & ~(uint64_t)(notif_size / 2 / n_phys_chn - 1); // make it chunks of 2048
        count = std::min(min_sz * 2 * n_phys_chn, (nele - m_output_cnt) * 32 * n_phys_chn * 2);
//read out available number of bytes
        if (!count) {
            continue;
        }
        else {
        }
        if (count & 1) {
            printf("Count: %lu\n", count);
            abort();
        }
        int16_t* curr_ptr;
        int16_t* curr_ptr2;
        if (n_phys_chn == 1) {
            for(int i = 0; i < (count / 2); i += 64/2) {
                curr_ptr = buff_ptr + buff_pos;
                _mm512_stream_si512((__m512i*)curr_ptr, *(__m512i*) ptrs[0]);
                buff_pos += 64/2;
                ptrs[0] += 64/2;
                if (buff_pos >= buff_sz_nele) {
                    buff_pos = 0;
                }
            }
        }
        else if (n_phys_chn == 2) {
            for (int i = 0; i < (count / 2); i+= 64) {
                curr_ptr = buff_ptr + buff_pos;
                curr_ptr2 = curr_ptr + 32;
                __m512i out1, out2, data1, data2;
                data1 = *(__m512i*)ptrs[0];
                data2 = *(__m512i*)ptrs[1];
                out1 = _mm512_mask_permutex2var_epi16(data1, 0xFFFFFFFF,
                                                      (__m512i) interweave_idx1, data2);
                out2 = _mm512_mask_permutex2var_epi16(data1, 0xFFFFFFFF,
                                                      (__m512i) interweave_idx2, data2);
                _mm512_stream_si512((__m512i*)curr_ptr, out1);
                _mm512_stream_si512((__m512i*)curr_ptr2, out2);
                ptrs[0] += 32;
                ptrs[1] += 32;
                buff_pos += 64;
                if (buff_pos >= buff_sz_nele * 2) {
                    buff_pos = 0;
                }
            }
        }
        // NO SUPPORT FOR MORE THAN 2 PHYS OUTPUTS AT THE MOMENT
        for (int i = 0; i < n_phys_chn; ++i) {
            (*m_stm_mngrs[m_out_chns[i]]).consume_output(count / 2 / n_phys_chn);
        }
        //printf("m_output_cnt, controller: %lu\n", m_output_cnt);
        m_output_cnt += count / 2 / n_phys_chn / 32; // stream times are in units of 32 samples
        CPU::wake();
    }
    uint64_t finish_time = cycleclock();
    for (int i = 0; i < n_phys_chn; ++i) {
        //printf("Stopping stream %u\n", m_out_chns[i]);
        (*m_stm_mngrs[m_out_chns[i]]).stop_streams();
        (*m_stm_mngrs[m_out_chns[i]]).stop_worker();
        (*m_stm_mngrs[m_out_chns[i]]).reset_streams_out();
        (*m_stm_mngrs[m_out_chns[i]]).reset_out();
    }
    res["t"] = (finish_time - initial_clock) / 3e9;
    res["nele"] = nele;
    res["buff_sz_nele"] = buff_sz_nele;
    res["rate"] = nele / ((finish_time - initial_clock) / 3e9); // amount of time elements per second
    res["number of samples per second"] =  32 * nele / ((finish_time - initial_clock) / 3e9);
    res["ratio with 625e6"] = 32 * nele / ((finish_time - initial_clock) / 3e9) / 625e6;
    printf("Worker finishing\n");
    return res;
}
}
