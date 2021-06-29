//

#define LLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING 1

#include <nacs-spcm/Trigger.h>
//#include <nacs-spcm/DummyServer.h>
#include <nacs-spcm/Server.h>

#include <iostream>

using namespace NaCs;
int main(int argc, char **argv)
{
    std::string fname = "/etc/server_config.yml";
    ::Spcm::Config conf;
    conf = conf.loadYAML(fname.data());
    ::Spcm::Server serv{conf};
    // std::cout << serv.m_conf.listen << std::endl;
    // std::cout << "serv id: " << serv.m_serv_id << std::endl;
    ::Spcm::Trigger trigger(argv[1]);
    //printf("trigger fd %i", trigger.fd);
    //std::cout << "trigger fd " << (int) trigger.fd << std::endl;
    serv.run(trigger.fd, trigger.cb);
}
