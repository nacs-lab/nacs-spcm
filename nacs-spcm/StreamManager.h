// Written by Kenneth Wang, Oct 2020

#ifndef _NACS_SPCM_STREAMMANAGER_H
#define _NACS_SPCM_STREAMMANAGER_H

#include <nacs-utils/thread.h>
#include <nacs-utils/mem.h>
#include "Stream.h"
#include "Config.h"

#include <thread>
#include <vector>

using namespace NaCs;

namespace Spcm {
const uint32_t UINT_MAX = 4294967295;

class Controller;

template<class T>
inline std::pair<uint32_t, T> get_min(T *begin, size_t sz) {
    T smallest = *begin;
    uint32_t smallest_idx = 0;
    for (uint32_t i = 0; i < sz; i++) {
        if (*begin < smallest) {
            smallest = *begin;
            smallest_idx = i;
        }
        begin++;
    }
    return std::make_pair(smallest_idx, smallest);
}

struct ChannelMap {
    ChannelMap(uint32_t n_streams, uint32_t max_per_chn)
        : m_max_per_chn(max_per_chn),
          stream_cnt(n_streams) {
        chn_counts.reserve(n_streams);
        chn_map.reserve(n_streams * max_per_chn);
        for (int i = 0; i < n_streams; i++) {
            chn_counts.push_back(0);
            chn_map.push_back(UINT_MAX);
        }
        for (int i = n_streams; i < (n_streams * max_per_chn); i++) {
            chn_map.push_back(UINT_MAX);
        }
              //std::cout << "Initial chn count: ";
              //for (int i = 0; i < n_streams; i++) {
              //    std::cout << chn_counts[i] << ' ';
              //}
              //std::cout << std::endl;
    }
private:
    inline uint32_t getChn(uint32_t chnid) {
        // returns idx of entry with chnid
        int i = 0;
        for (; i < chn_map.size(); i++) {
            if (chn_map[i] == chnid) {
                break;
            }
        }
        return i;
    }

    inline uint32_t getIdx(std::pair<uint32_t, uint32_t> stream_info) {
        return stream_info.first * m_max_per_chn + stream_info.second;
    }
    inline std::pair<uint32_t, uint32_t> getStreamInfo(uint32_t idx) {
        uint32_t stream_num = idx / m_max_per_chn;
        uint32_t stream_pos = idx % m_max_per_chn;
        return std::make_pair(stream_num, stream_pos);
    }
public:
    inline bool isChn(uint32_t chnid) {
        return getChn(chnid) != chn_map.size(); // checks if key exists
    }

    inline std::pair<uint32_t, uint32_t> ChnToStream(uint32_t chnid) {
        return getStreamInfo(getChn(chnid));
    }
    inline uint32_t StreamToChn(std::pair<uint32_t, uint32_t> stream_info) {
        return chn_map[getIdx(stream_info)];
    }

    inline bool addChn(uint32_t chnid, uint32_t &stream_num) {
        // fills in stream_num to addChn to, returns true if it's added. returns false, if not.
        if (tot_chns >= (m_max_per_chn * stream_cnt)) {
            stream_num = stream_cnt; // return number of streams if unsuccessful
            return false;
        }
        uint32_t idx = getChn(chnid);
        uint32_t stream_idx;
        if (idx != chn_map.size()) // if already added
        {
            stream_num = idx / m_max_per_chn; // return stream num
            return false;
        }
        else {
            // add a chn to stream with fewest channels
            std::pair<uint32_t, uint32_t> min_info;
            min_info = get_min<uint32_t>(chn_counts.data(), chn_counts.size());
            //std::cout << "chn_counts:";
            //for (int i = 0; i < chn_counts.size(); ++i) {
            //    std::cout << chn_counts[i] << ' ';
            //}
            //std::cout << std::endl;
            //std::cout << min_info.first << std::endl;
            //std::cout << min_info.second << std::endl;
            stream_idx = min_info.first;
            uint32_t map_idx = getIdx(min_info);
            //std::cout << map_idx << std::endl;
            //std::cout << chn_map.size() << std::endl;
            chn_map[map_idx] = chnid;
            chn_counts[stream_idx] = chn_counts[stream_idx] + 1;
            tot_chns++;
        }
        stream_num = stream_idx;
        return true;
    }

