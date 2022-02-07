// Written by Kenneth Wang Oct 2020

#include "StreamManager.h"
#include "ControllerText.h"

#include <nacs-utils/mem.h>
#include <nacs-utils/thread.h>

using namespace NaCs;

namespace Spcm {

NACS_EXPORT() std::ostream &operator<<(std::ostream &stm, ChannelMap cmap) {
    stm << "Channel Map: (";
    for (int i = 0; i < cmap.chn_map.size(); i++) {
        stm << cmap.chn_map[i] << ", ";
    }
    stm << ")" << std::endl;
    return stm;
}

bool StreamManagerBase::reqRestart(uint32_t trig_id) {
    if (trig_id == restart_id) {
        // restart already requested
        return false;
    }
    restart_id = trig_id;
    m_ctrl.reqRestart(trig_id);
    return true;
}

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
    //printf("calling actual_send_cmds\n");
    // actual distribution to stream_idx
    size_t copied_sz;
    Cmd *this_cmd = cmd;
    //std::cout << "Sz: " << sz << std::endl;
    while (sz > 0) {
        //printf("In while loop, sz: %u, this_cmd: %p\n", sz, this_cmd);
        copied_sz = m_streams[stream_idx]->copy_cmds(this_cmd, sz);
        sz -= copied_sz;
        //std::cout << "Sz after: " << sz << std::endl;
        //std::cout << "Sent " << *this_cmd << " to Stream" << stream_idx << std::endl;
        this_cmd = this_cmd + copied_sz;
    }
}

inline void StreamManagerBase::send_cmds(Cmd *cmd, size_t sz)
{
    // input are commands at a given time. They will be sorted and then distributed to the right streams. Assumes inputs are only amp and freq commands
    if (sz) {
        for (int i = 0; i < sz; ++i) {
            //std::cout << "Considering inside send_cmds before sort " << cmd[i] << std::endl;
        }
        sort_cmd_chn(cmd, cmd + sz);
        for (int i = 0; i < sz; ++i) {
            //std::cout << "Considering inside send_cmds " << cmd[i] << std::endl;
        }/*
        uint32_t stream_idx = 0;
        uint32_t tot = 0;
        uint32_t loc = 0; // location in commands
        std::vector<uint32_t> real_chn;
        while ((tot < sz) && (stream_idx < m_n_streams)) {
            uint32_t this_cmd_real_chn;
            if (real_chn.size() < tot + 1) {
                this_cmd_real_chn = (cmd + loc)->chn;
                real_chn.push_back(this_cmd_real_chn);
            }
            else {
                this_cmd_real_chn = real_chn[tot];
            }
            std::pair<uint32_t, uint32_t> this_stream_info = chn_map.ChnToStream(this_cmd_real_chn);
            uint32_t stream_num = this_stream_info.first;
            uint32_t stream_pos = this_stream_info.second;
            std::cout << "stream num to send to: " << stream_num << std::endl;
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
            }*/
        std::vector<uint32_t> stream_num, stream_pos;
        stream_num.reserve(sz);
        //stream_pos.reserve(sz);
        for (int i = 0; i < sz; ++i){
            //printf("cmd chn: %u\n", cmd[i].chn);
            std::pair<uint32_t, uint32_t> this_stream_info = chn_map.ChnToStream(cmd[i].chn);
            stream_num.push_back(this_stream_info.first);
            //stream_pos.push_back(this_stream_info.second);
            (*(cmd + i)).chn = this_stream_info.second;
        }
        int counter = 0;
        for (int this_stream_num = 0; this_stream_num < m_n_streams; ++this_stream_num) {
            uint32_t sz_to_send = 0;
            uint32_t first_idx = counter;
            if (counter < sz) {
                while (stream_num[counter] == this_stream_num) {
                    sz_to_send++;
                    counter++;
                    if (counter >= sz) {
                        break;
                    }
                }
            }
            actual_send_cmds(this_stream_num, cmd + first_idx, sz_to_send);
        }
    }
}

