//

#ifndef _NACS_SPCM_CONFIG_H
#define _NACS_SPCM_CONFIG_H

#include <string>
#include <map>

#include <nacs-utils/utils.h>
#include <stdint.h>

using namespace NaCs;
namespace Spcm {
struct Config {
    Config();
    Config(std::string fname);
    std::string listen;
    uint32_t amp{2500};
    int64_t sample_rate{static_cast<int64_t> (625e6)};
    uint64_t trig_delay{1};
    uint64_t ext_clock_cycle_factor{static_cast<uint64_t> (16)};
    double clock_factor{8 * 16};
    static Config loadYAML(const char *fname);
};
}

#endif