    inline std::pair<uint32_t,uint32_t> delChn(uint32_t chnid) {
        // returns stream idx to delete from and position there
        uint32_t idx = getChn(chnid);
        if (idx == chn_map.size()){
            return std::make_pair(idx,UINT_MAX); // entry doesnt exist
        }
        else {
            // implements what happens in stream. when a channel is deleted, the last channel gets moved to the deleted channel.
            std::pair<uint32_t, uint32_t> stream_info = getStreamInfo(idx);
            uint32_t stream_num = stream_info.first;
            chn_counts[stream_num] = chn_counts[stream_num] - 1;
            uint32_t stream_last = getIdx(std::make_pair(stream_num, chn_counts[stream_num]));
            chn_map[idx] = chn_map[stream_last];
            chn_map[stream_last] = UINT_MAX;
            tot_chns--;
            return stream_info;
        }
    }

    inline void reset() {
        for (int i = 0; i < stream_cnt; i++) {
            chn_counts[i] = 0;
            chn_map[i] = UINT_MAX;
        }
        for (int i = stream_cnt; i < (stream_cnt * m_max_per_chn); i++) {
            chn_map[i] = UINT_MAX;
        }
        tot_chns = 0;
    }

    std::vector<uint32_t> chn_map; // vector of size m_max_per_chn * stream_cnt. entry is chnid
private:
    std::vector<uint32_t> chn_counts; // vector of size stream_cnt. gives number of chns in each stream
    uint32_t tot_chns = 0;
    uint32_t m_max_per_chn;
    uint32_t stream_cnt;
};

std::ostream &operator<<(std::ostream &stm, ChannelMap cmap);

class StreamManagerBase
{
    // This class is responsible for passing commands to the various streams and also conveying output.
public:
    inline void reset()
    {
        for (int i = 0; i < m_n_streams; i++) {
            (*m_streams[i]).add_cmd(Cmd::getResetAll());
        }
        chn_map.reset();
    }
    inline void sync_reader()
    {
        m_output.sync_reader();
    }
    inline const int16_t *get_output(size_t &sz)
    {
        return m_output.get_read_ptr(&sz); // stores into size number of outputs ready, and returns a pointer
    }
    inline void consume_output(size_t sz)
    {
        return m_output.read_size(sz);
    }
    inline void set_start_trigger(uint32_t v, uint64_t t)
    {
        for (int i = 0; i < m_n_streams; i++) {
            (*m_streams[i]).set_start_trigger(v, t);
        }
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
            //std::cout << "Started Stream: " << i << std::endl;
        }
    }
    inline void stop_streams() {
        for (int i = 0; i < m_n_streams; i++) {
            (*m_streams[i]).stop_worker();
            (*m_streams[i]).consume_all_cmds(); // clear up any remaining commands in the stream, so next worker starts fresh.
            (*m_streams[i]).reset_output_cnt();
        }
    }
    inline bool is_wait_for_seq() {
        for (int i = 0; i < m_n_streams; i++) {
            if (!(*m_streams[i]).is_wait_for_seq())
            {
                return false;
            }
        }
        return true;
    }
    bool reqRestart(uint32_t trig_id);
    void reset_streams_out() {
        for (int i = 0; i < m_n_streams; i++) {
            (*m_streams[i]).reset_out();
        }
    }
    inline void reset_output() {
        size_t sz;
        //uint32_t i = 0;
        //m_output.sync_reader();
        do {
            //printf("reset count %u, sz: %u", i, sz);
            //i++;
            m_output.get_read_ptr(&sz);
            m_output.read_size(sz); // reset my own output.
        } while (sz != 0);
    }
protected:
    StreamManagerBase(Controller& ctrl,Config &conf, uint32_t n_streams, uint32_t max_per_stream,
                      double step_t, std::atomic<uint64_t> &cmd_underflow,
                      std::atomic<uint64_t> &underflow, bool start = false)
        : m_ctrl(ctrl),
          m_conf(conf),
          m_n_streams(n_streams),
          m_max_per_stream(max_per_stream),
          chn_map(n_streams, max_per_stream),
          m_commands((Cmd*)mapAnonPage(sizeof(Cmd) * 1024ll, Prot::RW), 1024, 512),
          m_output((int16_t*)mapAnonPage(output_buf_sz, Prot::RW), output_buf_sz / 2, output_buf_sz / 2)
    {
        // start streams
        for (int i = 0; i < n_streams; i++) {
            Stream<128> *stream_ptr;
            stream_ptr = new Stream<128>(*this, m_conf, step_t, cmd_underflow, underflow, i, start);
            m_streams.push_back(stream_ptr);
            stream_ptrs.push_back(nullptr);
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
    
    std::vector<Stream<128>*> m_streams; // vector of Streams to manage
    std::vector<const int16_t*> stream_ptrs; // vector of stream_ptrs
    ChannelMap chn_map;
    uint32_t m_n_streams = 0;
    uint32_t m_max_per_stream = 0;
    Config& m_conf;
    int64_t m_cur_t = 0; // current time for output
    uint64_t m_output_cnt = 0; // output count, units of output_block_sz

    uint64_t output_buf_sz = 1 * 1024ll * 1024ll; // in bytes. Let Streams below it throttle the filling of this buffer.
    uint64_t wait_buf_sz = 1 * 1024ll * 1024ll;
    DataPipe<Cmd> m_commands; // command pipe for writers to put in commands
    DataPipe<int16_t> m_output; // pipe for output and hardware to output
    constexpr static uint32_t output_block_sz = 2048 * 16; //32768; //2048;

    const Cmd *m_cmd_read_ptr = nullptr; // pointer to read commands
    size_t m_cmd_read = 0;
    size_t m_cmd_max_read = 0;
    Cmd *m_cmd_write_ptr __attribute__ ((aligned(64))) = nullptr;
    size_t m_cmd_wrote = 0;
    size_t m_cmd_max_write = 0;

    uint64_t stuck_counter = 0;
    Controller& m_ctrl;
    uint32_t restart_id;
};

struct StreamManager : StreamManagerBase {
    StreamManager(Controller &ctrl,Config &conf, uint32_t n_streams, uint32_t max_per_stream,
                  double step_t, std::atomic<uint64_t> &cmd_underflow,
                  std::atomic<uint64_t> &underflow, bool startStream = false,
                  bool startWorker = false)
        : StreamManagerBase(ctrl, conf, n_streams, max_per_stream, step_t, cmd_underflow, underflow, startStream)
    {
        if (startWorker)
        {
            start_worker();
        }
    }

    void start_worker()
    {
        m_stop.store(false, std::memory_order_relaxed);
        m_worker = std::thread(&StreamManager::thread_fun, this);
    }
    void stop_worker()
    {
        m_stop.store(true, std::memory_order_relaxed);
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }
    void reset_out() {
        printf("reset out stm mngr called\n");
        if (m_worker.joinable()) {
            stop_worker();
        }
        reset_output();
        //size_t sz;
        //m_output.sync_reader();
        //m_output.get_read_ptr(&sz);
        //m_output.read_size(sz); // reset my own output.
    }
    ~StreamManager()
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
        while(likely(!m_stop.load(std::memory_order_relaxed))) {
            //std::cout << "here" << std::endl;
            generate_page();
        }
    }

    std::thread m_worker{};
};

} // namespace brace

#endif
