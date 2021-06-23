// Written by Kenneth Wang Oct 2020

#include "DummyStreamManager.h"

#include <nacs-utils/mem.h>
#include <nacs-utils/thread.h>
#include <nacs-utils/timer.h>

#include <thread>
#include <chrono>

using namespace NaCs;

using namespace std::chrono_literals;

namespace Spcm {

inline const Cmd *DummyStreamManagerBase::get_cmd()
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

inline void DummyStreamManagerBase::cmd_next()
{
    // increment m_cmd_read in the if statement. If hit max_read, alert writer that reading is done
    if (++m_cmd_read == m_cmd_max_read) {
        m_commands.read_size(m_cmd_max_read);
    }
}
// TODO: Command Flushing
inline void DummyStreamManagerBase::send_cmd_to_all(const Cmd &cmd)
{
    for (int i = 0; i < m_n_streams; ++i) {
        m_streams[i]->add_cmd(cmd);
    }
}

template<typename T> inline void DummyStreamManagerBase::sort_cmd_chn(T begin, T end)
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

inline void DummyStreamManagerBase::actual_send_cmds(uint32_t stream_idx, Cmd *cmd, size_t sz)
{
    // actual distribution to stream_idx
    size_t copied_sz;
    Cmd *this_cmd = cmd;
    std::cout << "Sz: " << sz << std::endl;
    while (sz > 0) {
        copied_sz = m_streams[stream_idx]->copy_cmds(this_cmd, sz);
        sz -= copied_sz;
        //std::cout << "Sz after: " << sz << std::endl;
        std::cout << "Sent " << *this_cmd << " to Stream" << stream_idx << std::endl;
        this_cmd = cmd + copied_sz;
    }
}

inline void DummyStreamManagerBase::send_cmds(Cmd *cmd, size_t sz)
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
        for (int i = 0; i < sz; ++i){
            std::pair<uint32_t, uint32_t> this_stream_info = chn_map.ChnToStream(cmd[i].chn);
            stream_num.push_back(this_stream_info.first);
            stream_pos.push_back(this_stream_info.second);
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

NACS_EXPORT() void DummyStreamManagerBase::distribute_cmds()
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
                std::cout << "First cmd is now: " << *first_cmd << " at address " << first_cmd << std::endl;
                t = cmd->t;
                //std:: cout << "Now t is: " << t << std::endl;
            }
            else {
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
}
__attribute__((target("avx512f,avx512bw"), flatten))
NACS_EXPORT() void DummyStreamManagerBase::generate_page()
{
    std::this_thread::sleep_for(100ms);
}






}; // namespace brace
