#include <nacs-spcm/FileManager.h>
#include <nacs-spcm/FileWriter.h>
#include <nacs-utils/mem.h>

#include <immintrin.h>

using namespace NaCs;

__attribute__((target("avx512f,avx512bw"), flatten))
int main (int argc, char **argv) {
    uint32_t nstreams = 4;
    uint32_t nchunks = 2; // each chunk is 512 bits, and 32 samples
    uint32_t nchunks_read = 4;
    std::string fname = "/home/nilab/data/1.dat";
    ::Spcm::FileWriter fw = ::Spcm::FileWriter(nstreams, fname);

    std::vector<::Spcm::Cmd> cmd_vector;

    std::vector<double> freqs = {70e6};
    std::vector<double> amps = {0.2f};

    for (uint32_t i = 0; i < freqs.size(); i++) {
        cmd_vector.push_back(::Spcm::Cmd::getAddChn(0,0,i,i));
    }

    for (uint32_t i = 0; i < freqs.size(); i++) {
        cmd_vector.push_back(::Spcm::Cmd::getFreqSet(0,0,freqs.size() + 2 * i, i, freqs[i]));
        cmd_vector.push_back(::Spcm::Cmd::getAmpSet(0,0,freqs.size() + 2 * i + 1, i, amps[i]));
    }

    fw.send_cmds(cmd_vector.data(), cmd_vector.size());
    fw.compute_and_write(nchunks);

    ::Spcm::FileManager fm = ::Spcm::FileManager(fname);
    fm.load();

    int16_t *p = (int16_t*) mapAnonPage(nchunks_read * 64, Prot::RW);

    fm.get_data((__m512i*) p, nchunks_read, ::Spcm::BuffOp::Id, 1);

    for (uint32_t i = 0; i < nchunks_read * 32; i++) {
        printf("Sample %u, Data: %d\n", i, p[i]);
    }

    unmapPage(p, nchunks_read * 64);
}
