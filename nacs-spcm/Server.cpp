//

#include "Server.h"

#include <nacs-utils/zmq_utils.h>
#include <nacs-utils/fd_utils.h>
#include <nacs-utils/processor.h>
#include <nacs-utils/timer.h>

#include <llvm/Support/Host.h>

#include <system_error>
#include <thread>
#include <chrono>

using namespace NaCs;

using namespace std::chrono_literals;

namespace Spcm {
struct WaitSeq{
    std::vector<zmq::message_t> addr;
    uint64_t id;
};

NACS_EXPORT() Server::Server(Config conf)
: m_conf(std::move(conf)),
m_zmqctx(),
m_zmqsock(m_zmqctx, ZMQ_ROUTER),
m_evfd(openEvent(0, EFD_NONBLOCK | EFD_CLOEXEC)),
m_ctrl(*this, m_conf, std::vector<uint8_t>{0}),
m_cache(8 * 1024ll * 1024ll * 1024ll) // pretty arbitrary
{
    //m_ctrl = Controller(init_out_chn);
    m_zmqsock.bind(m_conf.listen);
    m_serv_id = getTime(); //uint64_t in nanoseconds
    startController();
}

NACS_EXPORT() bool Server::startController()
{
    std::lock_guard<std::mutex> locker(m_seqlock);
    m_ctrl.startWorker();
    return true;
}

NACS_EXPORT() bool Server::controllerRunning() const
{
    std::lock_guard<std::mutex> locker(m_seqlock);
    return m_ctrl.workerRunning();
}

NACS_EXPORT() bool Server::stop()
{
    if (!m_running)
        return false;
    {
        std::lock_guard<std::mutex> locker(m_seqlock);
        m_running = false;
    }
    m_seqcv.notify_all();
    writeEvent(m_evfd);
    return true;
}

NACS_EXPORT() bool Server::runSeq(uint64_t client_id, uint64_t seq_id, const uint8_t *data, uint32_t &sz, bool is_seq_sent, uint64_t seqcnt, uint32_t start_trigger_id, bool is_first_seq)
{
    SeqCache::Entry* entry;
    if (!m_cache.getAndFill(client_id, seq_id, data, sz, entry, is_seq_sent)) {
        return false;
    }
    {
        //printf("Pushing back QueueItem");
        std::lock_guard<std::mutex> locker(m_seqlock);
        m_seque.push_back(QueueItem{entry, seqcnt, start_trigger_id, is_first_seq});
    }
    // This is a little racy, in principle we need to wait
    // until some data has been pushed to the controller.
    // However, we can't possibly remove the dependency
    // on the CPU code to be fast enough and this particular
    // issue is quite minor so I'm not going to worry about
    // it for now.
    m_seqcv.notify_all();
    return true;
}

NACS_EXPORT() bool Server::seqDone(uint64_t id) const
{
    return m_seqfin.load(std::memory_order_acquire) >= id;
}

NACS_INTERNAL auto Server::popSeq() -> QueueItem
{
    std::unique_lock<std::mutex> locker(m_seqlock);
    QueueItem res = {nullptr, 0, 0};
    m_seqcv.wait(locker, [&] {
        if (!m_running)
            return true;
        if (m_seque.empty())
            return false;
        res = m_seque[0];
        m_seque.erase(m_seque.begin());
        return true;
    });
    return res;
}

NACS_INTERNAL void Server::seqRunner()
{
    // this call to popSeq hangs until a sequence (QueueItem) can be popped off
    while (auto entry = popSeq()) {
        //printf("Controller running sequence\n");
        // do reset now if this is a first sequence.
        if (entry.is_first_seq) {
            m_ctrl.resetStmManagers();
        }
        auto fin_id = m_ctrl.get_end_id();
        auto outChns = m_ctrl.getOutChn();
        for (int i = 0; i < outChns.size(); i++) {
            uint8_t phys_chn_idx = outChns[i];
            std::vector<Cmd> preSend;
            if (entry.start_trigger) {
                preSend.push_back(Cmd::getTriggerStart(0, 0, 0, entry.start_trigger));
            }
            else {
                m_ctrl.set_trig_to_now(0, 195312); // baked in 10 ms delay. Otherwise the trigger_t will be set when the trigger arrives.
            }
            auto &tot_seq = entry.entry->m_seq;
            //if (tot_seq.is_valid) {
            //    printf("Sequence valid\n");
            //}
            //else {
            //    printf("Sequence invalid\n");
            //}
            int64_t seq_len = 0;
            auto &this_seq = tot_seq.getSeq(phys_chn_idx);
            auto cmds = this_seq.toCmds(preSend, seq_len);
            m_ctrl.set_len(entry.start_trigger, (uint64_t) seq_len);
            // MAKE COMMANDS HERE AT RUNTIME, SORT AND SEND.
            auto pPre = &preSend[0];
            auto szPre = preSend.size();
            //printf("preSend size: %u", szPre);
            uint32_t nwrote;
            do {
                nwrote = m_ctrl.copy_cmds(phys_chn_idx, pPre, szPre);
                pPre += nwrote;
                szPre -= nwrote;
            }
            while (szPre> 0 && (nwrote > 0 || controllerRunning()) && m_running);
                auto p = &cmds[0];
                auto sz = cmds.size();
                // printf("cmd size: %u", sz);
            do {
                nwrote = m_ctrl.copy_cmds(phys_chn_idx, p, sz);
                p += nwrote;
                sz -= nwrote;
                //printf("nwrote %u, sz: %u\n", nwrote, sz);
                if (sz > 0)
                    CPU::pause();
            // We just want to avoid looping without reader and without being able to
            // write anything so we don't have to do any check if `nwrote` is not 0.
            // `controllerRunning` can in principle sleep but that can only happen
            // if another thread are doing very slow stuff to the controller so we
            // don't need to worry too much.
            } while (sz > 0 && (nwrote > 0 || controllerRunning()) && m_running);
            if (!m_running)
                return;
            m_cache.unref(*entry.entry);
            m_ctrl.add_cmd(phys_chn_idx, Cmd::getTriggerEnd(0, 0, 0, fin_id));
            m_ctrl.flush_cmd(phys_chn_idx);
            m_ctrl.distribute_cmds(phys_chn_idx);
            /*if (!first_start) {
                m_ctrl.startDataTransfer();
                first_start = true;
                }*/
        }
        uint32_t cur_restarts = m_ctrl.get_restarts();
        while (!m_ctrl.get_end_triggered(entry.start_trigger) && controllerRunning() && m_running){
            cur_restarts = m_ctrl.get_restarts();
            if (cur_restarts != restart_ctr) {
                goto restart;
            }
            std::this_thread::sleep_for(100ms);
        }
        printf("Sequence finished!");
        m_seqfin.store(entry.id, std::memory_order_release);
        writeEvent(m_evfd);
        continue;
    restart:
        printf("Restart interrupted sequence");
        m_seqfin.store(entry.id, std::memory_order_release);
        writeEvent(m_evfd, 2); // 2 indicates an interruption.
    }
}

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

inline bool Server::recvMore(zmq::message_t &msg) {
    return ZMQ::recv_more(m_zmqsock, msg);
}

NACS_EXPORT() void Server::run(int trigger_fd, const std::function<std::pair<uint32_t, int64_t>(int)> &trigger_cb) {
    uint64_t seqcnt = 0;
    m_running = true;
    std::thread worker(&Server::seqRunner, this);
    zmq::message_t empty(0);

    std::vector<WaitSeq> waits;
    std::vector<uint32_t> start_triggers;

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
        else if (ZMQ::match(msg, "set_out_config")) {
            // [nchns: 4B][out_chn_num: 1B x nchns]
            // expected to be sorted.
            if (!recvMore(msg)) {
                send_reply(addr, ZMQ::bits_msg(uint64_t(0)));
                goto out;
            }
            uint32_t n_out;
            memcpy(&n_out, msg.data(), 4);
            std::vector<uint8_t> out_chns;
            for (uint32_t i = 0; i < n_out; i++) {
                uint8_t this_out;
                memcpy(&this_out, msg.data() + 4 + i, 1);
                out_chns.push_back(this_out);
            }
            m_ctrl.setPhysChn(out_chns);
            send_reply(addr, ZMQ::str_msg("ok"));
        }
        else if (ZMQ::match(msg, "req_client_id")) {
            uint64_t client_id;
            client_id = getTime();
            //std::cout << "client id: " << client_id << std::endl;
            ZMQ::send_addr(m_zmqsock, addr, empty);
            //ZMQ::send_more(m_zmqsock, ZMQ::bits_msg(m_serv_id));
            ZMQ::send(m_zmqsock, ZMQ::bits_msg(client_id));
        }
        else if (ZMQ::match(msg, "req_server_id")) {
            ZMQ::send_addr(m_zmqsock,addr, empty);
            ZMQ::send(m_zmqsock, ZMQ::bits_msg(m_serv_id));
        }
        else if (ZMQ::match(msg, "req_restarts")) {
            ZMQ::send_addr(m_zmqsock, addr, empty);
            uint32_t restarts = m_ctrl.get_restarts();
            ZMQ::send(m_zmqsock, ZMQ::bits_msg(restarts));
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
            // WORK OUT INITIALIZATION LOGIC
            if (!controllerRunning())
                startController();
            //if (seqcnt && seqDone(seqcnt))
            //  m_ctrl.reqWait();
            restart_ctr = m_ctrl.get_restarts();
            if (!recvMore(msg) || msg.size() != 4) {
                // No version
                send_reply(addr, ZMQ::bits_msg(uint64_t(0)));
                goto out;
            }
            uint32_t ver;
            memcpy(&ver, msg.data(), 4);
            if (ver != 0) {
                // Wrong version
                send_reply(addr, ZMQ::bits_msg(uint64_t(0)));
                goto out;
            }
            if (!recvMore(msg) || msg.size() != 25) {
                // No code
                send_reply(addr, ZMQ::bits_msg(uint64_t(0)));
                goto out;
            }
            // 8 bytes server_id, 8 bytes client_id, 8 bytes seq_id, 1 byte
            uint64_t this_serv_id, this_client_id, this_seq_id;
            bool is_first_seq;
            memcpy(&this_serv_id, msg.data(), 8);
            memcpy(&this_client_id, msg.data() + 8, 8);
            memcpy(&this_seq_id, msg.data() + 16, 8);
            memcpy(&is_first_seq, msg.data() + 24, 1);
            printf("Seq id %lu \n", this_seq_id);
            auto id = ++seqcnt;
            // check cache before looking at next message.
            bool haveSeq = m_cache.hasSeq(this_client_id, this_seq_id);
            if (!recvMore(msg)) {
                // No code
                send_reply(addr, ZMQ::bits_msg(uint64_t(0)));
                goto out;
            }
            auto msg_data = (const uint8_t*) msg.data();
            uint32_t msg_sz = msg.size();
            // trigger first
            uint32_t start_trigger;
            memcpy(&start_trigger, msg_data, 4);
            msg_data += 4;
            msg_sz -= 4;
            uint32_t start_id = (start_trigger && trigger_fd != -1) ? m_ctrl.get_start_id() : 0;
            //printf("start id: %u\n", start_id);
            // check next bit to see if sequence was sent
            uint8_t data_type;
            //uint8_t *non_const_data = nullptr;
            //uint32_t non_const_data_sz;
            //uint8_t *all_data = nullptr;
            //uint32_t all_data_sz;
            memcpy(&data_type, msg_data, 1);
            msg_data += 1;
            msg_sz -= 1;
            bool seq_sent;
            if (data_type == 0) {
                seq_sent = false;
                // only data sent. assume sequence is here
                if (!haveSeq) {
                    // but I don't have the sequence...request a resend
                    send_reply(addr, ZMQ::str_msg("need_seq"));
                    goto out;
                }
            }
            else if (data_type == 1) {
                seq_sent = true;
            }
            while (m_ctrl.waitPending()) {
                // We hang here until the card is ready for sequences
                if (!m_running) {
                    send_reply(addr, ZMQ::bits_msg(uint64_t(0)));
                    goto out;
                }
                std::this_thread::sleep_for(1ms);
            }
            if (!runSeq(this_client_id, this_seq_id, msg_data, msg_sz, seq_sent, id, start_id, is_first_seq)) {
                // this must mean data is missing
                send_reply(addr, ZMQ::str_msg("need_data"));
                goto out;
            }
            if (start_id)
                start_triggers.push_back(start_id);
            // Do as much as possible before waiting for the wait request to be processed
            ZMQ::send_addr(m_zmqsock, addr, empty);
            auto rpy = ZMQ::bits_msg(id);
            ZMQ::send_more(m_zmqsock, ZMQ::str_msg("ok"));
            ZMQ::send(m_zmqsock, rpy);
        }
        else if (ZMQ::match(msg, "wait_seq")) {
            if (!recvMore(msg) || msg.size() != 8) {
                send_reply(addr, ZMQ::bits_msg(false));
                goto out;
            }
            uint64_t id;
            memcpy(&id, msg.data(), 8);
            if (id > seqcnt || id == 0) {
                send_reply(addr, ZMQ::bits_msg(false));
                goto out;
            }
            if (seqDone(id)) {
                send_reply(addr, ZMQ::bits_msg(true));
                goto out;
            }
            //printf("Pushing back wait\n");
            waits.push_back(WaitSeq{std::move(addr), id});
        }
    out:
        ZMQ::readall(m_zmqsock);
    };

