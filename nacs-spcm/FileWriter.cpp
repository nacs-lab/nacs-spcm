#include "FileWriter.h"

using namespace NaCs;

namespace Spcm {

NACS_EXPORT() void FileWriter::send_cmds(Cmd *p, size_t sz) {
    uint32_t nwrote;
    do {
        nwrote = stm_mngr.copy_cmds(p, sz);
        p += nwrote;
        sz -= nwrote;
    } while (sz > 0);
    stm_mngr.flush_cmd();
    stm_mngr.distribute_cmds();
}

__attribute__((target("avx512f,avx512bw"), flatten))
NACS_EXPORT() void FileWriter::compute_and_write(size_t nsamples) {
    // nsamples is in 32 sample chunks
    uint64_t m_output_cnt = 0;
    uint64_t count;
    size_t min_chunk = 64; // 32 4 byte samples is 128 bytes or 64 int16_t
    std::vector<const int16_t*> ptr_vec(num_streams, nullptr);
// File IO
    std::ofstream ofs(fname, std::ofstream::binary);
    std::filebuf* pbuf = ofs.rdbuf();
    stm_mngr.start_streams(true);
    while(m_output_cnt < nsamples) {
        size_t sz;
        size_t min_sz = 8 * 1024ll * 1024ll * 1024ll;
        for (uint32_t j = 0; j < num_streams; j++) {
        retry:
            ptr_vec[j] = stm_mngr.get_read_ptr(j, sz);
            if (sz < min_chunk) {
                CPU::pause();
                goto retry;
            }
            if (sz < min_sz) {
                min_sz = sz;
            }
        }
        min_sz = min_sz & ~(uint64_t)(min_chunk - 1);
        count = std::min(min_sz * 2, (nsamples - m_output_cnt) * 32 * 4); // units of bytes
        if (!count) {
            continue;
        }
        if (count & 1) {
            printf("Count: %lu\n", count);
            abort();
        }

        __m512 res;
        for (uint32_t i = 0; i < count / 2; i += 32) {
            res = _mm512_load_ps(ptr_vec[0] + i);
            for (uint32_t j = 1; j < num_streams; j++) {
                res = _mm512_add_ps(res, _mm512_load_ps(ptr_vec[j] + i));
            }
            pbuf->sputn((char*) &res, 64);
        }
        stm_mngr.consume_all_output(count / 2);
        m_output_cnt += count / 2 / 32;
        CPU::wake();
    }
    stm_mngr.stop_streams();
    stm_mngr.reset_streams_out();
    ofs.close();
}

}
