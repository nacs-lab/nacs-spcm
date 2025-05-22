// Written by Kenneth Wang, Oct 2020

#ifndef _NACS_SPCM_STREAM_H
#define _NACS_SPCM_STREAM_H

#include <nacs-utils/thread.h>
#include <nacs-utils/mem.h>
#include "Config.h"

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

#include <stdint.h>

using namespace NaCs;

namespace Spcm {

// STREAM_MAX_CHN = 128;

class StreamManagerBase;

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

//constexpr uint64_t t_serv_to_client = 32/(625e6) * 1e12; // converts to client time standard which is in ps.

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
    activeCmd(const Cmd* cmd, double t) :
        m_cmd(cmd),
        t_serv_to_client(double(t))
    {
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
    double t_serv_to_client = 1;
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
        //std::cout << "added: " << cmd << std::endl;
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
    inline bool is_wait_for_seq() const
    {
        return wait_for_seq.load(std::memory_order_relaxed);
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
    void consume_all_cmds();
    void reset_output_cnt() {
        wait_for_seq.store(true, std::memory_order_relaxed);
        m_output_cnt = 0;
    }
    inline void reqRestart(uint32_t id);
    inline void reset_output() {
        size_t sz;
        //uint32_t i = 0;
        //const int16_t* ptr;
        //const int16_t* ptr2;
        //m_output.sync_reader();
        //m_output.get_read_ptr(&sz);
        //m_output.read_size(sz); // reset my own output.
        do {
            //i++;
            m_output.get_read_ptr(&sz);
            //ptr2 = m_output.get_write_ptr(&sz2);
            m_output.read_size(sz); // reset my own output.
        } while (sz != 0);
    }
protected:
    const Cmd *get_cmd();
    StreamBase(StreamManagerBase &stm_mngr, Config &conf, double step_t, double amp_scale, std::atomic<uint64_t> &cmd_underflow, std::atomic<uint64_t> &underflow, uint32_t stream_num) :
        m_stm_mngr(stm_mngr),
        m_conf(conf),
        m_step_t(step_t),
        amp_scale(amp_scale),
        m_cmd_underflow(cmd_underflow),
        m_underflow(underflow),
        m_commands((Cmd*)mapAnonPage(sizeof(Cmd) * 1024ll, Prot::RW), 1024, 512),
        m_output((int16_t*)mapAnonPage(output_buf_sz, Prot::RW), output_buf_sz / 2, output_buf_sz / 2),
        m_stream_num(stream_num)
    {
    }
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
    bool check_start(int64_t t, uint32_t id);
    void clear_underflow();
    constexpr static uint32_t output_block_sz = 2048 * 16;//32768; //2048; // units of int16_t. 32 of these per _m512
    // Members accessed by worker threads
    std::atomic_bool m_stop{false};
    DataPipe<Cmd> m_commands;
    DataPipe<int16_t> m_output;
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
    Config &m_conf;
    // Members accessed by the command generation thread
    Cmd *m_cmd_write_ptr __attribute__ ((aligned(64))) = nullptr; //location to write commands to
    size_t m_cmd_wrote = 0;
    size_t m_cmd_max_write = 0;
    //uint32_t m_end_trigger_cnt{0};
    //uint32_t m_start_trigger_cnt{0};

    uint64_t output_buf_sz = 256 * 1024ll * 1024ll; // extra space to use for filling up a known sequence
    uint64_t wait_buf_sz = 32 * 1024ll * 1024ll; // buffer size during waiting periods, not during a sequence
    double amp_scale = 6.7465185e9f / 8; // Divide by 8 for safety by default
    std::atomic<bool> wait_for_seq = true; // boolean to indicate whether we are waiting for a sequence
    std::vector<activeCmd*> active_cmds;
    std::atomic<uint32_t> m_end_triggered{0};
    std::atomic<int64_t> m_time_offset{0};
    // Read by all threads most of the time and
    // may be written by both worker and control threads
    // No ordering is needed on this.
    std::atomic<int16_t*> m_end_trigger{nullptr};
    std::atomic<uint32_t> m_start_trigger{0};
    std::atomic<uint64_t> m_start_trigger_time{0};

    StreamManagerBase &m_stm_mngr;
};

template<uint32_t max_chns = 128>
struct Stream : StreamBase {
    Stream(StreamManagerBase& stm_mngr, Config &conf, double step_t, double amp_scale, std::atomic<uint64_t> &cmd_underflow,
           std::atomic<uint64_t> &underflow, uint32_t stream_num, bool start=true)
        : StreamBase(stm_mngr, conf, step_t, amp_scale, cmd_underflow, underflow, stream_num),
            max_phase(uint64_t(conf.sample_rate * 10)),
            phase_scale(2/double(max_phase)),
            phase_scale_client(conf.sample_rate * 10),
            freq_scale(0.1/(conf.sample_rate/32)),
            m_t_serv_to_client(32.0f/conf.sample_rate * 1e12)
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
    void reset_out()
    {
        printf("reset out stream called\n");
        if (m_worker.joinable()) {
            stop_worker();
        }
        reset_output();
    }
    ~Stream()
    {
        stop_worker();
    }
protected:
    struct State {
        // structure which keeps track of the state of a channel
        int64_t phase; // phase_cnt = (0 to 1 phase) * 625e6 * 10
        uint64_t freq; // freq_cnt = real freq * 10
        double amp; // real amp * 6.7465185e9f
    };
    void generate_page(State *states)  //workhorse, takes a vector of states for the channels
    {
        //printf("generate page\n");
        int16_t *out_ptr;
        while (true) {
            size_t sz_to_write;
            out_ptr = m_output.get_write_ptr(&sz_to_write);
            if (sz_to_write >= output_block_sz) {
                // If we are not waiting for a sequence, i.e. we are processing a sequence, or we are waiting
                // and the reader is less than wait_buf_sz bytes behind, we break out and generate data
                if (!wait_for_seq.load(std::memory_order_relaxed)) {
                    break;
                }
                else{
                    uint64_t diff = m_output_cnt - m_stm_mngr.getControllerOutputCnt();
                    //if (diff < 0)
                        //std::cout << "diff less than 0" << std::endl;
                    if (diff < (wait_buf_sz / (2 * 32) - output_block_sz / 32) && diff >= 0){
                        break;
                    }
                }
                //std::cout << "Throttling" << std::endl;
            }
            if (sz_to_write > 0) {
                m_output.sync_writer();
            }
            CPU::pause();
            if (unlikely(m_stop.load(std::memory_order_relaxed))){
                return;
            }
        }
        //printf("Stream ready\n");
        //std::cout << "ready to write" << std::endl;
        // Now ready to write to output. Write in output_block_sz chunks
        for (uint32_t i = 0; i < output_block_sz; i += 32) {
            // for now advance one position at a time.
            m_output_cnt += 1;
            step(&out_ptr[i], states);
            //std::cout << "stream stepped" << std::endl;
        }
        //std::cout << "Stream" << m_stream_num << " wrote " << *out_ptr << std::endl;
        m_output.wrote_size(output_block_sz); // alert reader that data is ready.
    }
private:
    uint64_t max_phase;
    double phase_scale;
    double phase_scale_client;
    double freq_scale;
    double m_t_serv_to_client;

    NACS_INTERNAL NACS_NOINLINE
    const Cmd *consume_old_cmds(State * states) 
    {
        // consumes old commands (updates the states) and returns a pointer to a currently active command.
        // If only commmands in future or no commands, then return nullptr
        auto cmd = get_cmd();
        //std::cout << "consume_old_cmds called" << std::endl;
        if (cmd->t != 0)
            m_cmd_underflow.fetch_add(1, std::memory_order_relaxed);
        do {
            if (cmd->t == m_cur_t)
                return cmd;
            if (cmd->t > m_cur_t)
                return nullptr; // get_cmd returns something in the future
            //std::cout << "consume old cmds: " << (*cmd) << std::endl;
            switch (cmd->op()){
            case CmdType::Meta:
                if (cmd->chn == (uint32_t)CmdMeta::Reset) {
                    m_cur_t = 0; // set time to 0 if consuming a Reset
                }
                else if (cmd->chn == (uint32_t)CmdMeta::ResetAll){
                    clear_underflow();
                    m_cur_t = 0;
                    m_chns = 0;
                    m_slow_mode.store(false,std::memory_order_relaxed);
                }
                else if (cmd-> chn == (uint32_t)CmdMeta::TriggerEnd) {
                    //printf("Process trigger end in consume_old_cmds\n");
                    wait_for_seq.store(true,std::memory_order_relaxed);
                    m_end_trigger_pending = cmd->final_val;
                }
                else if (cmd-> chn == (uint32_t)CmdMeta::TriggerStart) {
                    if (!check_start(cmd->t, cmd->final_val)) {
                        return nullptr;
                    }
                    wait_for_seq.store(false,std::memory_order_relaxed);
                }
                break;
            case CmdType::AmpSet:
                states[cmd->chn].amp = cmd->final_val * amp_scale; // set amplitude of state
                break;
            case CmdType::FreqSet:
                states[cmd->chn].freq = cmd->final_val * freq_scale_client;
                break;
            case CmdType::AmpFn:
            case CmdType::AmpVecFn:
                // cmd pointer only increments. Should be safe to initialize an active command here
                if (cmd->t + cmd->len > m_cur_t) {
                    // command still active
                    active_cmds.push_back(new activeCmd(cmd, m_t_serv_to_client));
                    std::pair<double, double> these_vals;
                    these_vals = active_cmds.back()->eval(m_cur_t - cmd->t);
                    states[cmd->chn].amp = (these_vals.first + these_vals.second) * amp_scale;
                }
                else {
                    states[cmd->chn].amp = cmd->final_val * amp_scale; // otherwise set to final value.
                }
                break;
            case CmdType::FreqFn:
            case CmdType::FreqVecFn:
                if (cmd->t + cmd->len > m_cur_t) {
                    // command still active
                    active_cmds.push_back(new activeCmd(cmd, m_t_serv_to_client));
                    std::pair<double, double> these_vals;
                    these_vals = active_cmds.back()->eval(m_cur_t - cmd->t);
                    states[cmd->chn].freq = uint64_t(these_vals.first + these_vals.second) * freq_scale_client;
                }
                else {
                    states[cmd->chn].freq = cmd->final_val * freq_scale_client; // otherwise set to final value.
                }
                break;
            case CmdType::Phase:
                states[cmd->chn].phase = cmd->final_val * phase_scale_client; // possibly a scale factor needed. COME BACK
                break;
            case CmdType::ModChn:
                if (cmd->chn == Cmd::add_chn) {
                    //printf("Process add_chn\n");
                    states[m_chns] = {0, 0, 0.0f}; // initialize new channel
                    m_chns++;
                }
                else {
                    m_chns--;
                    states[cmd->chn] = states[m_chns]; // move last_chn to place of deleted channel
                }
                break;
            }
            cmd_next(); //after interpretting this command, increment pointer to next one.
        } while((cmd = get_cmd())); // keep on going until there are no more commands or one reaches the present
        return nullptr;
    }

    __attribute__((target("avx512f,avx512bw"), flatten))
    void step(int16_t *out, State *states) // workhorse function to step to next time
    {
            // Key function
    const Cmd *cmd;
    retry:
        // returns command at current time or before
        if ((cmd = get_cmd_curt())){
            if (unlikely(cmd->t < m_cur_t)) {
                cmd = consume_old_cmds(states); //consume past commands
                if (!cmd) {
                    goto cmd_out; //if no command available, go to cmd_out
                }
            }
            if (unlikely(cmd->t > m_cur_t)) {
                cmd = nullptr; // don't deal with future commands
            }
            // deal with different types of commands
            else if (unlikely(cmd->op() == CmdType::Meta)) {
                if (cmd->chn == (uint32_t)CmdMeta::Reset) {
                    m_cur_t = 0;
                }
                else if (cmd->chn == (uint32_t)CmdMeta::ResetAll) {
                    clear_underflow();
                    m_cur_t = 0;
                    m_chns = 0;
                    m_slow_mode.store(false, std::memory_order_relaxed);
                }
                else if (cmd->chn == (uint32_t)CmdMeta::TriggerEnd) {
                    //printf("Process trigger end\n");
                    m_end_trigger_pending = cmd->final_val;
                    wait_for_seq.store(true, std::memory_order_relaxed);
                }
                else if (cmd->chn == (uint32_t)CmdMeta::TriggerStart) {
                    if (!check_start(cmd->t, cmd->final_val)){
                        cmd = nullptr;
                        goto cmd_out;
                    }
                    wait_for_seq.store(false, std::memory_order_relaxed);
                }
                cmd_next();
                goto retry; // keep on going if it's a meta command
            }
            else {
                while (unlikely(cmd->op() == CmdType::ModChn)) {
                    if (cmd->chn == Cmd::add_chn) {
                        //printf("Process add chn\n");
                        states[m_chns] = {0, 0, 0.0f};
                        m_chns++;
                    }
                    else {
                        m_chns--;
                        states[cmd->chn] = states[m_chns];
                    }
                    cmd_next();
                    cmd = get_cmd_curt();
                    if (!cmd) {
                        break;
                    } // keep on getting more commands until you're done adding channels.
                    // What if you get a meta command here....
                }
            }
        }
    cmd_out:
        // At this point we have a nullptr if out of commands or in the future, or it's an actual command
        // related to amp, phase, freq
        if (unlikely(m_end_trigger_waiting)) {
            auto cur_end_trigger = end_trigger();
            if (cur_end_trigger) {
                m_end_triggered.store(m_end_trigger_waiting, std::memory_order_relaxed);
                m_end_trigger_waiting = m_end_trigger_pending;
                if (m_end_trigger_pending) {
                    set_end_trigger(out); // out
                }
            }
        }
        else if (unlikely(m_end_trigger_pending)) {
            m_end_trigger_waiting = m_end_trigger_pending;
            m_end_trigger_pending = 0;
            set_end_trigger(out); // out
        }
        // calculate actual output.
        __m512 v1 = _mm512_set1_ps(0.0f);
        __m512 v2 = _mm512_set1_ps(0.0f);
        uint32_t _nchns = m_chns;
        for (uint32_t i = 0; i < _nchns; i++){
            // iterate through the number of channels
            auto &state = states[i];
            int64_t phase = state.phase;
            double amp = state.amp;
            uint64_t freq = state.freq;
            int64_t df = 0;
            double damp = 0;
            // check active commands
            auto it = active_cmds.begin();
            while(it != active_cmds.end()) {
                const Cmd* this_cmd = (*it)->m_cmd;
                if (this_cmd->chn == i) {
                    if (this_cmd->op() == CmdType::AmpFn || this_cmd->op() == CmdType::AmpVecFn) {
                        if (this_cmd->t + this_cmd->len > m_cur_t) {
                            std::pair<double, double> these_vals;
                            these_vals = (*it)->eval(m_cur_t - this_cmd->t);
                            amp = these_vals.first * amp_scale;
                            damp = these_vals.second * amp_scale;
                            state.amp = amp + damp;
                        }
                        else {
                            amp = this_cmd->final_val * amp_scale;
                            state.amp = amp;
                            it = active_cmds.erase(it); // no longer active
                            continue;
                        }
                    }
                    else if (this_cmd->op() == CmdType::FreqFn || this_cmd->op() == CmdType::FreqVecFn) {
                        if (this_cmd->t + this_cmd->len > m_cur_t) {
                            std::pair<double, double> these_vals;
                            these_vals = (*it)->eval(m_cur_t - this_cmd->t);
                            freq = uint64_t(these_vals.first) * freq_scale_client;
                            df = int64_t(these_vals.second) * freq_scale_client;
                            state.freq = (uint64_t)((int64_t) freq + df);
                        }
                        else {
                            freq = this_cmd->final_val * freq_scale_client;
                            state.freq = freq;
                            it = active_cmds.erase(it); // no longer active
                            continue;
                        }
                    }
                }
                ++it;
            }
            // now deal with current command
            if (!cmd || cmd->chn != i) {
                // Prevent amp from exceeding amp_scale at the lowest level possible
                if (amp > amp_scale) {
                    amp = amp_scale;
                }
                if (amp + damp > amp_scale) {
                    damp = 0;
                }
                compute_single_chn(v1, v2, float(phase * phase_scale), float(freq * freq_scale), float(df * freq_scale), amp, damp);
                /*if (freq != 0) {
                    std::cout << "int64_t " <<typeid(int64_t(2)).name() << std::endl;
                    std::cout << "uint64_t: " << typeid(uint64_t(2)).name() << std::endl;
                std::cout << "phase type: " << typeid(phase).name() << std::endl;
                std::cout << "freq type: " << typeid(freq).name() << std::endl;
                std::cout << "df type: " << typeid(df).name() << std::endl;
                std::cout << "state phase: " <<typeid(state.phase).name() << std::endl;
                }*/
                phase = phase + (int64_t) (freq * 32) + df * 32 / 2;
                //if (freq != 0) {
                //std::cout << "phase type after: " << typeid(phase).name() << std::endl;
                //}
                //phase = phase + freq * 2;
                //compute_single_chn(out1freq, out2freq, freq, df);
            }
            else {
                bool ampSet = false; // Behavior for now... if ampSet command is at this time, ignore all other things such as active ramps
                __m512 ampv1, ampv2;
                uint16_t amp_mask1, amp_mask2;
                do {
                    //std::cout << (*cmd) << std::endl;
                    if (cmd->op() == CmdType::FreqSet){
                        //std::cout << "in freq set" << std::endl;
                        freq = cmd->final_val * freq_scale_client;
                    }
                    else if (cmd->op() == CmdType::FreqFn || cmd->op() == CmdType::FreqVecFn) {
                        // first time seeing function command
                        if (cmd->t + cmd->len > m_cur_t) {
                            // command still active
                            active_cmds.push_back(new activeCmd(cmd, m_t_serv_to_client));
                            std::pair<float, float> these_vals;
                            these_vals = active_cmds.back()->eval(m_cur_t - cmd->t);
                            freq = uint64_t(these_vals.first) * freq_scale_client;
                            df = int64_t(these_vals.second) * freq_scale_client;
                        }
                        else {
                            freq = cmd->final_val * freq_scale_client; // otherwise set to final value.
                        }
                    }
                    else if (cmd->op() == CmdType::AmpSet) {
                        if (!ampSet) {
                            ampSet = true;
                            //printf("amp_init: %f\n", amp);
                            ampv1 = _mm512_set1_ps(amp);
                            ampv2 = _mm512_set1_ps(amp);
                            /*float ampv1p[16];
                        float ampv2p[16];
                        memcpy(ampv1p, &ampv1, sizeof(ampv1p));
                        memcpy(ampv2p, &ampv2, sizeof(ampv2p));
                        printf("Amp v1: %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f \n", ampv1p[0],
                               ampv1p[1],ampv1p[2],ampv1p[3],ampv1p[4], ampv1p[5],ampv1p[6],ampv1p[7],
                               ampv1p[8],ampv1p[9],ampv1p[10],ampv1p[11],ampv1p[12],ampv1p[13],ampv1p[14],ampv1p[15]
                            );
                        printf("Amp v2: %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f \n", ampv2p[0],
                               ampv2p[1],ampv2p[2],ampv2p[3],ampv2p[4], ampv2p[5],ampv2p[6],ampv2p[7],
                               ampv2p[8],ampv2p[9],ampv2p[10],ampv2p[11],ampv2p[12],ampv2p[13],ampv2p[14],ampv2p[15]
                               );*/
                        }
                        int shift = int(cmd->len);
                        if (shift < 16) {
                            //printf("shift: %d\n", shift);
                            //amp_mask1 = _mm512_int2mask(UINT16_MAX >> shift); // len determines where the pulse ought to start.
                            //amp_mask2 = _mm512_int2mask(UINT16_MAX);
                            amp_mask1 = UINT16_MAX << shift;
                            amp_mask2 = UINT16_MAX;
                        }
                        else {
                            //printf("shift: %d\n", shift);
                            shift = shift - 16;
                            amp_mask1 = 0;
                            amp_mask2 = UINT16_MAX << shift;
                            //amp_mask1 = _mm512_int2mask(0);
                            //amp_mask2 = _mm512_int2mask(UINT16_MAX >> shift);
                        }
                        //printf("amp_mask1: %x\n", amp_mask1);
                        //printf("amp_mask2: %x\n", amp_mask2);
                        amp = cmd->final_val * amp_scale; // This is just for updating the state
                        if (amp > amp_scale) {
                            amp = amp_scale;
                        }
                        const float constamp = amp;
                        //printf("amp: %f\n", constamp);
                        __m128 ampfinal = _mm_load_ps1(&constamp);
                        /*float ampv1p[16];
                        float ampv2p[16];
                        memcpy(ampv1p, &ampv1, sizeof(ampv1p));
                        memcpy(ampv2p, &ampv2, sizeof(ampv2p));
                        printf("Amp v1 before broadcast: %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f \n", ampv1p[0],
                               ampv1p[1],ampv1p[2],ampv1p[3],ampv1p[4], ampv1p[5],ampv1p[6],ampv1p[7],
                               ampv1p[8],ampv1p[9],ampv1p[10],ampv1p[11],ampv1p[12],ampv1p[13],ampv1p[14],ampv1p[15]
                            );
                        printf("Amp v2 before broadcast: %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f \n", ampv2p[0],
                               ampv2p[1],ampv2p[2],ampv2p[3],ampv2p[4], ampv2p[5],ampv2p[6],ampv2p[7],
                               ampv2p[8],ampv2p[9],ampv2p[10],ampv2p[11],ampv2p[12],ampv2p[13],ampv2p[14],ampv2p[15]
                               );*/
                        ampv1 = _mm512_mask_broadcastss_ps(ampv1, amp_mask1, ampfinal);
                        ampv2 = _mm512_mask_broadcastss_ps(ampv2, amp_mask2, ampfinal);
                        //ampv1 = ampv1t;
                        //ampv2 = ampv2t;
                        //memcpy(ampv1p, &ampv1, sizeof(ampv1p));
                        //memcpy(ampv2p, &ampv2, sizeof(ampv2p));
                        /*printf("Amp v1: %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f \n", ampv1p[0],
                               ampv1p[1],ampv1p[2],ampv1p[3],ampv1p[4], ampv1p[5],ampv1p[6],ampv1p[7],
                               ampv1p[8],ampv1p[9],ampv1p[10],ampv1p[11],ampv1p[12],ampv1p[13],ampv1p[14],ampv1p[15]
                            );
                        printf("Amp v2: %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f \n", ampv2p[0],
                               ampv2p[1],ampv2p[2],ampv2p[3],ampv2p[4], ampv2p[5],ampv2p[6],ampv2p[7],
                               ampv2p[8],ampv2p[9],ampv2p[10],ampv2p[11],ampv2p[12],ampv2p[13],ampv2p[14],ampv2p[15]
                               );*/
                    }
                    else if (likely(cmd->op() == CmdType::AmpFn || cmd->op() == CmdType::AmpVecFn)) {
                        // first time seeing function command
                        if (cmd->t + cmd->len > m_cur_t) {
                            // command still active
                            active_cmds.push_back(new activeCmd(cmd, m_t_serv_to_client));
                            std::pair<double, double> these_vals;
                            these_vals = active_cmds.back()->eval(m_cur_t - cmd->t);
                            amp = these_vals.first * amp_scale;
                            damp = these_vals.second * amp_scale;
                        }
                        else {
                            amp = cmd->final_val * amp_scale; // otherwise set to final value.
                        }
                    }
                    else if (unlikely(cmd->op() == CmdType::Phase)) {
                        phase = cmd->final_val * phase_scale_client; // may need to be changed
                    }
                    else {
                        //encountered a non phase,amp,freq command
                        break;
                    }
                    cmd_next(); // increment cmd counter
                    cmd = get_cmd_curt(); // get command only if it's current
                } while (cmd && cmd->chn == i);
                if (damp != 0)
                {
                    //std::cout << "damp: " << damp << std::endl;
                }
                if (amp > amp_scale) {
                    amp = amp_scale;
                }
                if (amp + damp > amp_scale) {
                    damp = 0;
                }
                if (unlikely(ampSet)) {
                    compute_single_chn(v1, v2, float(phase * phase_scale), float(freq * freq_scale), float(df * freq_scale), ampv1, ampv2);
                }
                else {
                    compute_single_chn(v1, v2, float(phase * phase_scale), float(freq * freq_scale), float(df * freq_scale), amp, damp);
                }
                phase = phase + int64_t(freq * 32) + df * 32 / 2;
                //test_compute_single_chn(out1freq, out2freq, freq, df);
                state.amp = amp + damp;
                state.freq = (uint64_t)((int64_t) freq + df);
            }
            // deal with phase wraparound
            if (phase > 0)
            {
              phase -= max_phase * 4;
              while (unlikely(phase > 0)) {
                  phase -= max_phase * 4;
              }
            }
            state.phase = phase;
        } // channel iteration
        // after done iterating channels
        m_cur_t++; // increment time
        //if (m_cur_t % uint32_t(1e6) == 0) {
        //  std::cout << "t: " << m_cur_t << std::endl;
        //}
        if (m_output_cnt % 19531250 == 0) { // 19531250
            printf("m_output_cnt: %lu\n", m_output_cnt);
        }
        __m512i v;
        v = _mm512_permutex2var_epi16(_mm512_cvttps_epi32(v1), (__m512i)mask0,
                                  _mm512_cvttps_epi32(v2));
        _mm512_store_si512(out, v);
    }
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
        //printf("m_stop 1: %s\n", m_stop.load(std::memory_order_relaxed) ? "true" : "false");
        while(likely(!m_stop.load(std::memory_order_relaxed))) {
            generate_page(m_states);
            //printf("m_stop 2: %s\n", m_stop.load(std::memory_order_relaxed) ? "true" : "false");
            //std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        //printf("m_stop 3: %s\n", m_stop.load(std::memory_order_relaxed) ? "true" : "false");
    }
    State m_states[max_chns]{}; // array of states
    std::thread m_worker{};
};

}

#endif
