// Written by Kenneth Wang Oct 2020

#include "StreamManager.h"

#include <nacs-utils/mem.h>
#include <nacs-utils/thread.h>

using namespace NaCs;

namespace Spcm {

inline Cmd *StreamManager::get_cmd()
{
    if (m_cmd_read == m_cmd_max_read) {
        m_cmd_read = 0;
        m_cmd_read_ptr = m_commands.get_read_ptr(&m_cmd_max_read);
        if (!m_cmd_max_read) {
            return nullptr;
        }
    }
    return &m_cmd_read_ptr[m_cmd_read];
}

inline void StreamManager::cmd_next()
{
    // increment m_cmd_read in the if statement. If hit max_read, alert writer that reading is done
    if (++m_cmd_read == m_cmd_max_read) {
        m_commands.read_size(m_cmd_max_read);
    }
}

inline void StreamManager::send_cmd_to_all(Cmd &cmd)
{
    for (int i = 0; i < n_streams; ++i) {
        m_streams[i]->add_cmd(cmd);
    }
}

template<typename T> inline void StreamManager::sort_cmd_chn(T begin, T end)
{
    // sort amp and freq commands by stream number and then by id within that channel
    return std::sort(begin, end, [] (auto &p1, auto &p2) {
            std::pair<uint32_t, uint32_t> chn_info1, chn_info2;
            chn_info1 = chn_map.at(p1.chn);
            chn_info2 = chn_map.at(p2.chn);
            if (chn_info1.first != chn_info2.first)
                return chn_info1.first < chn_info2.first;
            return chn_info1.second < chn_info2.second;
        });
}

inline void actual_distribute_cmds(uint32_t stream_idx, Cmd *cmd, size_t sz)
{
    // actual distribution to stream_idx
    size_t copied_sz;
    while (sz > 0) {
        copied_sz = m_streams[stream_idx]->copy_cmds(cmd, sz);
        sz -= copied_sz;
    }
}

inline void flush_cmds(Cmd *cmd, size_t sz)
{
    // input are commands at a given time. They will be sorted and then distributed to the right streams. Assumes inputs are only amp and freq commands
    sort_cmd_chn(cmd, cmd + sz);
    uint32_t stream_idx = 0;
    uint32_t tot = 0;
    uint32_t loc = 0; // location in commands
    while ((tot < sz) && (stream_idx < n_streams)) {
        this_cmd_chn = (cmd + loc)->chn;
        if (chn_map.at(this_cmd_chn) == stream_idx) {
            loc++;
            tot++;
        }
        else {
            actual_distribute_cmds(stream_idx, cmd, loc);
            stream_idx++;
            loc = 0;
            cmd += loc;
        }
    }
}

inline size_t StreamManager::distribute_cmds()
{
    // distribute all commands
    Cmd *cmd;
    uint32_t t = 0;
    while ((cmd = get_cmd())){
        
    }
    
}




}; // namespace brace
