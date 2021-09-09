// Written by Kenneth Wang, Oct 2020

#ifndef _NACS_SPCM_STREAM_H
#define _NACS_SPCM_STREAM_H

#include <nacs-utils/thread.h>
#include <nacs-utils/mem.h>

#include <complex>
#include <atomic>
#include <thread>
#include <cstring>
#include <ostream>
#include <vector>
#include <condition_variable>

#include <chrono>
#include <iostream>
#include <cmath>

using namespace NaCs;

namespace Spcm {

enum class CmdType : uint8_t
{
    // CmdType is a enumerated class that holds all possible commands.
    // They are represented by a uint8_t
    Meta, // Meta command types are in CmdMeta
    AmpSet,
    AmpFn,
    AmpVecFn,
    FreqSet,
    FreqFn,
    FreqVecFn,
    ModChn, // add or delete channels
    Phase,
    _MAX = Phase // keeps track of how many CmdType options there are
};

enum class CmdMeta : uint32_t
{
    Reset,
    ResetAll,
    TriggerEnd,
    TriggerStart
};

// Note freq, amp, and phase are already integers in the command struct
// amp is normalized to (2^(31) -1) * pi = 6.7465185e9f
// freq is 10 times the actual frequency
// phase_scale is 2 / (625e6 * 10). We take the integer phase and multiply itby
// phase_scale to get the actual phase in units of pi. 625e6 * 10 is the max possible frequency.

constexpr uint64_t t_serv_to_client = 32/(625e6) * 1e12; // converts to client time standard which is in ps.

struct Cmd
{
private:
    static constexpr int op_bits = 4; // number of bits needed to describe the operation
    static constexpr int chn_bits = 32 - op_bits; // number of bits to determine the chn number
    static_assert((int)CmdType::_MAX < (1 << op_bits), ""); // ensure op_bits are enough to describe the number of commands. << is the left shift operator.
public:
    static constexpr uint32_t add_chn = (uint32_t(1) << chn_bits) - 1; // code for adding a channel
    int64_t t; // start time for command
    int64_t t_client; // time for client and that the function pointer takes
    uint32_t id; // id only for sorting purposes
    uint8_t _op:op_bits; // op should only contain op_bits amount of information.
    uint32_t chn:chn_bits;
    double final_val; // final value at end of command.
    double len = 0; // length of pulse
    void(*fnptr)(void) = nullptr; // function pointer
    CmdType op() const
    {
        return (CmdType)_op; // returns integer index of operation
    }
    // Functions below are used to get the command object for the desired operation
    static Cmd getReset(int64_t t = 0, int64_t t_client = 0, uint32_t id = 0)
    {
        return Cmd{t, t_client, id, (uint8_t)CmdType::Meta, (uint32_t)CmdMeta::Reset, 0}; //initializer list notation, initializes in the order of declared variables above.
    }
    static Cmd getResetAll(int64_t t = 0, int64_t t_client = 0, uint32_t id = 0)
    {
        return Cmd{t, t_client, id, (uint8_t)CmdType::Meta, (uint32_t)CmdMeta::ResetAll, 0};
    }
    static Cmd getTriggerEnd(int64_t t = 0, int64_t t_client = 0, uint32_t id = 0, uint32_t trigger_id = 0)
    {
        return Cmd{t, t_client, id, (uint8_t)CmdType::Meta, (uint32_t)CmdMeta::TriggerEnd, trigger_id};
    }
    static Cmd getTriggerStart(int64_t t = 0, int64_t t_client = 0, uint32_t id = 0, uint32_t trigger_id = 0)
    {
        return Cmd{t, t_client, id, (uint8_t)CmdType::Meta, (uint32_t)CmdMeta::TriggerStart, trigger_id};
    }
    static Cmd getAmpSet(int64_t t, int64_t t_client, uint32_t id, uint32_t chn, double amp)
    {
        return Cmd{t, t_client, id, (uint8_t)CmdType::AmpSet, chn, amp};
    }
    static Cmd getFreqSet(int64_t t, int64_t t_client, uint32_t id, uint32_t chn, double freq)
    {
        return Cmd{t, t_client, id, (uint8_t)CmdType::FreqSet, chn, freq};
    }
    static Cmd getPhase(int64_t t, int64_t t_client, uint32_t id, uint32_t chn, double phase)
    {
        return Cmd{t, t_client, id, (uint8_t)CmdType::Phase, chn, phase};
    }
    static Cmd getAddChn(int64_t t, int64_t t_client, uint32_t id = 0)
    {
        return Cmd{t, t_client, id, (uint8_t)CmdType::ModChn, add_chn, 0}; // largest possible chn_number interpretted as adding a channel
    }
    static Cmd getAddChn(int64_t t, int64_t t_client, uint32_t id, uint32_t chn)
    {
        return Cmd{t, t_client, id, (uint8_t)CmdType::ModChn, add_chn, static_cast<int32_t> (chn)}; //overload NOT meant to be used in stream. Meant for usage with real chn ID not chn within a stream
    }
    static Cmd getDelChn(int64_t t, int64_t t_client, uint32_t id, uint32_t chn)
    {
        return Cmd{t, t_client, id, (uint8_t)CmdType::ModChn, chn, 0};
    }
    static Cmd getAmpFn(int64_t t, int64_t t_client, uint32_t id, uint32_t chn, double final_val, double len, void(*fnptr)(void))
    {
        return Cmd{t, t_client, id, (uint8_t)CmdType::AmpFn, chn, final_val, len, fnptr};
    }
    static Cmd getFreqFn(int64_t t, int64_t t_client, uint32_t id, uint32_t chn, double final_val, double len, void(*fnptr)(void))
    {
        return Cmd{t, t_client, id, (uint8_t)CmdType::FreqFn, chn, final_val, len, fnptr};
    }
    static Cmd getAmpVecFn(int64_t t, int64_t t_client, uint32_t id, uint32_t chn, double final_val, double len, void(*fnptr)(void))
    {
        return Cmd{t, t_client, id, (uint8_t)CmdType::AmpFn, chn, final_val, len, fnptr};
    }
    static Cmd getFreqVecFn(int64_t t, int64_t t_client, uint32_t id, uint32_t chn, double final_val, double len, void(*fnptr)(void))
    {
        return Cmd{t, t_client, id, (uint8_t)CmdType::FreqFn, chn, final_val, len, fnptr};
    }
    const char *name() const; // returns name of cmd
    void dump() const;
    inline bool operator==(const Cmd &other) const
    {
        //Checks whether commands are the same or not.
        if (other.t != t)
            return false;
        if (other.op() != op())
            return false;
        switch(op())
        {
        case CmdType::AmpSet:
        case CmdType::FreqSet:
        case CmdType::Phase:
        case CmdType::ModChn:
            if (other.final_val != final_val)
                return false;
            return other.chn == chn;
        case CmdType::Meta:
            if (chn == (uint32_t)CmdMeta::TriggerEnd || chn == (uint32_t)CmdMeta::TriggerStart)
                return other.chn == chn && final_val == other.final_val;
        case CmdType::AmpFn:
        case CmdType::FreqFn:
        case CmdType::AmpVecFn:
        case CmdType::FreqVecFn:
            if ((other.final_val == final_val) && (other.len == len))
                return other.fnptr == fnptr;
        default:
            return false;
        }
    }
};

//static_assert(sizeof(Cmd) == 24, "");

std::ostream &operator<<(std::ostream &stm, const Cmd &cmd);
std::ostream &operator<<(std::ostream &stm, const std::vector<Cmd> &cmds); //printing functions

struct activeCmd {
// structure to keep track of commands that span longer times
    const Cmd* m_cmd;
    //std::vector<float> vals; // precalculated values
    activeCmd(const Cmd* cmd) : m_cmd(cmd) {
        ramp_func = cmd->fnptr;
        if (cmd->op() == CmdType::AmpVecFn || cmd->op() == CmdType::FreqVecFn) {
            is_vec = true;
        }
        /*if (cmd->op() == CmdType::AmpVecFn || cmd->op() == CmdType::FreqVecFn) {
// only precalculate and store if it's vector input. If not calculate in real time.
            printf("In active cmd constructor\n");
            std::vector<int64_t> ts;
            ts.reserve(static_cast<size_t>(std::ceil(cmd->len)));
            for (uint32_t i = 0; i < (cmd->len + 1); i++)
                ts.push_back(i * t_serv_to_client); // convert to t_client
            printf("About to convert function, fnptr at: %p\n", cmd->fnptr);
            vals = ((std::vector<float>(*)(std::vector<int64_t>))(cmd->fnptr))(ts);
            printf("after calculating vals\n");
            }*/
    }
    std::pair<double,double> eval(int64_t t); // called with server t convention
    int64_t time_base = 0; // in server times
    int64_t nsteps = 0;
    double buffer[8] __attribute__((aligned(64)));
    bool is_vec = false;
    void (*ramp_func)(void) = nullptr;
    double times [8] __attribute((aligned(64))) = {0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875};
};

class StreamBase
{
public:
    inline const int16_t *get_output(size_t *sz)
    {
        return m_output.get_read_ptr(sz); // call to obtain values for output
    }
    inline void consume_output(size_t sz)
    {
        return m_output.read_size(sz); // call after finishing using values for output
    }
    inline void sync_reader()
    {
        return m_output.sync_reader();
    }
    //similar commands for the command pipe to come
    inline size_t copy_cmds(const Cmd *cmds, size_t sz)
    {
        if (!probe_cmd_input()) // return 0 if no commands to consume
            return 0;
        sz = std::min(sz, assume(m_cmd_max_write - m_cmd_wrote));
        std::memcpy(&m_cmd_write_ptr[m_cmd_wrote], cmds, sz * sizeof(Cmd));
        m_cmd_wrote += sz;
        if (m_cmd_wrote == m_cmd_max_write) {
            m_commands.wrote_size(m_cmd_max_write);
            m_cmd_wrote = m_cmd_max_write = 0;
        }
        return sz;
    }
    inline bool try_add_cmd(const Cmd &cmd)
    {
        // adds a single command. returns true if successfully added
        return copy_cmds(&cmd, 1) != 0;
    }
    inline void add_cmd(const Cmd &cmd)
    {
        // keeps on trying to add command until successfully added
        while(!try_add_cmd(cmd)){
            CPU::pause();
        }
        std::cout << "added: " << cmd << std::endl;
        //std::cout << "Command Added!" << std::endl;
    }
    inline void flush_cmd()
    {
        // tells command pipe data has been read.
        // if(uint32_t) returns true if uint32_t is nonzero
        if (m_cmd_wrote) {
            m_cmd_max_write -= m_cmd_wrote;
            m_cmd_write_ptr += m_cmd_wrote; // advances pointer
            m_commands.wrote_size(m_cmd_wrote);
            m_cmd_wrote = 0;
        }
    }
    // RELATED TO TRIGGER. MIGHT NOT BE NEEDED
    //inline uint32_t get_end_id()
    //{
    //    return ++m_end_trigger_cnt;
    //}
    //inline uint32_t get_start_id()
    //{
    //    return ++m_start_trigger_cnt;
    //}
    inline bool slow_mode() const
    {
        return m_slow_mode.load(std::memory_order_relaxed);
    }
    uint32_t end_triggered() const
    {
        return m_end_triggered.load(std::memory_order_relaxed);
    }

