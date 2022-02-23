#ifndef _NACS_SPCM_FSTREAM_H
#define _NACS_SPCM_FSTREAM_H

#include <nacs-spcm/FileCache.h>
#include <nacs-spcm/Stream.h>

using namespace NaCs;

namespace Spcm {

struct FCmd : Cmd {
public:
    std::string fname;

    static FCmd getFReset(int64_t t = 0, int64_t t_client = 0, uint32_t id = 0)
    {
        return FCmd{t, t_client, id, (uint8_t)CmdType::Meta, (uint32_t)CmdMeta::Reset, 0}; //initializer list notation, initializes in the order of declared variables above.
    }
    static FCmd getFResetAll(int64_t t = 0, int64_t t_client = 0, uint32_t id = 0)
    {
        return FCmd{t, t_client, id, (uint8_t)CmdType::Meta, (uint32_t)CmdMeta::ResetAll, 0};
    }
    static FCmd getFTriggerEnd(int64_t t = 0, int64_t t_client = 0, uint32_t id = 0, uint32_t trigger_id = 0)
    {
        return FCmd{t, t_client, id, (uint8_t)CmdType::Meta, (uint32_t)CmdMeta::TriggerEnd, trigger_id};
    }
    static FCmd getFTriggerStart(int64_t t = 0, int64_t t_client = 0, uint32_t id = 0, uint32_t trigger_id = 0)
    {
        return FCmd{t, t_client, id, (uint8_t)CmdType::Meta, (uint32_t)CmdMeta::TriggerStart, trigger_id};
    }
    static FCmd getFAddChn(int64_t t, int64_t t_client, uint32_t id = 0)
    {
        return FCmd{t, t_client, id, (uint8_t)CmdType::ModChn, add_chn, 0}; // largest possible chn_number interpretted as adding a channel
    }
    static FCmd getFAddChn(int64_t t, int64_t t_client, uint32_t id, uint32_t chn)
    {
        return FCmd{t, t_client, id, (uint8_t)CmdType::ModChn, add_chn, static_cast<int32_t> (chn)}; //overload NOT meant to be used in stream. Meant for usage with real chn ID not chn within a stream
    }
    static FCmd getFDelChn(int64_t t, int64_t t_client, uint32_t id, uint32_t chn)
    {
        return FCmd{t, t_client, id, (uint8_t)CmdType::ModChn, chn, 0};
    }
    static FCmd getFAmpSet(int64_t t, int64_t t_client, uint32_t id, uint32_t chn, double amp, std::string fname)
    {
        return FCmd{t, t_client, id, (uint8_t)CmdType::AmpSet, chn, amp, 0, nullptr, fname};
    }
    static FCmd getFAmpFn(int64_t t, int64_t t_client, uint32_t id, uint32_t chn, double final_val, double len, void(*fnptr)(void), std::string fname)
    {
        return FCmd{t, t_client, id, (uint8_t)CmdType::AmpFn, chn, final_val, len, fnptr, fname};
    }

    inline bool operator==(const FCmd &other) const
    {
        return (static_cast<const Cmd&>(*this) == static_cast<const Cmd&>(other)) && (fname.compare(other.fname) == 0);
    }
};

class FileStreamManager;

struct FStream : StreamBase {
    FStream(FileStreamManager &mngr, FileCache &fcache, Config &conf, std::atomic<uint64_t> &cmd_underflow, std::atomic<uint64_t> &underflow, uint32_t stream_num, bool start = true) :
        m_stm_mngr(mngr),
        m_fcache(fcache),
        t_serv_to_client(uint64_t(32 * 1e12 / (conf.sample_rate))),
        StreamBase(conf, cmd_underflow, underflow, stream_num)
    {
        if (start) {
            start_worker();
        }
    }

    void start_worker(bool flag = false)
    {
        m_stop.store(false, std::memory_order_relaxed);
        m_worker = std::thread(&FStream::thread_fun, this);
    }

    void stop_worker()
    {
        m_stop.store(true, std::memory_order_relaxed);
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }

    void reset_out()
    {
        if (m_worker.joinable()) {
            stop_worker();
        }
        reset_output();
    }

    ~FStream()
    {
        stop_worker();
    }
    uint32_t get_chns()
    {
        return m_chns;
    }

    struct State {
        double amp;
        std::string fname;
    };

    const FCmd *consume_old_cmds();
    void generate_page();
    void step(int16_t *out);

    inline void reqRestart(uint32_t id){

    }
    bool check_start(int64_t t, uint32_t id);

private:
    void thread_fun()
    {
        while(likely(!m_stop.load(std::memory_order_relaxed))) {
            generate_page();
        }
    }
    uint64_t t_serv_to_client;
    uint32_t m_chns = 0;
    State m_states [128];
    FileCache &m_fcache;
    FileStreamManager & m_stm_mngr;

    std::thread m_worker{};
};


}

#endif
