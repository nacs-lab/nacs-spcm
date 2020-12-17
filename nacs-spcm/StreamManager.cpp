// Written by Kenneth Wang Oct 2020

#include "StreamManager.h"

#include <nacs-utils/mem.h>
#include <nacs-utils/thread.h>

using namespace NaCs;

namespace Spcm {

inline const Cmd *StreamManagerBase::get_cmd()
{
    if (m_cmd_read == m_cmd_max_read) {
        m_cmd_read = 0;
        m_cmd_read_ptr = m_commands.get_read_ptr(&m_cmd_max_read);
        if (!m_cmd_max_read) {
            return nullptr;
        }
    }
    const Cmd *ptr; // the command can be modified through this pointer.
    ptr = &m_cmd_read_ptr[m_cmd_read];
    return ptr;
}

inline void StreamManagerBase::cmd_next()
{
    // increment m_cmd_read in the if statement. If hit max_read, alert writer that reading is done
    if (++m_cmd_read == m_cmd_max_read) {
        m_commands.read_size(m_cmd_max_read);
    }
}
// TODO: Command Flushing
inline void StreamManagerBase::send_cmd_to_all(const Cmd &cmd)
{
    for (int i = 0; i < m_n_streams; ++i) {
        m_streams[i]->add_cmd(cmd);
    }
}

template<typename T> inline void StreamManagerBase::sort_cmd_chn(T begin, T end)
{
    // sort amp and freq commands by stream number and then by id within that channel
    return std::sort(begin, end, [this] (auto &p1, auto &p2) {
            std::pair<uint32_t, uint32_t> chn_info1, chn_info2;
            chn_info1 = chn_map.ChnToStream(p1.chn);
            chn_info2 = chn_map.ChnToStream(p2.chn);
            if (chn_info1.first != chn_info2.first)
                return chn_info1.first < chn_info2.first;
            return chn_info1.second < chn_info2.second;
        });
}

inline void StreamManagerBase::actual_send_cmds(uint32_t stream_idx, Cmd *cmd, size_t sz)
{
    // actual distribution to stream_idx
    size_t copied_sz;
    while (sz > 0) {
        copied_sz = m_streams[stream_idx]->copy_cmds(cmd, sz);
        sz -= copied_sz;
    }
}

inline void StreamManagerBase::send_cmds(Cmd *cmd, size_t sz)
{
    // input are commands at a given time. They will be sorted and then distributed to the right streams. Assumes inputs are only amp and freq commands
    if (sz) {
        sort_cmd_chn(cmd, cmd + sz);
        uint32_t stream_idx = 0;
        uint32_t tot = 0;
        uint32_t loc = 0; // location in commands
        while ((tot < sz) && (stream_idx < m_n_streams)) {
            uint32_t this_cmd_chn = (cmd + loc)->chn;
            std::pair<uint32_t, uint32_t> this_stream_info = chn_map.ChnToStream(this_cmd_chn);
            uint32_t stream_num = this_stream_info.first;
            uint32_t stream_pos = this_stream_info.second;
            (*(cmd + loc)).chn = stream_pos; // gets correct channel within stream
            if (stream_num == stream_idx) {
                // keep on accumulating commands for this stream_idx
                loc++;
                tot++;
            }
            else {
                actual_send_cmds(stream_idx, cmd, loc);
                stream_idx++;
                cmd += loc;
                loc = 0;
            }
        }
    // after exiting loop, may still need to distribute some commands.
        if (loc && (stream_idx < m_n_streams)) {
            actual_send_cmds(stream_idx, cmd, loc);
        }
    }
}

inline size_t StreamManagerBase::distribute_cmds()
{
    // distribute all commands
    // What needs to be handled
    //   1) Interpret modChn commands and Meta commands
    //   2) Gather commands at a given t and send them out
    const Cmd *cmd;
    Cmd var_cmd; // non const command
    std::vector<Cmd> non_const_cmds; // non constant commands
    Cmd *first_cmd = nullptr;// first_cmd is first cmd in a group to send
    uint32_t t = 0;
    size_t sz_to_send = 0;
    while ((cmd = get_cmd())){
        if (cmd->op() == CmdType::Meta) {
            // send out previous commands and reset first_cmd
            send_cmds(first_cmd, sz_to_send);
            first_cmd = nullptr;
            sz_to_send = 0;
            // for now, assume meta commands are sent to all
            send_cmd_to_all(*cmd);
        }
        else if (cmd->op() == CmdType::ModChn) {
            send_cmds(first_cmd, sz_to_send);
            first_cmd = nullptr;
            sz_to_send = 0;
            if (cmd->chn == Cmd::add_chn) {
                // if add channel command
                uint32_t stream_num = chn_map.addChn(cmd->final_val); // final_val encodes the real channel number
                m_streams[stream_num]->add_cmd(*cmd); // add an add channel command to the right stream
            }
            else {
                var_cmd = *cmd; // needs modification
                std::pair<uint32_t, uint32_t> stream_info = chn_map.delChn(cmd->chn);
                uint32_t stream_num = stream_info.first;
                (var_cmd).chn = stream_info.second; // change real chn ID to one in the stream
                m_streams[stream_num]->add_cmd(var_cmd);
            }
        }
        else {
            // amplitude, phase or freq command
            var_cmd = *cmd;
            non_const_cmds.push_back(var_cmd);
            if (cmd->t != t) {
                // send out previous commands, reset first_cmd
                send_cmds(first_cmd, sz_to_send);
                sz_to_send = 1;
                first_cmd = &non_const_cmds.back();
                t = cmd->t;
            }
            else {
                sz_to_send++; // keep on collecting commands
            }
        }
        cmd_next(); // move to next command
    } // while brace
    // send out remaining commands
    send_cmds(first_cmd, sz_to_send);
}

NACS_INLINE void StreamManagerBase::generate_page()
{
    // function which processes outputs from streams.
    int *out_ptr;
    while (true) {
        size_t sz_to_write;
        out_ptr = m_output.get_write_ptr(&sz_to_write);
        if (sz_to_write >= output_block_sz) {
            break;
        }
        if (sz_to_write > 0)
            m_output.sync_writer();
        CPU::pause();
    }
    // wait for input streams to be ready
    uint32_t stream_idx = 0;
    size_t sz_to_read;
    const int *read_ptr;
    while (stream_idx < m_n_streams) {
        read_ptr = (*m_streams[stream_idx]).get_output(&sz_to_read);
        if (sz_to_read >= output_block_sz) {
            stream_ptrs[stream_idx] = read_ptr;
            stream_idx++;
        }
        else {
            CPU::pause();
            (*m_streams[stream_idx]).sync_reader();
        }
    }
    // now streams are ready. 
    for (uint32_t stream_idx = 0; stream_idx < m_n_streams; stream_idx++) {
        for (uint32_t i = 0; i < output_block_sz; i++) {
            if (stream_idx == 0) {
                // first pass
                *(out_ptr + i) = *(stream_ptrs[stream_idx] + i);
            }
            else {
                *(out_ptr + i) = *(out_ptr + i) + *(stream_ptrs[stream_idx] + i);
            }
            if (i == output_block_sz - 1) {
                (*m_streams[stream_idx]).consume_output(output_block_sz); // allow stream to continue
            }
        }
    }
    m_output.wrote_size(output_block_sz); // wrote to output. 
    m_cur_t += output_block_sz;
    m_output_cnt += output_block_sz;
}






}; // namespace brace
