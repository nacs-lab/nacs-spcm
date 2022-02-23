//

#ifndef _NACS_SPCM_FSTREAMMANAGER_H
#define _NACS_SPCM_FSTREAMMANAGER_H

#include <nacs-spcm/FileCache.h>
#include <nacs-spcm/FileStream.h>
#include <nacs-spcm/Config.h>
#include <nacs-spcm/StreamManager.h>



using namespace NaCs;

namespace Spcm {

class Controller;

struct FileStreamManager : StreamManagerBase {
    FileStreamManager(Controller &ctrl, FileCache& fcache, Config &conf, uint32_t n_streams, uint32_t max_per_stream,
                  std::atomic<uint64_t> &cmd_underflow,
                  std::atomic<uint64_t> &underflow, bool startStream = false,
                  bool startWorker = false)
        : m_ctrl(ctrl),
          m_fcache(fcache),
          StreamManagerBase(conf, n_streams, max_per_stream)
    {
        // start streams
        for (int i = 0; i < n_streams; i++) {
            FStream *stream_ptr;
            stream_ptr = new FStream(*this, fcache, conf, cmd_underflow, underflow, i, startStream);
            m_streams.push_back(stream_ptr);
            stream_ptrs.push_back(nullptr);
        }
        /*if (startWorker)
        {
            start_worker();
            }*/
    }
/*
    void start_worker()
    {
        m_stop.store(false, std::memory_order_relaxed);
        m_worker = std::thread(&StreamManager::thread_fun, this);
    }
    void stop_worker()
    {
        m_stop.store(true, std::memory_order_relaxed);
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }

    void reset_out() {
        printf("reset out stm mngr called\n");
        if (m_worker.joinable()) {
            stop_worker();
        }
        //reset_output();
        //size_t sz;
        //m_output.sync_reader();
        //m_output.get_read_ptr(&sz);
        //m_output.read_size(sz); // reset my own output.
    }
*/
    ~FileStreamManager()
    {
        //stop_worker();
    }
    bool reqRestart(uint32_t trig_id);
private:
    Controller& m_ctrl;
    FileCache & m_fcache;
/*
    void thread_fun()
    {
        //while (get_cur_t() < 19) {
        //    generate_page();
        //}
        // after page generation, let's read the data to test it.
        //const int *ptr;
        //size_t sz;
        //ptr = get_output(sz);
        //ChannelMap cmap = get_chn_map();
        //std::cout << cmap;
        //for (int i = 0; i < sz; i++) {
        //    std::cout << i << ": " << ptr[i] << std::endl;
        //}
        while(likely(!m_stop.load(std::memory_order_relaxed))) {
            //std::cout << "here" << std::endl;
            generate_page();
        }
        }*/

    //std::thread m_worker{};
};


}

#endif
