#define LLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING 1

#include <nacs-spcm/ControllerText.h>
#include <nacs-spcm/Stream.h>
#include <yaml-cpp/yaml.h>
#include <numeric>
#include <stdlib.h>

#include <nacs-utils/log.h>

using namespace NaCs;

int test_tone_generation_fixed_tones(int nrep, int ntones)
{
    bool two_outputs = true;
    printf("nrep: %d\n", nrep);
    printf("ntones_per_thread: %d\n", ntones);
    if (two_outputs) {
        printf("Two outputs\n");
    }
    std::vector<uint8_t> out_chns;
    out_chns.push_back(0);
    if (two_outputs)
        out_chns.push_back(1);
    size_t nele = 8 * 1024ll * 1024ll;
    size_t buff_sz_nele = 2 * 1024ll * 1024ll;
    //std::vector<uint32_t> stream_nums = {1,2,3,4,5,6,7,8,9,10};
    std::vector<uint32_t> stream_nums = {1,2,3,4,5,6,7,8,9,10};
    //std::vector<uint32_t> stream_nums = {1};
    std::vector<double> rates;

    std::vector<::Spcm::Cmd> cmd_vector;
    double start_freq = 70e6;
    double delta_freq = 100e3;
    double amp = 0.9f / (static_cast<double> (ntones));
    uint32_t nwrote;
    for (int i = 0; i < stream_nums.size(); i++) {
        cmd_vector.clear();
        for (int j = 0; j < ntones * stream_nums[i]; j++) {
            cmd_vector.push_back(::Spcm::Cmd::getAddChn(0,0,j,j));
        }
        for (int j = 0; j < ntones * stream_nums[i]; j++) {
            cmd_vector.push_back(::Spcm::Cmd::getFreqSet(0,0,ntones * stream_nums[i] + j, j, start_freq + j * delta_freq));
            cmd_vector.push_back(::Spcm::Cmd::getAmpSet(0,0,ntones * stream_nums[i] + j, j, amp));
        }
        cmd_vector.push_back(::Spcm::Cmd::getStopCheck());
        // Compute once that doesn't get included in the average
        ::Spcm::ControllerText ctxt = ::Spcm::ControllerText(out_chns, stream_nums[i]);
        auto p = &cmd_vector[0];
        auto sz = cmd_vector.size();
        do {
            nwrote = ctxt.copy_cmds(0, p, sz);
            p += nwrote;
            sz -= nwrote;
        } while (sz > 0);
        ctxt.flush_cmd(0);
        ctxt.distribute_cmds(0);
        if (two_outputs) {
            p = &cmd_vector[0];
            sz = cmd_vector.size();
            do {
                nwrote = ctxt.copy_cmds(1, p, sz);
                p += nwrote;
                sz -= nwrote;
            } while (sz > 0);
            ctxt.flush_cmd(1);
            ctxt.distribute_cmds(1);
        }
        ctxt.testCompute(nele, buff_sz_nele);
        for (int j = 0; j < nrep; j++) {
            p = &cmd_vector[0];
            sz = cmd_vector.size();
            do {
                nwrote = ctxt.copy_cmds(0, p, sz);
                p += nwrote;
                sz -= nwrote;
            } while (sz > 0);
            ctxt.flush_cmd(0);
            ctxt.distribute_cmds(0);
            ctxt.set_chk_cmd(0, true);
            if (two_outputs) {
                p = &cmd_vector[0];
                sz = cmd_vector.size();
                do {
                    nwrote = ctxt.copy_cmds(1, p, sz);
                    p += nwrote;
                    sz -= nwrote;
                } while (sz > 0);
                ctxt.flush_cmd(1);
                ctxt.distribute_cmds(1);
                ctxt.set_chk_cmd(1, true);
            }
            auto res = ctxt.testCompute(nele, buff_sz_nele);
            auto node = res["ratio with 625e6"];
            rates.push_back(node.as<double>());
        }
        // Average and print out
        auto avg = 1.0f * std::accumulate(rates.begin(), rates.end(), 0.0f) / rates.size();
        printf("Number of Streams: %lu, Number of tones: %lu, Average Ratio: %f\n", stream_nums[i], stream_nums[i] * ntones, avg);
        rates.clear();
    }
    return 0;
}

