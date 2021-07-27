//

#include "Config.h"

#include <yaml-cpp/yaml.h>

using namespace NaCs;
namespace Spcm{

NACS_EXPORT() Config::Config()
: listen("tcp://*:8888")
{
}

NACS_EXPORT() Config Config::loadYAML(const char *fname)
{
    Config conf;
    auto file = YAML::LoadFile(fname);
    if (auto listen_node = file["listen"])
        conf.listen = listen_node.as<std::string>();
    if (auto amp_node = file["amp"])
        conf.amp = amp_node.as<uint32_t>();
    if (auto sample_rate_node = file["sample_rate_mhz"])
        conf.sample_rate = sample_rate_node.as<int64_t>() * 1e6;
    if (auto trig_node = file["trig_delay_ms"])
        conf.trig_delay = uint64_t(trig_node.as<double>() * conf.sample_rate / (32 * 1e3)); // units of stream times
    if (auto clock_node = file["clock_cycle_factor"])
        conf.ext_clock_cycle_factor = uint64_t(clock_node.as<double>());
    if (conf.sample_rate  > 71.68e6) {
        conf.clock_factor = 8 * conf.ext_clock_cycle_factor;
    }
    else {
        conf.clock_factor = 4 * conf.ext_clock_cycle_factor;
    }
    return conf;
}

}
