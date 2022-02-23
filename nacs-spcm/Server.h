//

#ifndef _NACS_SPCM_SERVER_H
#define _NACS_SPCM_SERVER_H

#include "Controller.h"
#include "Config.h"
#include "DummyController.h"
#include "SeqCache.h"
#include "FileStream.h"

#include <zmq.hpp>

#include <memory>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <utility>
#include <functional>

using namespace NaCs;

namespace Spcm {

//class Controller;

class Server {
public:
    Server(Config conf);

    bool startController();
    bool controllerRunning() const;

    bool runSeq(uint64_t client_id, uint64_t seq_id, const uint8_t *data, uint32_t &sz, bool is_seq_sent, uint64_t seqcnt, uint32_t start_trigger_id, bool is_first_seq, uint32_t ver);
    bool seqDone(uint64_t) const;

    bool stop();

    void run(int fd, const std::function<std::pair<uint32_t, int64_t>(int)> &cb);
    void run()
    {
        run(-1, [] (int) {return std::make_pair(0, 0);});
    }
    private:
            struct QueueItem {
                SeqCache::Entry *entry;
                uint64_t id;
                uint32_t start_trigger;
                bool is_first_seq;
                operator bool() const
                {
                    return entry != nullptr;
                }
            };
        QueueItem popSeq();
        void seqRunner();
        bool recvMore(zmq::message_t&);

        Config m_conf;
        zmq::context_t m_zmqctx;
        zmq::socket_t m_zmqsock;
        const int m_evfd;
    Controller m_ctrl;
    SeqCache m_cache;
        std::atomic<uint64_t> m_seqfin{0};
        mutable std::mutex m_seqlock;
        std::condition_variable m_seqcv;
        std::vector<QueueItem> m_seque;
        bool m_running{false};
    uint64_t m_serv_id;
    bool first_start{false};
    uint32_t restart_ctr;
        //std::vector<uint64_t> m_client_ids;
    //std::vector<uint8_t> init_out_chn = {1};
    };

}

#endif
