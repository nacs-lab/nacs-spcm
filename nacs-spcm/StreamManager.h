// Written by Kenneth Wang, Oct 2020

#ifndef _NACS_SPCM_STREAMMANAGER_H
#define _NACS_SPCM_STREAMMANAGER_H

#include <nacs-utils/thread.h>
#include <nacs-utils/mem.h>

#include <thread>
#include <vector>

using namespace NaCs;

namespace Spcm {

class StreamManager
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
    
private:
    template<typename T> inline void sort_cmd_chn(T begin, T end);
    inline size_t distribute_cmds(); // distributes all commands to streams
    
    std::vector<Stream*> m_streams; // vector of Streams to manage
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> chn_map; // maps real channel id to pair of stream number and index within
    
    uint32_t m_cur_t = 0; // current time for output
    uint64_t m_output_cnt = 0; // output count

    DataPipe<Cmd> m_commands; // command pipe for writers to put in commands
    DataPipe<int16_t> m_output; // pipe for output and hardware to output

    Cmd *m_cmd_read_ptr = nullptr; // pointer to read commands
    size_t m_cmd_read = 0;
    size_t m_cmd_max_read = 0;
    Cmd *m_cmd_write_ptr __attribute__ ((aligned(64))) = nullptr;
    size_t m_cmd_wrote = 0;
    size_t m_cmd_max_write = 0;
    
};

} // namespace brace

#endif
