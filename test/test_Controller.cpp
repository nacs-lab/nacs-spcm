#include <nacs-spcm/Stream.h>
#include <nacs-spcm/StreamManager.h>
#include <nacs-spcm/Controller.h>
//#include <nacs-spcm/Controller.h>
//#include <nacs-utils/log.h>
#include <nacs-utils/mem.h>
#include <nacs-utils/thread.h>
//#include <nacs-spcm/StreamManager.cpp>

#include <thread>
#include <chrono>

template <int64_t start_val = 0, int64_t slope = 1>
float LinearRamp(uint32_t t) {
    return start_val + slope * t;
}

int main() {
    ::Spcm::Controller ctrl{};
    //std::atomic<uint64_t> cmd_underflow(0);
    //std::atomic<uint64_t> underflow(0);
    //Spcm::StreamManager sm{4, 4, 1, cmd_underflow, underflow, false, false};
    std::vector<::Spcm::Cmd> cmd_vector;
    // code at this level expects phase_cnt, freq_cnt, amp that spans entire range, and t is in units of 32 samples.
    constexpr double phase_to_phase_cnt = 625e7f;
    constexpr double amp_scale = 6.7465185e9f;
    constexpr double t_scale = 625e6f/32;
    cmd_vector.push_back(::Spcm::Cmd::getAddChn(0, 1));
    cmd_vector.push_back(::Spcm::Cmd::getAddChn(0, 2));
    cmd_vector.push_back(::Spcm::Cmd::getAddChn(0, 3));
    //cmd_vector.push_back(Spcm::Cmd::getAmpSet(2, 1, 3));
    //cmd_vector.push_back(Spcm::Cmd::getFreqFn(4, 2, 12, 10, (void(*)(void))&LinearRamp<2,1>));
    cmd_vector.push_back(::Spcm::Cmd::getFreqSet(1 * t_scale, 1, int32_t(round(75e6f * 10))));
    cmd_vector.push_back(::Spcm::Cmd::getAmpSet(1 * t_scale, 1, 0.1f * amp_scale));
    cmd_vector.push_back(::Spcm::Cmd::getFreqSet(10 * t_scale, 2, 80e6f * 10));
    cmd_vector.push_back(::Spcm::Cmd::getAmpSet(10 * t_scale, 2, 0.3f * amp_scale));
    cmd_vector.push_back(::Spcm::Cmd::getFreqSet(11 * t_scale, 3, 70e6f * 10));
    cmd_vector.push_back(::Spcm::Cmd::getAmpSet(11 * t_scale, 3, 0.1f * amp_scale));
    constexpr int64_t start_val1 = 81e6f * 10;
    constexpr int64_t slope1 = 2e6f * 10 / t_scale;
    constexpr int64_t start_val2 = 0.3f * amp_scale;
    constexpr int64_t slope2 = -0.3f * amp_scale / (5 * t_scale);
    std::cout << "slope1: " << slope1 << std::endl;
    std::cout << "slope2: " << slope2 << std::endl;
    cmd_vector.push_back(::Spcm::Cmd::getFreqFn(13 * t_scale, 3, 91e6f * 10, 5 * t_scale, (void(*)(void))&LinearRamp<start_val1, slope1>));
    cmd_vector.push_back(::Spcm::Cmd::getAmpFn(18* t_scale, 2, 0, 5 * t_scale, (void(*)(void))&LinearRamp<start_val2, slope2>));
    //cmd_vector.push_back(::Spcm::Cmd::getFreqSet(3 * t_scale, 3, 75e6 * 10));
    //cmd_vector.push_back(::Spcm::Cmd::getAmpSet(3 * t_scale, 3, 0.1 * amp_scale));
    //cmd_vector.push_back(Spcm::Cmd::getAmpSet(5, 1, 2));
    //cmd_vector.push_back(Spcm::Cmd::getAddChn(6, 4));
    //cmd_vector.push_back(Spcm::Cmd::getAddChn(7, 6));
    //cmd_vector.push_back(Spcm::Cmd::getAmpSet(8, 6, 1));
    //cmd_vector.push_back(Spcm::Cmd::getAddChn(8, 3));
    //cmd_vector.push_back(Spcm::Cmd::getAddChn(8, 9));
    //cmd_vector.push_back(Spcm::Cmd::getAmpSet(10, 2, 0));
    //cmd_vector.push_back(Spcm::Cmd::getAmpFn(10, 1,7, 3,(void(*)(void))&LinearRamp<1,2>));
    //cmd_vector.push_back(Spcm::Cmd::getFreqSet(12, 2, 2));
    //cmd_vector.push_back(Spcm::Cmd::getFreqSet(18, 6, 5));
    //cmd_vector.push_back(Spcm::Cmd::getDelChn(19, 1));
    //cmd_vector.push_back(Spcm::Cmd::getDelChn(19, 2));
    //cmd_vector.push_back(Spcm::Cmd::getDelChn(19, 4));
    //cmd_vector.push_back(Spcm::Cmd::getDelChn(19, 6));
    size_t sz = cmd_vector.size();
    //while (sz > 0) {
    //    size_t new_sz;
    //    new_sz = sm.copy_cmds(cmd_vector.data(), sz);
    //    cmd_vector.erase(cmd_vector.begin(), cmd_vector.begin() + new_sz);
    //    sz -= new_sz;
    //}
    // std::cout << cmd_vector << std::endl;
    //std::cout << "here" << std::endl;

    //send commands to stream_manager
    //ctrl.init(); // initialize controller
    ctrl.runSeq(cmd_vector.data(), sz, false);
    //ctrl.force_trigger();
    //size_t sz2;
    //sz2 = sm.distribute_cmds();
    //std::cout << "after distribution" << std::endl;
    //sm.start_streams();
    //sm.start_worker();
    uint64_t avail;
    while (1) {
        avail = ctrl.check_avail();
        std::cout << avail << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        if (avail == 0)
            break;
    }
    ctrl.force_trigger();
    while (1) {
        //ctrl.force_trigger();
        avail = ctrl.check_avail();
        std::cout << avail << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}
