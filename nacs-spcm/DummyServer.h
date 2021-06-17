//

#ifndef _NACS_SPCM_DUMMY_SERVER_H
#define _NACS_SPCM_DUMMY_SERVER_H

#include "Config.h"

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

class DummyServer {
public:
    DummyServer(Config conf);

    //bool startController();
    //bool controllerRunning() const;

    //bool runSeq(uint64_t client_id, uint64_t seq_id, const uint8_t *data, size_t sz, bool is_seq_sent, uint64_t seqcnt, uint32_t start_trigger_id);
    //bool seqDone(uint64_t) const;

    //bool stop();
    void run();
    private:
            //struct QueueItem {
            //    const SeqCache::Entry *entry;
            //    uint64_t id;
            //    uint32_t start_trigger;
            //    operator bool() const
            //    {
            //        return entry != nullptr;
            //    }
            //};
            //QueueItem popSeq();
    //void seqRunner();
    bool recvMore(zmq::message_t&);
public:
    Config m_conf;
    zmq::context_t m_zmqctx;
    zmq::socket_t m_zmqsock;
    //const int m_evfd;
    //Controller m_ctrl;
    //SeqCache m_cache;
    //std::atomic<uint64_t> m_seqfin{0};
    //mutable std::mutex m_seqlock;
    //std::condition_variable m_seqcv;
    //std::vector<QueueItem> m_seque;
    //bool m_running{false};
        uint64_t m_serv_id;
        //std::vector<uint64_t> m_client_ids;
    };

}

#endif
