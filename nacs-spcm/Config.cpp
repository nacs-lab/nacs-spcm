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
        conf.sample_rate = sample_rate_node.as<int64_t>();
    return conf;
}

}