    // The majority of the above were just definitions of two key function used by run. Now, we get hte actual code of Server::run
    // three events to poll: Requests from control computer, events from the other thread about the completion of a sequence (thorugh m_evfd), and triggers
    std::vector<zmq::pollitem_t> polls {
        {(void*) m_zmqsock, 0, ZMQ_POLLIN, 0},
        {nullptr, m_evfd, ZMQ_POLLIN, 0}
    };
    if (trigger_fd != -1) {
        //printf("Pushed back trigger poll");
        polls.push_back(zmq::pollitem_t{nullptr, trigger_fd, ZMQ_POLLIN, 0});
    }
    while (m_running) {
        zmq::poll(polls);
        //auto t = getTime();
        //uint64_t t = 0;
        if (trigger_fd != -1 && polls[2].revents & ZMQ_POLLIN) {
            // trigger event
            //std::cout << "trigger received" << std::endl;
            auto trig_pair = trigger_cb(trigger_fd);
            uint32_t ntrigger = trig_pair.first;
            uint64_t t = trig_pair.second * m_conf.clock_factor / 32; // in units of arduino reads. Convert to time units used by Stream
            //printf("ntriggers: %u, time: %lu, time_for_trigger: %lu", ntrigger, t, t + m_conf.trig_delay);
            if (ntrigger > start_triggers.size())
                ntrigger = start_triggers.size();
            if (ntrigger > 0)
                m_ctrl.set_start_trigger(start_triggers[ntrigger - 1],
                                         t + m_conf.trig_delay); // trig delay is in units of stream times already
            start_triggers.erase(start_triggers.begin(),
                                 start_triggers.begin() + ntrigger);
            //if (!first_start) {
            //m_ctrl.startDataTransfer();
                //}
        }
        if (polls[1].revents & ZMQ_POLLIN) {
            //printf("SEQUENCE FINISHED!");
            // sequence finish event
            uint64_t ev = readEvent(m_evfd);
            for (size_t i = 0; i < waits.size(); i++) {
                auto &wait = waits[i];
                if (!seqDone(wait.id))
                    continue;
                if (ev == 2) {
                    // A restart had occurred.
                    send_reply(wait.addr, ZMQ::bits_msg(false));
                }
                else {
                    send_reply(wait.addr, ZMQ::bits_msg(true));
                }
                waits.erase(waits.begin() + i);
                i--;
            }
        }
        if (polls[0].revents & ZMQ_POLLIN) {
            // network event
            handle_msg();
        }
    }
    for (auto &wait: waits)
        send_reply(wait.addr, ZMQ::bits_msg(false));

    stop();

    worker.join();
}
}
