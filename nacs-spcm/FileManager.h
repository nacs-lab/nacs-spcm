#ifndef _NACS_SPCM_FILEMANAGER_H
#define _NACS_SPCM_FILEMANAGER_H

#include <iostream>
#include <fstream>
#include <nacs-utils/mem.h>
#include <immintrin.h>

using namespace NaCs;

namespace Spcm {

enum class BuffOp : uint8_t {
    Id = 1,
    Add = 2
};

class FileManager {
public:
    FileManager(std::string name)
        : fname(name)
    {
    }

    ~FileManager(){
        unmapPage(data, sz);
    }

    void load();
    __attribute__((target("avx512f,avx512bw"), flatten))
    void get_data(__m512i* out_buf, uint64_t sz, BuffOp op = BuffOp::Id, float amp = 1, float damp = 0);

    std::string fname;
    char* data;
    uint64_t pos = 0; // in units of bytes
    size_t sz;
};
}

#endif
