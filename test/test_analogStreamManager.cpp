//

#define LLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING 1

// #include <nacs-spcm/Trigger.h>
//#include <nacs-spcm/DummyServer.h>
#include <nacs-spcm/Server.h>
// #include <nacs-spcm/Stream.h>

#include <iostream>

//template <double start_val = 0, double slope = 1>
double LinearRamp(double t) {
    return 0.8 - 0.1 * t;
}

using namespace NaCs;
int main(int argc, char **argv)
{
    std::cout << sizeof(::Spcm::Cmd) << std::endl;

    std::atomic<uint64_t> cmd_underflow(0);
    std::atomic<uint64_t> underflow(0); // not relevant either
    std::string fname = "/etc/server_config.yml";
    ::Spcm::Config conf;
    conf = conf.loadYAML(fname.data());
    ::Spcm::Server serv{conf, true};
    auto &ctrl = serv.getController();
    auto &stream_mgrs = ctrl.get_stream_mgrs();
    auto &first_stream_mgr = *(stream_mgrs[0].get());

    // double step_t = 1;
    // double amp_scale = 1000;
    // uint32_t stream_num = 0;
    // bool start = false;

    // auto this_stream = ::Spcm::AnalogStream(first_stream_mgr, conf, step_t, amp_scale, cmd_underflow,
    //     underflow, stream_num, start);

    // getAnalogSet(int64_t t, int64_t t_client, uint32_t id, uint32_t chn, double amp)
    //static Cmd getAddChn(int64_t t, int64_t t_client, uint32_t id = 0)
    // static Cmd getAnalogFn(int64_t t, int64_t t_client, uint32_t id, uint32_t chn, double final_val, double len, void(*fnptr)(void))
    std::vector<::Spcm::Cmd> cmd_vector;
    cmd_vector.push_back(::Spcm::Cmd::getAnalogAddChn(0, 0, 0));
    cmd_vector.push_back(::Spcm::Cmd::getAnalogSet(2, 2, 0, 0, 0.5, 0));
    cmd_vector.push_back(::Spcm::Cmd::getAnalogSet(2, 2, 0, 0, 0.8, 1));
    cmd_vector.push_back(::Spcm::Cmd::getAnalogSet(2, 2, 0, 0, 0.6, 21));
    cmd_vector.push_back(::Spcm::Cmd::getAnalogFn(3, 3, 0, 0, 0.2, 3, (void(*)(void))&LinearRamp));
    uint32_t nwrote;
    auto p = cmd_vector.data();
    auto sz = cmd_vector.size();
    do {
        nwrote = ctrl.copy_cmds(0, p, sz);
        p += nwrote;
        sz -= nwrote;
    }
    while (sz > 0);

    ctrl.flush_cmd(0);
    ctrl.distribute_cmds(0);
    first_stream_mgr.start_streams();
    first_stream_mgr.start_worker();
    // cmd_vector.push_back(Cmd::getDelChn(0,0));
    // size_t sz = cmd_vector.size();
    // while (sz > 0) {
    //     size_t new_sz;
    //     new_sz = this_stream.copy_cmds(cmd_vector.data(), sz);
    //     cmd_vector.erase(cmd_vector.begin(), cmd_vector.begin() + new_sz);
    //     sz -= new_sz;
    // }
    // this_stream.flush_cmd();
    // this_stream.start_worker();
    size_t n_samples = 32 * 7; 
    size_t sz_to_read = 0;
    const int16_t *read_ptr;
    while (sz_to_read < n_samples) {
        read_ptr = first_stream_mgr.get_output(sz_to_read);
    }
    int16_t buff[n_samples];
    memcpy(buff, read_ptr, n_samples * sizeof(int16_t));
    for (int i = 0; i < n_samples; i++) {
        std::cout << "buff[" << i << "] = " << buff[i] << std::endl;
    }
    first_stream_mgr.stop_streams();
    first_stream_mgr.stop_worker();
}