int test_tone_generation(int nrep, int ntones)
{
    bool two_outputs = false;
    printf("nrep: %d\n", nrep);
    printf("ntones: %d\n", ntones);
    if (two_outputs) {
        printf("Two outputs\n");
    }
    std::vector<uint8_t> out_chns;
    out_chns.push_back(0);
    if (two_outputs)
        out_chns.push_back(1);
    size_t nele = 8 * 1024ll * 1024ll;
    size_t buff_sz_nele = 2 * 1024ll * 1024ll;
    //std::vector<uint32_t> stream_nums = {1,2,3,4,5,6,7,8,9,10};
    std::vector<uint32_t> stream_nums = {1,2,3,4,5,6,7,8,9,10};
    //std::vector<uint32_t> stream_nums = {1};
    std::vector<double> rates;

    std::vector<::Spcm::Cmd> cmd_vector;
    double start_freq = 70e6;
    double delta_freq = 100e3;
    double amp = 0.9f / (static_cast<double> (ntones));
    uint32_t nwrote;
    for (int i = 0; i < ntones; i++) {
        cmd_vector.push_back(::Spcm::Cmd::getAddChn(0,0,i,i));
    }
    for (int i = 0; i < ntones; i++) {
        cmd_vector.push_back(::Spcm::Cmd::getFreqSet(0,0,ntones + i, i, start_freq + i * delta_freq));
        cmd_vector.push_back(::Spcm::Cmd::getAmpSet(0,0,ntones + i, i, amp));
    }
    cmd_vector.push_back(::Spcm::Cmd::getStopCheck());
    for (int i = 0; i < stream_nums.size(); i++) {
        // Compute once that doesn't get included in the average
        ::Spcm::ControllerText ctxt = ::Spcm::ControllerText(out_chns, stream_nums[i]);
        auto p = &cmd_vector[0];
        auto sz = cmd_vector.size();
        do {
            nwrote = ctxt.copy_cmds(0, p, sz);
            p += nwrote;
            sz -= nwrote;
        } while (sz > 0);
        ctxt.flush_cmd(0);
        ctxt.distribute_cmds(0);
        if (two_outputs) {
            p = &cmd_vector[0];
            sz = cmd_vector.size();
            do {
                nwrote = ctxt.copy_cmds(1, p, sz);
                p += nwrote;
                sz -= nwrote;
            } while (sz > 0);
            ctxt.flush_cmd(1);
            ctxt.distribute_cmds(1);
        }
        ctxt.testCompute(nele, buff_sz_nele);
        for (int j = 0; j < nrep; j++) {
            p = &cmd_vector[0];
            sz = cmd_vector.size();
            do {
                nwrote = ctxt.copy_cmds(0, p, sz);
                p += nwrote;
                sz -= nwrote;
            } while (sz > 0);
            ctxt.flush_cmd(0);
            ctxt.distribute_cmds(0);
            ctxt.set_chk_cmd(0, true);
            if (two_outputs) {
                p = &cmd_vector[0];
                sz = cmd_vector.size();
                do {
                    nwrote = ctxt.copy_cmds(1, p, sz);
                    p += nwrote;
                    sz -= nwrote;
                } while (sz > 0);
                ctxt.flush_cmd(1);
                ctxt.distribute_cmds(1);
                ctxt.set_chk_cmd(1, true);
            }
            auto res = ctxt.testCompute(nele, buff_sz_nele);
            auto node = res["ratio with 625e6"];
            rates.push_back(node.as<double>());
        }
        // Average and print out
        auto avg = 1.0f * std::accumulate(rates.begin(), rates.end(), 0.0f) / rates.size();
        printf("Number of Streams: %lu, Average Ratio: %f\n", stream_nums[i], avg);
        rates.clear();
    }
    return 0;
}

int test_stream_combining(int nrep)
{
    printf("nrep: %d\n", nrep);
    std::vector<uint8_t> out_chns;
    out_chns.push_back(0);
    //out_chns.push_back(1);
    size_t nele = 8 * 1024ll * 1024ll;
    size_t buff_sz_nele = 2 * 1024ll * 1024ll;
    //std::vector<uint32_t> stream_nums = {1,2,3,4,5,6,7,8,9,10};
    std::vector<uint32_t> stream_nums = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    //std::vector<uint32_t> stream_nums = {1};
    std::vector<double> rates;

    for (int i = 0; i < stream_nums.size(); i++) {
        // Compute once that doesn't get included in the average
        ::Spcm::ControllerText ctxt = ::Spcm::ControllerText(out_chns, stream_nums[i]);
        ctxt.testCompute(nele, buff_sz_nele);
        for (int j = 0; j < nrep; j++) {
            auto res = ctxt.testCompute(nele, buff_sz_nele);
            auto node = res["ratio with 625e6"];
            rates.push_back(node.as<double>());
        }
        // Average and print out
        auto avg = 1.0f * std::accumulate(rates.begin(), rates.end(), 0.0f) / rates.size();
        printf("Number of Streams: %lu, Average Ratio: %f\n", stream_nums[i], avg);
        rates.clear();
    }
    return 0;
}


int main(int argc, char **argv)
{
    //uint32_t nstreams = 15;
    //std::vector<uint8_t> out_chns;
    //out_chns.push_back(0);
    //::Spcm::ControllerText ctxt = ::Spcm::ControllerText(out_chns, nstreams);
    //size_t nele = 8 * 1024ll * 1024ll;
    //size_t buff_sz_nele = 512 * 1024ll * 1024ll;
    //uint32_t nrep = 10;
    //for (int i = 0; i < nrep; i++) {
    //    auto res = ctxt.testCompute(nele, buff_sz_nele);
    //    YAML::Emitter out;
    //    out << res;
    //    std::cout << out.c_str() << std::endl;
    //}
    if (argc < 2) {
        Log::error("No action specified.\n");
        return 1;
    }
    if (strcmp(argv[1], "test_stream_combining") == 0) {
        return test_stream_combining(atoi(argv[2]));
    }
    else if (strcmp(argv[1], "test_tone_generation") == 0) {
        return test_tone_generation(atoi(argv[2]), atoi(argv[3]));
    }
    else if (strcmp(argv[1], "test_tone_generation_fixed_tones") == 0) {
        return test_tone_generation_fixed_tones(atoi(argv[2]), atoi(argv[3]));
    }
    return 0;
}