    void set_time_offset(int64_t offset)
    {
        m_time_offset.store(offset, std::memory_order_relaxed);
    }
    int64_t time_offset()
    {
        return m_time_offset.load(std::memory_order_relaxed);
    }
    void set_start_trigger(uint32_t v, uint64_t t)
    {
        m_start_trigger_time.store(t, std::memory_order_relaxed);
        m_start_trigger.store(v, std::memory_order_release);
    }
    void set_end_trigger(int16_t *p)
    {
        m_end_trigger.store(p, std::memory_order_relaxed);
    }
    int16_t *end_trigger() const
    {
        return m_end_trigger.load(std::memory_order_relaxed);
    }
    uint32_t get_cur_t()
    {
        return m_cur_t;
    }
    uint32_t get_chns()
    {
        return m_chns;
    }

protected:
    struct State {
        // structure which keeps track of the state of a channel
        int64_t phase; // phase_cnt = (0 to 1 phase) * 625e6 * 10
        uint64_t freq; // freq_cnt = real freq * 10
        double amp; // real amp * 6.7465185e9f
    };
    void generate_page(State *states); //workhorse, takes a vector of states for the channels
    void step(int16_t *out, State *states); // workhorse function to step to next time
    const Cmd *get_cmd();
    StreamBase(double step_t, std::atomic<uint64_t> &cmd_underflow, std::atomic<uint64_t> &underflow, uint32_t stream_num) :
        m_step_t(step_t),
        m_cmd_underflow(cmd_underflow),
        m_underflow(underflow),
        m_commands((Cmd*)mapAnonPage(sizeof(Cmd) * 1024ll, Prot::RW), 1024, 1),
        m_output((int16_t*)mapAnonPage(1 * 1024ll * 1024ll, Prot::RW), 1024ll * 1024ll / 2, 1024ll * 1024ll / 2),
        m_stream_num(stream_num)
    {
    }
private:
    inline bool probe_cmd_input()
    {
        // returns true if there are still commands to read, and determines number of cmds ready
        // returns false if no commands left
        if (m_cmd_wrote == m_cmd_max_write){
            m_cmd_wrote = 0;
            m_cmd_write_ptr = m_commands.get_write_ptr(&m_cmd_max_write);
            if (!m_cmd_max_write) {
                return false; // return false if no commands
            }
        }
        //std::cout << "m_cmd_max_write:" << m_cmd_max_write << std::endl;
        return true;
    }
    const Cmd *get_cmd_curt();
    void cmd_next();
    const Cmd *consume_old_cmds(State * states);
    bool check_start(int64_t t, uint32_t id);
    void clear_underflow();
    constexpr static uint32_t output_block_sz = 32768; //2048; // units of int16_t. 32 of these per _m512
    // Members accessed by worker threads
protected:
    std::atomic_bool m_stop{false};
private:
    uint32_t m_stream_num;
    std::atomic_bool m_slow_mode{true}; // related to trigger
    uint32_t m_end_trigger_pending{0};
    uint32_t m_end_trigger_waiting{0};
    uint32_t m_chns = 0;
    int64_t m_cur_t = 0;
    uint64_t m_output_cnt = 0; // in unit of 8 bytes, or 32 samples (each sample 2 bits)
    const double m_step_t;
    const Cmd *m_cmd_read_ptr = nullptr;
    size_t m_cmd_read = 0;
    size_t m_cmd_max_read = 0;
    std::atomic<uint64_t> &m_cmd_underflow;
    std::atomic<uint64_t> &m_underflow;
    // Members accessed by the command generation thread
    Cmd *m_cmd_write_ptr __attribute__ ((aligned(64))) = nullptr; //location to write commands to
    size_t m_cmd_wrote = 0;
    size_t m_cmd_max_write = 0;
    //uint32_t m_end_trigger_cnt{0};
    //uint32_t m_start_trigger_cnt{0};

