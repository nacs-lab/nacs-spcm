//

#include "DummyServer.h"

#include <nacs-utils/zmq_utils.h>
#include <nacs-utils/fd_utils.h>
#include <nacs-utils/timer.h>

#include <system_error>
#include <thread>
#include <chrono>

#include <iostream>

using namespace NaCs;

namespace Spcm {

NACS_EXPORT() DummyServer::DummyServer(Config conf) :
    m_conf(std::move(conf)),
    m_zmqctx(),
    m_zmqsock(m_zmqctx, ZMQ_ROUTER)
{
    m_zmqsock.bind(m_conf.listen);
    m_serv_id = getTime();
    std::cout << "Server ID: " << m_serv_id << std::endl;
}

    //bool startController();
    //bool controllerRunning() const;

    //bool runSeq(uint64_t client_id, uint64_t seq_id, const uint8_t *data, size_t sz, bool is_seq_sent, uint64_t seqcnt, uint32_t start_trigger_id);
    //bool seqDone(uint64_t) const;

    //bool stop();

inline bool DummyServer::recvMore(zmq::message_t &msg) {
    return ZMQ::recv_more(m_zmqsock, msg);
}

NACS_EXPORT() void DummyServer::run(int fd, const std::function<int(int)> &cb)
{
    uint64_t seqcnt = 0;
    zmq::message_t empty(0);
    
    auto send_reply = [&] (auto &addr, auto &&msg) {
        ZMQ::send_addr(m_zmqsock, addr, empty);
        ZMQ::send(m_zmqsock, msg);
    };

    auto handle_msg = [&] {
        auto addr = ZMQ::recv_addr(m_zmqsock);

        zmq::message_t msg;

        if (!recvMore(msg)) {
            send_reply(addr, ZMQ::bits_msg(false));
            goto out;
        }
        else if (ZMQ::match(msg, "req_client_id")) {
            uint64_t client_id;
            client_id = getTime();
            ZMQ::send_addr(m_zmqsock, addr, empty);
            //ZMQ::send_more(m_zmqsock, ZMQ::bits_msg(m_serv_id));
            ZMQ::send(m_zmqsock, ZMQ::bits_msg(client_id));
        }
        else if (ZMQ::match(msg, "req_server_id")) {
            ZMQ::send_addr(m_zmqsock,addr, empty);
            ZMQ::send(m_zmqsock, ZMQ::bits_msg(m_serv_id));
        }
        else if (ZMQ::match(msg, "req_triple")) {
            // TODO: reply correctly
            ZMQ::send_addr(m_zmqsock, addr, empty);
            ZMQ::send_more(m_zmqsock, ZMQ::str_msg("triple"));
            ZMQ::send_more(m_zmqsock, ZMQ::str_msg("cpu"));
            ZMQ::send(m_zmqsock, ZMQ::str_msg("features"));
        }
        else if (ZMQ::match(msg, "run_seq")) {
            // TODO read out info to check protocol on client side
            uint64_t id = ++seqcnt;
            ZMQ::send_addr(m_zmqsock, addr, empty);
            auto rpy = ZMQ::bits_msg(id);
            ZMQ::send(m_zmqsock, rpy);
        }
        else if (ZMQ::match(msg, "wait_seq")) {
            if (!recvMore(msg) || msg.size() != 8) {
                send_reply(addr, ZMQ::bits_msg(false));
                goto out;
            }
            uint64_t id;
            memcpy(&id, msg.data(), 8);
            if (id == seqcnt) {
                send_reply(addr, ZMQ::bits_msg(true));
                goto out;
            }
            else {
                send_reply(addr, ZMQ::bits_msg(false));
                goto out;
            }
        }
    out:
        ZMQ::readall(m_zmqsock);
    };

    std::vector<zmq::pollitem_t> polls {
        {(void*) m_zmqsock, 0, ZMQ_POLLIN, 0},
    };
    while (true) {
        zmq::poll(polls);
        if (polls[0].revents & ZMQ_POLLIN) {
            // network event
            handle_msg();
        }
    }
}
}
