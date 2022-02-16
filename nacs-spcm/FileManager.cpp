#include "FileManager.h"

using namespace NaCs;

namespace Spcm {

typedef short v32si __attribute__((vector_size(64)));
static constexpr v32si mask0 = {1, 3, 5, 7, 9, 11, 13, 15,
                                17, 19, 21, 23, 25, 27, 29, 31,
                                33, 35, 37, 39, 41, 43, 45, 47,
                                49, 51, 53, 55, 57, 59, 61, 63};

NACS_EXPORT() void FileManager::load(){
    std::ifstream ifs(fname, std::ifstream::binary);

    // get pointer to associated buffer object
    std::filebuf* pbuf = ifs.rdbuf();

    // get file size
    sz = pbuf->pubseekoff(0, ifs.end, ifs.in);
    if (sz % 128 != 0) {
        // 128 bytes corresponds to 32 4 byte floats, which will become 32 16 bit ints.
        sz = 0;
        printf("Number of bytes in file %s is not a multiple of 128\n", fname.c_str());
    }
    else {

        pbuf->pubseekpos(0, ifs.in);

        // allocate memory to contain file data
        data = (char*) mapAnonPage(sz, Prot::RW);
        pos = 0;

        // get file data
        pbuf->sgetn(data, sz);
    }
    ifs.close();
}

__attribute__((target("avx512f,avx512bw"), flatten))
NACS_EXPORT() void FileManager::get_data(__m512i* out_buf, size_t write_sz, BuffOp op, float amp) {
    // write_sz is in units of 512 bits
    printf("sz: %lu\n", sz);
    if (sz) {
        __m512 fdata1, fdata2, ampv;
        __m512i res;
        if (amp != 1) {
            ampv = _mm512_set1_ps(amp);
        }
        for (uint32_t i = 0; i < write_sz; i++) {
            fdata1 = _mm512_load_ps(data + pos);
            fdata2 = _mm512_load_ps(data + pos + 64);
            if (amp != 1) {
                fdata1 = fdata1 * ampv;
                fdata2 = fdata2 * ampv;
            }
            res = _mm512_permutex2var_epi16(_mm512_cvttps_epi32(fdata1), (__m512i)mask0,
                                            _mm512_cvttps_epi32(fdata2));
            if (op == BuffOp::Id) {
                //printf("%u\n", i);
                _mm512_stream_si512(out_buf + i, res);
            }
            else if(op == BuffOp::Add) {
                _mm512_stream_si512(out_buf + i, _mm512_add_epi16(out_buf[i], res));
            }
            pos += 128;
            if (pos >= sz) {
                pos = 0;
            }
        }
    }
}
}