    DataPipe<Cmd> m_commands;
    DataPipe<int16_t> m_output;
    std::vector<activeCmd*> active_cmds;
    std::atomic<uint32_t> m_end_triggered{0};
    std::atomic<int64_t> m_time_offset{0};
    // Read by all threads most of the time and
    // may be written by both worker and control threads
    // No ordering is needed on this.
    std::atomic<int16_t*> m_end_trigger{nullptr};
    std::atomic<uint32_t> m_start_trigger{0};
    std::atomic<uint64_t> m_start_trigger_time{0};
};

template<uint32_t max_chns = 128>
struct Stream : StreamBase {
    Stream(double step_t, std::atomic<uint64_t> &cmd_underflow,
           std::atomic<uint64_t> &underflow, uint32_t stream_num, bool start=true)
        : StreamBase(step_t, cmd_underflow, underflow, stream_num)
    {
        if (start) {
            start_worker();
        }
    }

    void start_worker()
    {
        m_stop.store(false, std::memory_order_relaxed);
        m_worker = std::thread(&Stream::thread_fun, this);
    }
    void stop_worker()
    {
        m_stop.store(true, std::memory_order_relaxed);
        if (m_worker.joinable()){
            m_worker.join();
        }
    }
    ~Stream()
    {
        stop_worker();
    }

private:
    void thread_fun()
    {
        /*while (likely(!m_stop.load(std::memory_order_relaxed))) {
            generate_page(m_states);
            }*/
        // int outputs [4] = {0, 0, 0, 0};
        /* while(get_cur_t() < 20) {
            std::cout << "m_cur_t=" << get_cur_t() << std::endl;
            step(&(outputs[0]), m_states);
            //generate_page(m_states);
            //get_cmd();
            std::cout << "amp: ( " << outputs[0] << ", " << outputs[1] << ")" << std::endl;
            std::cout << "freq: ( " << outputs[2] << ", " << outputs[3] << ")" << std::endl;
            //std::cout << get_cmd() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            } */
        while(likely(!m_stop.load(std::memory_order_relaxed))) {
            generate_page(m_states);
            //std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
    State m_states[max_chns]{}; // array of states
    std::thread m_worker{};
};

}

#endif
