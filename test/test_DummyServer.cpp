//

#include <nacs-spcm/DummyServer.h>

#include <iostream>

using namespace NaCs;
int main()
{
    std::string fname = "/etc/server_config.yml";
    Spcm::Config conf;
    conf = conf.loadYAML(fname.data());
    Spcm::DummyServer serv{conf};
    std::cout << serv.m_conf.listen << std::endl;
    std::cout << "serv id: " << serv.m_serv_id << std::endl;
    serv.run();
}
