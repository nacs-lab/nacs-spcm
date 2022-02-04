#define LLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING 1

#include <nacs-spcm/ControllerText.h>
#include <yaml-cpp/yaml.h>

using namespace NaCs;
int main(int argc, char **argv)
{
    std::vector<uint8_t> out_chns;
    out_chns.push_back(0);
    ::Spcm::ControllerText ctxt = ::Spcm::ControllerText(out_chns);
    size_t nele = 8 * 1024ll * 1024ll;
    size_t buff_sz_nele = 512 * 1024ll * 1024ll;
    uint32_t nrep = 10;
    for (int i = 0; i < nrep; i++) {
        auto res = ctxt.testCompute(nele, buff_sz_nele);
        YAML::Emitter out;
        out << res;
        std::cout << out.c_str() << std::endl;
    }
    return 0;
}
