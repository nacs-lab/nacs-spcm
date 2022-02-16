#ifndef _NACS_SPCM_FILEWRITER_H
#define _NACS_SPCM_FILEWRITER_H

#include <iostream>
#include <fstream>

#include <nacs-spcm/StreamManager.h>
#include <nacs-spcm/Config.h>



using namespace NaCs;

namespace Spcm {

class FileWriter {
public:
    FileWriter(uint32_t nstreams,  std::string name)
        : num_streams(nstreams),
          fname(name),
          m_conf(conf_fname.data()),
          stm_mngr(m_conf, nstreams, 1000, 1, cmd_underflow,
                                     underflow, false, true)
    {
        //std::string conf_fname = "/etc/server_config.yml";
        //m_conf = m_conf.loadYAML(conf_fname.data());
    }

    ~FileWriter()
    {
    }

    void send_cmds(Cmd *p, size_t sz);
    void compute_and_write(size_t nsamples);

private:
    std::string conf_fname = "/etc/server_config.yml";
    uint32_t num_streams;
    std::string fname;
    Config m_conf;

    std::atomic<uint64_t> cmd_underflow{0};
    std::atomic<uint64_t> underflow{0};
    StreamManagerBase stm_mngr;
};

}

#endif
