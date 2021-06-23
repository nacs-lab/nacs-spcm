// Written by Kenneth Wang, Oct 2020

#ifndef _NACS_SPCM_DUMMYSTREAM_H
#define _NACS_SPCM_DUMMYSTREAM_H

#include "Stream.h"

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

class DummyStreamBase
{
public:
    //inline const int16_t *get_output(size_t *sz)
    //{
    //    return m_output.get_read_ptr(sz); // call to obtain values for output
    //}
    //inline void consume_output(size_t sz)
    //{
    //    return m_output.read_size(sz); // call after finishing using values for output
    //}
    //inline void sync_reader()
    //{
    //    return m_output.sync_reader();
    //}
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
    void step(State *states); // workhorse function to step to next time
    const Cmd *get_cmd();
    DummyStreamBase(double step_t, std::atomic<uint64_t> &cmd_underflow, std::atomic<uint64_t> &underflow, uint32_t stream_num, uint8_t stream_mngr_num) :
        m_step_t(step_t),
        m_cmd_underflow(cmd_underflow),
        m_underflow(underflow),
        m_commands((Cmd*)mapAnonPage(24 * 1024ll, Prot::RW), 1024, 1),
        m_stream_num(stream_num),
        m_stream_mngr_num(stream_mngr_num)
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
    constexpr static uint32_t output_block_sz = 2048; // units of int16_t. 32 of these per _m512
    // Members accessed by worker threads
protected:
    std::atomic_bool m_stop{false};
private:
    uint32_t m_stream_num;
    uint8_t m_stream_mngr_num;
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
    //DataPipe<int16_t> m_output;
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
struct DummyStream : DummyStreamBase {
    DummyStream(double step_t, std::atomic<uint64_t> &cmd_underflow,
           std::atomic<uint64_t> &underflow, uint32_t stream_num, bool start=true, uint8_t stream_mngr_num = 0)
        : DummyStreamBase(step_t, cmd_underflow, underflow, stream_num, stream_mngr_num)
    {
        if (start) {
            start_worker();
        }
    }

    void start_worker()
    {
        m_stop.store(false, std::memory_order_relaxed);
        m_worker = std::thread(&DummyStream::thread_fun, this);
    }
    void stop_worker()
    {
        m_stop.store(true, std::memory_order_relaxed);
        if (m_worker.joinable()){
            m_worker.join();
        }
    }
    ~DummyStream()
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
