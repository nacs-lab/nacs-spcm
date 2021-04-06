//

#ifndef _NACS_SPCM_CONFIG_H
#define _NACS_SPCM_CONFIG_H

#include <string>
#include <map>

#include <stdint.h>

using namespace NaCs;
namespace Spcm {
struct Config {
    Config();
    std::string listen;
    uint32_t amp{2500};
    int64_t sample_rate{static_cast<int64_t> (625e6)};
    static Config loadYAML(const char *fname);
};
}

#endif
