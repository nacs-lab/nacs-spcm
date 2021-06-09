//

#include <nacs-spcm/Config.h>
#include <string>
#include <iostream>

using namespace Spcm;
int main() {
    std::string fname = "/etc/server_config.yml";
    Config conf;
    conf = conf.loadYAML(fname.data());
    std::cout << "listen: " << conf.listen << std::endl;
    std::cout << "amp: " << conf.amp << std::endl;
    std::cout << "sample_rate_mhz: " << conf.sample_rate << std::endl;
    std::cout << "trig_delay: " << conf.trig_delay << std::endl;
}
