//#include <nacs-spcm/spcm.h>
#include <nacs-spcm/Stream.h>
#include <nacs-spcm/StreamManager.h>
//#include <nacs-utils/log.h>
#include <nacs-utils/mem.h>
#include <nacs-utils/thread.h>
#include <nacs-spcm/StreamManager.cpp>

template <int32_t start_val = 0, int32_t slope = 1>
int32_t LinearRamp(uint32_t t) {
    return start_val + slope * t;
}

int main() {
    std::atomic<uint64_t> cmd_underflow(0);
    std::atomic<uint64_t> underflow(0);
    Spcm::StreamManager sm{4, 4, 1, cmd_underflow, underflow, false, false};
    std::vector<Spcm::Cmd> cmd_vector;
    cmd_vector.push_back(Spcm::Cmd::getAddChn(0, 1));
    cmd_vector.push_back(Spcm::Cmd::getAddChn(0, 2));
    //cmd_vector.push_back(Spcm::Cmd::getAmpSet(2, 1, 3));
    //cmd_vector.push_back(Spcm::Cmd::getFreqFn(4, 2, 12, 10, (void(*)(void))&LinearRamp<2,1>));
    cmd_vector.push_back(Spcm::Cmd::getAmpSet(2, 2, 2));
    //cmd_vector.push_back(Spcm::Cmd::getAmpSet(5, 1, 2));
    cmd_vector.push_back(Spcm::Cmd::getAddChn(6, 4));
    cmd_vector.push_back(Spcm::Cmd::getAddChn(7, 6));
    cmd_vector.push_back(Spcm::Cmd::getAmpSet(8, 6, 1));
    cmd_vector.push_back(Spcm::Cmd::getAddChn(8, 3));
    cmd_vector.push_back(Spcm::Cmd::getAddChn(8, 9));
    cmd_vector.push_back(Spcm::Cmd::getAmpSet(10, 2, 0));
    cmd_vector.push_back(Spcm::Cmd::getAmpFn(10, 1,7, 3,(void(*)(void))&LinearRamp<1,2>));
    cmd_vector.push_back(Spcm::Cmd::getFreqSet(12, 2, 2));
    cmd_vector.push_back(Spcm::Cmd::getFreqSet(18, 6, 5));
    //cmd_vector.push_back(Spcm::Cmd::getDelChn(19, 1));
    //cmd_vector.push_back(Spcm::Cmd::getDelChn(19, 2));
    //cmd_vector.push_back(Spcm::Cmd::getDelChn(19, 4));
    //cmd_vector.push_back(Spcm::Cmd::getDelChn(19, 6));
    size_t sz = cmd_vector.size();
    while (sz > 0) {
        size_t new_sz;
        new_sz = sm.copy_cmds(cmd_vector.data(), sz);
        cmd_vector.erase(cmd_vector.begin(), cmd_vector.begin() + new_sz);
        sz -= new_sz;
    }
    // std::cout << cmd_vector << std::endl;
    std::cout << "here" << std::endl;
    size_t sz2;
    sz2 = sm.distribute_cmds();
    std::cout << "after distribution" << std::endl;
    sm.start_streams();
    sm.start_worker();
}
