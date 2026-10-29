//

#ifndef _NACS_SPCM_CONFIG_H
#define _NACS_SPCM_CONFIG_H

#include <string>
#include <map>
#include <vector>

#include <nacs-utils/utils.h>
#include <stdint.h>

using namespace NaCs;
namespace Spcm {
struct Config {
    Config();
    std::string listen;
    uint32_t amp{2500};
    int64_t sample_rate{static_cast<int64_t> (40e6)};
    uint64_t trig_delay{1};
    uint64_t ext_clock_cycle_factor{static_cast<uint64_t> (16)};
    double clock_factor{8 * 16};
    // RF tone streams (each up to 4 tones) per card output. Every stream is a thread that
    // busy-waits, so unused streams cost a CPU core each; 0 leaves only the analog
    // (pulse shape) stream on that output.
    std::vector<uint32_t> streams_per_output{8, 1, 1, 1};
    static Config loadYAML(const char *fname);
};
}

#endif
