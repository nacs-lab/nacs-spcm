// Written by Kenneth Wang, Oct 2020

#ifndef _NACS_SPCM_DUMMYSTREAMMANAGER_H
#define _NACS_SPCM_DUMMYSTREAMMANAGER_H

#include <nacs-utils/thread.h>
#include <nacs-utils/mem.h>
#include "DummyStream.h"
#include "StreamManager.h"

#include <thread>
#include <vector>

using namespace NaCs;

namespace Spcm {

class DummyStreamManagerBase
{
    // This class is responsible for passing commands to the various streams and also conveying output.
public:
    //inline const int16_t *get_output(size_t &sz)
    //{
    //  return m_output.get_read_ptr(&sz); // stores into size number of outputs ready, and returns a pointer
    //}
    //inline void consume_output(size_t sz)
    //{
    //    return m_output.read_size(sz);
    //}
    inline void set_start_trigger(uint32_t v, uint64_t t)
    {
        for (int i = 0; i < m_n_streams; i++) {
            (*m_streams[i]).set_start_trigger(v, t);
        }
        //printf("Sending trigger %u for time %u\n", v, t);
    }
    inline uint32_t get_end_triggered()
    {
        uint32_t min_end_triggered = UINT_MAX;
        for (int i = 0; i < m_n_streams; i++) {
            min_end_triggered = std::min(min_end_triggered, (*m_streams[i]).end_triggered());
        }
        return min_end_triggered;
    }
    inline size_t copy_cmds(Cmd *cmds, size_t sz)
    {
        if (!probe_cmd_input())
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
    inline bool try_add_cmd (Cmd &cmd)
    {
        return copy_cmds(&cmd, 1) != 0; // returns true if successfully added
    }
    inline void add_cmd(Cmd &cmd)
    {
        while(!try_add_cmd(cmd)) {
            CPU::pause();
        }
    }
    inline void flush_cmd()
    {
        // use to flush commands that have not been written cause m_cmd_max_write is too large for instance
        if (m_cmd_wrote) {
            m_cmd_max_write -= m_cmd_wrote;
            m_cmd_write_ptr += m_cmd_wrote;
            m_commands.wrote_size(m_cmd_wrote);
            m_cmd_wrote = 0;
        }
    }
    const Cmd *get_cmd();
    void distribute_cmds(); // distributes all commands to streams
    int64_t get_cur_t(){
        return m_cur_t;
    }
    inline ChannelMap get_chn_map() {
        return chn_map;
    }
    inline void start_streams() {
        for (int i = 0; i < m_n_streams; i++) {
            (*m_streams[i]).start_worker();
            std::cout << "Started Stream: " << i << std::endl;
        }
    }
    inline void stop_streams() {
        for (int i = 0; i < m_n_streams; i++) {
            (*m_streams[i]).stop_worker();
        }
    }
protected:
    DummyStreamManagerBase(uint32_t n_streams, uint32_t max_per_stream,
                      double step_t, std::atomic<uint64_t> &cmd_underflow,
                      std::atomic<uint64_t> &underflow, bool start = false, uint8_t id = 0)
        : m_n_streams(n_streams),
          m_max_per_stream(max_per_stream),
          chn_map(n_streams, max_per_stream),
          m_commands((Cmd*)mapAnonPage(24 * 1024ll, Prot::RW), 1024, 1),
          stream_manager_id(id)
    {
        // start streams
        for (int i = 0; i < n_streams; i++) {
            DummyStream<128> *stream_ptr;
            stream_ptr = new DummyStream<128>(step_t, cmd_underflow, underflow, i, start, id);
            m_streams.push_back(stream_ptr);
            //stream_ptrs.push_back(nullptr);
        }
    }
    void generate_page();

    std::atomic_bool m_stop{false};
private:
    inline bool probe_cmd_input()
    {
        // returns true if there are still commands to write, and determines number of cmds to write
        // returns false if no commands left
        if (m_cmd_wrote == m_cmd_max_write) {
            m_cmd_wrote = 0;
            m_cmd_write_ptr = m_commands.get_write_ptr(&m_cmd_max_write);
            if (!m_cmd_max_write) {
                return false;
            }
        }
        return true;
    }
    
    template<typename T> inline void sort_cmd_chn(T begin, T end);
    // Cmd *get_cmd_curt();
    void cmd_next();
    void send_cmd_to_all(const Cmd &cmd);
    void actual_send_cmds(uint32_t stream_idx, Cmd *cmd, size_t sz);
    void send_cmds(Cmd *cmd, size_t sz);
    
    std::vector<DummyStream<128>*> m_streams; // vector of Streams to manage
    //std::vector<const int16_t*> stream_ptrs; // vector of stream_ptrs
    ChannelMap chn_map;
    uint32_t m_n_streams = 0;
    uint32_t m_max_per_stream = 0;
    
    int64_t m_cur_t = 0; // current time for output
    uint64_t m_output_cnt = 0; // output count, units of output_block_sz

    DataPipe<Cmd> m_commands; // command pipe for writers to put in commands
    //DataPipe<int16_t> m_output; // pipe for output and hardware to output
    constexpr static uint32_t output_block_sz = 2048;
    
    const Cmd *m_cmd_read_ptr = nullptr; // pointer to read commands
    size_t m_cmd_read = 0;
    size_t m_cmd_max_read = 0;
    Cmd *m_cmd_write_ptr __attribute__ ((aligned(64))) = nullptr;
    size_t m_cmd_wrote = 0;
    size_t m_cmd_max_write = 0;

    uint8_t stream_manager_id = 0;
};

struct DummyStreamManager : DummyStreamManagerBase {
    DummyStreamManager(uint32_t n_streams, uint32_t max_per_stream,
                  double step_t, std::atomic<uint64_t> &cmd_underflow,
                  std::atomic<uint64_t> &underflow, bool startStream = false,
                  bool startWorker = false, uint8_t id = 0)
        : DummyStreamManagerBase(n_streams, max_per_stream, step_t, cmd_underflow, underflow, startStream, id)
    {
        if (startWorker)
        {
            start_worker();
        }
    }

    void start_worker()
    {
        m_stop.store(false, std::memory_order_relaxed);
        m_worker = std::thread(&DummyStreamManager::thread_fun, this);
    }
    void stop_worker()
    {
        m_stop.store(true, std::memory_order_relaxed);
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }
    ~DummyStreamManager()
    {
        stop_worker();
    }
private:
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
        printf("StreamManager worker started\n");
        while(likely(!m_stop.load(std::memory_order_relaxed))) {
            //std::cout << "here" << std::endl;
            generate_page();
        }
        printf("StreamManager worker stopped\n");
    }

    std::thread m_worker{};

};

} // namespace brace

#endif
