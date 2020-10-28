//#include <nacs-spcm/spcm.h>
#include <nacs-spcm/Stream.h>
//#include <nacs-utils/log.h>
#include <nacs-utils/mem.h>
#include <nacs-utils/thread.h>

template <int32_t start_val = 0, int32_t slope = 1>
int32_t LinearRamp(uint32_t t) {
    return start_val + slope * t;
}


namespace Spcm{
void DoThis()
{
    double step_t = 1; // not relevant here
    std::atomic<uint64_t> cmd_underflow(0);
    std::atomic<uint64_t> underflow(0); // not relevant either
    Stream<128> this_stream{step_t, cmd_underflow, underflow, false};
    std::vector<Cmd> cmd_vector;
    cmd_vector.push_back(Cmd::getAddChn(0));
    cmd_vector.push_back(Cmd::getAddChn(0));
    cmd_vector.push_back(Cmd::getAmpSet(1, 0, 3));
    cmd_vector.push_back(Cmd::getFreqFn(4, 0, 12, 10, (void(*)(void))&LinearRamp<2,1>));
    cmd_vector.push_back(Cmd::getAmpSet(5, 1, 2));
    cmd_vector.push_back(Cmd::getAmpSet(10,0, 0));
    cmd_vector.push_back(Cmd::getAmpFn(12,0,7,3,(void(*)(void))&LinearRamp<1,2>));
    cmd_vector.push_back(Cmd::getFreqSet(18, 0, 5));
    cmd_vector.push_back(Cmd::getDelChn(0,0));
    size_t sz = cmd_vector.size();
    while (sz > 0) {
        size_t new_sz;
        new_sz = this_stream.copy_cmds(cmd_vector.data(), sz);
        cmd_vector.erase(cmd_vector.begin(), cmd_vector.begin() + new_sz);
        sz -= new_sz;
    }
    // std::cout << cmd_vector << std::endl;
    this_stream.start_worker();
}
}

int main() {
    Spcm::DoThis();
}
