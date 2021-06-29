//

#include "DummyServer.h"

#include <nacs-utils/zmq_utils.h>
#include <nacs-utils/fd_utils.h>
#include <nacs-utils/processor.h>
#include <nacs-utils/timer.h>

#include <llvm/Support/Host.h>

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
    std::cout << m_conf.listen << std::endl;
    m_zmqsock.bind(m_conf.listen);
    m_serv_id = getTime();
    std::cout << "Server ID: " << m_serv_id << std::endl;
}

    //bool startController();
    //bool controllerRunning() const;

    //bool runSeq(uint64_t client_id, uint64_t seq_id, const uint8_t *data, size_t sz, bool is_seq_sent, uint64_t seqcnt, uint32_t start_trigger_id);
    //bool seqDone(uint64_t) const;

    //bool stop();

inline std::string join_feature_strs(const std::vector<std::string> &strs)
{
    size_t nstr = strs.size();
    if (!nstr)
        return std::string("");
    std::string str = strs[0];
    for (size_t i = 1; i < nstr; i++)
        str += ',' + strs[i];
    return str;
}

inline bool DummyServer::recvMore(zmq::message_t &msg) {
    return ZMQ::recv_more(m_zmqsock, msg);
}

NACS_EXPORT() void DummyServer::run(int trigger_fd, const std::function<int(int)> &trigger_cb)
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
            std::cout << "client id: " << client_id << std::endl;
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
            auto triple = llvm::sys::getProcessTriple();
            ZMQ::send_more(m_zmqsock, ZMQ::str_msg(triple.data()));
            auto &host_info = CPUInfo::get_host();
            auto res = host_info.get_llvm_target(LLVM_VERSION_MAJOR);
            ZMQ::send_more(m_zmqsock, ZMQ::str_msg(res.first.data()));
            // make feature string from vector
            auto features = res.second;
            auto feature_str = join_feature_strs(features);
            ZMQ::send(m_zmqsock, ZMQ::str_msg(feature_str.data()));
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
    if (trigger_fd != -1)
        polls.push_back(zmq::pollitem_t{nullptr, trigger_fd, ZMQ_POLLIN, 0});
    while (true) {
        zmq::poll(polls);
        if (trigger_fd != -1 && polls[1].revents && ZMQ_POLLIN) {
            trigger_counter += trigger_cb(trigger_fd);
            printf("Trigger Count Now: %u\n", trigger_counter);
        }
        if (polls[0].revents & ZMQ_POLLIN) {
            // network event
            handle_msg();
        }
    }
}

NACS_EXPORT() void DummyServer::run()
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
            std::cout << "client id: " << client_id << std::endl;
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
            auto triple = llvm::sys::getProcessTriple();
            ZMQ::send_more(m_zmqsock, ZMQ::str_msg(triple.data()));
            auto &host_info = CPUInfo::get_host();
            auto res = host_info.get_llvm_target(LLVM_VERSION_MAJOR);
            ZMQ::send_more(m_zmqsock, ZMQ::str_msg(res.first.data()));
            // make feature string from vector
            auto features = res.second;
            auto feature_str = join_feature_strs(features);
            ZMQ::send(m_zmqsock, ZMQ::str_msg(feature_str.data()));
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
