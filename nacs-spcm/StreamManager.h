// Written by Kenneth Wang, Oct 2020

#ifndef _NACS_SPCM_STREAMMANAGER_H
#define _NACS_SPCM_STREAMMANAGER_H

#include <nacs-utils/thread.h>
#include <nacs-utils/mem.h>

#include <thread>
#include <vector>

using namespace NaCs;

namespace Spcm {

template<class T>
inline std::pair<uint32_t, T> get_min(T *begin, size_t sz) {
    T smallest = *begin;
    uint32_t smallest_idx = 0;
    for (int i = 0; i < sz; i++) {
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
              for (int i = 0; i < n_streams; i++) {
                  chn_counts.push_back(0);
                  chn_map.push_back(UINT_MAX);
              }
              for (int i = n_streams; i < (n_streams * max_per_chn); i++) {
                  chn_map.push_back(UINT_MAX);
              }
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
        return chn_map[getIdx[stream_info]];
    }

    inline uint32_t addChn(uint32_t chnid) {
        // return stream idx to add to
        if (tot_chns >= (m_max_per_chn * stream_cnt)) {
            return stream_cnt; // return number of streams if unsuccessful
        }
        uint32_t idx = getChn(chnid);
        if (idx != chn_map.size())
        {
            return idx / m_max_per_chn;
        }
        else {
            // add a chn to stream with fewest channels
            std::pair<uint32_t, uint32_t> min_info;
            min_info = get_min<uint32_t>(chn_counts.data(), chn_counts.size());
            uint32_t stream_idx = min_info.first;
            uint32_t val = min_info.second;
            uint32_t map_idx = getIdx(min_info);
            chn_map[map_idx] = chnid;
            chn_counts[stream_idx] = chn_counts[idx] + 1;
            tot_chns++;
        }
        return stream_idx;
    }

    inline std::pair<uint32_t,uint32_t> delChn(uint32_t chnid) {
        // returns stream idx to delete from and position there
        uint32_t idx = getChn(chnid);
        if (idx == chn_map.size()){
            return std::make_pair<idx,UINT_MAX>; // entry doesnt exist
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
private:
    std::vector<uint32_t> chn_map; // vector of size m_max_per_chn * stream_cnt. entry is chnid
    std::vector<uint32_t> chn_counts; // vector of size stream_cnt. gives number of chns in each stream
    uint32_t tot_chns = 0;
    uint32_t m_max_per_chn;
    uint32_t stream_cnt;
}

class StreamManagerBase
{
    // This class is responsible for passing commands to the various streams and also conveying output.
public:
    inline const int16_t *get_output(size_t &sz)
    {
        return m_output.get_read_ptr(&sz); // stores into size number of outputs ready, and returns a pointer
    }
    inline void consume_output(size_t sz)
    {
        return m_output.read_size(sz);
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
    Cmd *get_cmd();
    inline size_t distribute_cmds(); // distributes all commands to streams
protected:
    void generate_page();
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
    void send_cmd_to_all(Cmd &cmd);
    void actual_send_cmds(uint32_t stream_idx, Cmd *cmd, size_t sz);
    void send_cmds(Cmd *cmd, size_t sz);
    
    std::vector<Stream*> m_streams; // vector of Streams to manage
    std::vector<int*> stream_ptrs; // vector of stream_ptrs
    ChannelMap chn_map;
    uint32_t m_n_streams = 0;
    uint32_t m_max_per_stream = 0;
    
    uint32_t m_cur_t = 0; // current time for output
    uint64_t m_output_cnt = 0; // output count

    DataPipe<Cmd> m_commands; // command pipe for writers to put in commands
    DataPipe<int> m_output; // pipe for output and hardware to output
    constexpr static uint32_t output_block_sz = 1;
    
    Cmd *m_cmd_read_ptr = nullptr; // pointer to read commands
    size_t m_cmd_read = 0;
    size_t m_cmd_max_read = 0;
    Cmd *m_cmd_write_ptr __attribute__ ((aligned(64))) = nullptr;
    size_t m_cmd_wrote = 0;
    size_t m_cmd_max_write = 0;
    
};

} // namespace brace

#endif