NACS_EXPORT() void StreamManagerBase::distribute_cmds()
{
    //std::cout << "Calling distribute_cmds" << std::endl;
    // distribute all commands
    // What needs to be handled
    //   1) Interpret modChn commands and Meta commands
    //   2) Gather commands at a given t and send them out
    const Cmd *cmd;
    Cmd var_cmd; // non const command
    std::vector<Cmd> non_const_cmds; // non constant commands
    non_const_cmds.reserve(100);
    Cmd *first_cmd = nullptr;// first_cmd is first cmd in a group to send
    int64_t t = 0;
    size_t sz_to_send = 0;
    while ((cmd = get_cmd())){
        //std::cout << "Considering " << *cmd << std::endl;
        if (cmd->op() == CmdType::Meta) {
            // send out previous commands and reset first_cmd
            send_cmds(first_cmd, sz_to_send);
            first_cmd = nullptr;
            sz_to_send = 0;
            // for now, assume meta commands are sent to all
            if (cmd->chn == (uint32_t)CmdMeta::ResetAll) {
                // need to reset chn map
                chn_map.reset();
            }
            send_cmd_to_all(*cmd);
        }
        else if (cmd->op() == CmdType::ModChn) {
            send_cmds(first_cmd, sz_to_send);
            first_cmd = nullptr;
            sz_to_send = 0;
            if (cmd->chn == Cmd::add_chn) {
                // if add channel command
                //printf("Process add channel in stream manager\n");
                uint32_t stream_num;
                if(chn_map.addChn(cmd->final_val, stream_num)) // final_val encodes the real channel number
                {
                    m_streams[stream_num]->add_cmd(*cmd); // add an add channel command to the right stream
                }
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
            //std::cout << "This is a amp, phase or freq command" << std::endl;
            var_cmd = *cmd;
            non_const_cmds.push_back(var_cmd);
            //if (!first_cmd){
            //    first_cmd = non_const_cmds.data() + non_const_cmds.size() - 1;
            //}
            for (int i = 0; i < non_const_cmds.size(); ++i) {
                //std::cout << "non const commands: " << i << " " << non_const_cmds[i] << " at address " << &non_const_cmds[i] << std::endl;
            }
            if (cmd->t != t) {
                // send out previous commands, reset first_cmd
                //std::cout << "size of non const commands " << non_const_cmds.size() << std::endl;
                if (first_cmd) {
                    // std::cout << "first command is actually " << *first_cmd << std::endl;
                    //              std::cout << "first command address " << first_cmd << std::endl;
                }
                if (sz_to_send > 1) {
                    // std::cout << "second command is " << *(first_cmd + 1) << std::endl;
                    //std::cout << "second command address " << first_cmd + 1 << std::endl;
                    //std::cout << "second command address vec " << &first_cmd[1] << std::endl;
                }
                //std::cout << "sending cmds in" << std::endl;
                send_cmds(first_cmd, sz_to_send);
                // std::cout << "Sending " << sz_to_send << " commands starting from " << first_cmd << std::endl;
                sz_to_send = 1;
                first_cmd = non_const_cmds.data() + non_const_cmds.size() - 1;
                // std::cout << "First cmd is now: " << *first_cmd << " at address " << first_cmd << std::endl;
                t = cmd->t;
                //std:: cout << "Now t is: " << t << std::endl;
            }
            else {
                if (!first_cmd) {
                    // should enter this branch only if previous command was meta command at same time
                    first_cmd = non_const_cmds.data() + non_const_cmds.size() - 1;
                }
                sz_to_send++; // keep on collecting commands
            }
            //sz_to_send++;
            // send_cmds(&var_cmd, 1);
        }
        cmd_next(); // move to next command
    } // while brace
    // send out remaining commands
    //std::cout << "Size to send: " << sz_to_send << std::endl;
    //std::cout << "sending commands final" << std::endl;
    send_cmds(first_cmd, sz_to_send);
    //std::cout << "done with command distribution" << std::endl;
    // flush all commands to streams
    for (uint32_t i = 0; i < m_n_streams ; i++) {
        m_streams[i]->flush_cmd();
    }
}
__attribute__((target("avx512f,avx512bw"), flatten))
NACS_EXPORT() void StreamManagerBase::generate_page()
{
    // function which processes outputs from streams.
    int16_t *out_ptr;
    while (true) {
        size_t sz_to_write;
        out_ptr = m_output.get_write_ptr(&sz_to_write);
        if (sz_to_write >= output_block_sz) {
            if (!is_wait_for_seq() || m_output.check_reader(wait_buf_sz/2)) {
                break;
            }
        }
        if (sz_to_write > 0) {
            m_output.sync_writer();
        }
        CPU::pause();
        // if (stuck_counter % 10000000 == 0) {
        //     printf("Stream Manager stuck: %lu, sz: %u, ptr: %p\n", stuck_counter, sz_to_write, out_ptr);
        //     printf("Write buff location: %p", m_output.get_write_buff(&sz_to_write));
        //     printf(" sz: %u\n", sz_to_write);
        // }
        // stuck_counter++;
        if (unlikely(m_stop.load(std::memory_order_relaxed))) {
            return;
        }
    }
    //std::cout << "can write" << std::endl;
    // wait for input streams to be ready
    uint32_t stream_idx = 0;
    size_t sz_to_read;
    const int16_t *read_ptr;
    while (stream_idx < m_n_streams) {
        read_ptr = (*m_streams[stream_idx]).get_output(&sz_to_read);
        if (sz_to_read >= output_block_sz) {
            //std::cout << stream_idx << std::endl;
            stream_ptrs[stream_idx] = read_ptr;
            stream_idx++;
        }
        else {
            CPU::pause();
            //(*m_streams[stream_idx]).sync_reader();
        }
        if (unlikely(m_stop.load(std::memory_order_relaxed))) {
            return;
        }
    }
    //std::cout << "stream ready" << std::endl;
    // now streams are ready.
    //std::cout << "reading from streams" << std::endl;
    //__m512i data;
    /*
    for (uint32_t stream_idx = 0; stream_idx < m_n_streams; stream_idx++) {
        for (uint32_t i = 0; i < output_block_sz; i+= 32) {
            if (stream_idx == 0) {
                // first pass
                _mm512_store_si512(&out_ptr[i], *(__m512i*)(stream_ptrs[stream_idx] + i));
                //*(out_ptr + i) = *(stream_ptrs[stream_idx] + i);
            }
            else {
                _mm512_store_si512(&out_ptr[i], _mm512_add_epi16(
                                       *(__m512i*)(&out_ptr[i]),
                                       *(__m512i*)(stream_ptrs[stream_idx] + i)));
                //*(out_ptr + i) = *(out_ptr + i) + *(stream_ptrs[stream_idx] + i);
            }
            if (i == output_block_sz - 32) {
                (*m_streams[stream_idx]).consume_output(output_block_sz); // allow stream to continue
            }
        }
    }
    */
    __m512i data;
    for (uint32_t i = 0; i < output_block_sz; i += 32) {
        _mm512_store_si512(&data, *(__m512i*)(stream_ptrs[0] + i));
        for (uint32_t stream_idx = 1; stream_idx < m_n_streams; stream_idx++) {
            data = _mm512_add_epi16(data, *(__m512i*)(stream_ptrs[stream_idx] + i));
        }
        _mm512_store_si512(&out_ptr[i], data);
    }
    for (uint32_t stream_idx = 0; stream_idx < m_n_streams; stream_idx++) {
        (*m_streams[stream_idx]).consume_output(output_block_sz);
    }
    //std::cout << "output_block_sz: " << output_block_sz << std::endl;
    //std::cout << "m_cur_t: " << m_cur_t << std::endl;
    m_output.wrote_size(output_block_sz); // wrote to output.
    //std::cout << "output written" << std::endl;
    m_cur_t += 1;
    m_output_cnt += 1;
}






}; // namespace brace
