#define LLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING 1

#include <nacs-spcm/ControllerText.h>
#include <yaml-cpp/yaml.h>
#include <numeric>
#include <stdlib.h>

#include <nacs-utils/log.h>

using namespace NaCs;

int test_stream_combining(int nrep)
{
    printf("nrep: %d\n", nrep);
    std::vector<uint8_t> out_chns;
    out_chns.push_back(0);
    size_t nele = 8 * 1024ll * 1024ll;
    size_t buff_sz_nele = 2 * 1024ll * 1024ll;
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
    return 0;
}
