//

#include "FileStream.h"

using namespace NaCs;

namespace Spcm {

NACS_INTERNAL NACS_NOINLINE const FCmd*
FStream::consume_old_cmds()
{
    // consumes old commands (updates the fnames array) and returns a pointer to a currently active command.
    // If only commands in future or no commands, then return nullptr
    const FCmd* cmd = static_cast<const FCmd*>(get_cmd());
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
                wait_for_seq = true;
                m_end_trigger_pending = cmd->final_val;
            }
            else if (cmd-> chn == (uint32_t)CmdMeta::TriggerStart) {
                if (!check_start(cmd->t, cmd->final_val)) {
                    return nullptr;
                }
                wait_for_seq = false;
            }
            break;
        case CmdType::AmpSet:
            m_states[cmd->chn].amp = cmd->final_val;
            m_states[cmd->chn].fname = cmd->fname; // set amplitude of state
            break;
        case CmdType::FreqSet:
            break;
        case CmdType::AmpFn:
        case CmdType::AmpVecFn:
            // cmd pointer only increments. Should be safe to initialize an active command here
            if (cmd->t + cmd->len > m_cur_t) {
                // command still active
                active_cmds.push_back(new activeCmd(cmd, t_serv_to_client));
                std::pair<double, double> these_vals;
                these_vals = active_cmds.back()->eval(m_cur_t - cmd->t);
                m_states[cmd->chn].amp = (these_vals.first + these_vals.second);
            }
            else {
                m_states[cmd->chn].amp = cmd->final_val; // otherwise set to final value.
            }
            m_states[cmd->chn].fname = cmd->fname;
            break;
        case CmdType::FreqFn:
        case CmdType::FreqVecFn:
            break;
        case CmdType::Phase:
            break;
        case CmdType::ModChn:
            if (cmd->chn == Cmd::add_chn) {
                //printf("Process add_chn\n");
                m_states[m_chns] = {0, std::string()}; // initialize new channel
                m_chns++;
            }
            else {
                m_chns--;
                m_states[cmd->chn] = m_states[m_chns]; // move last_chn to place of deleted channel
            }
            break;
        }
        cmd_next(); //after interpretting this command, increment pointer to next one.
    } while(cmd = static_cast<const FCmd*>(get_cmd())); // keep on going until there are no more commands or one reaches the present
    return nullptr;
}

__attribute__((target("avx512f,avx512bw"), flatten))
void FStream::step(int16_t *out){
    // Key function
    const FCmd *cmd;
retry:
    // returns command at current time or before
    if (cmd = static_cast<const FCmd*>(get_cmd_curt())){
        if (unlikely(cmd->t < m_cur_t)) {
            cmd = consume_old_cmds(); //consume past commands
            if (!cmd) {
                goto cmd_out; //if no command available, go to cmd_out
            }
        }
        if (cmd->t > m_cur_t) {
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
                wait_for_seq = true;
            }
            else if (cmd->chn == (uint32_t)CmdMeta::TriggerStart) {
                if (!check_start(cmd->t, cmd->final_val)){
                    cmd = nullptr;
                    goto cmd_out;
                }
                wait_for_seq = false;
            }
            cmd_next();
            goto retry; // keep on going if it's a meta command
        }
        else {
            while (unlikely(cmd->op() == CmdType::ModChn)) {
                if (cmd->chn == Cmd::add_chn) {
                    //printf("Process add chn\n");
                    m_states[m_chns] = {0, std::string()}; // initialize new channel
                    m_chns++;
                }
                else {
                    m_chns--;
                    m_states[cmd->chn] = m_states[m_chns];
                }
                cmd_next();
                cmd = static_cast<const FCmd*>(get_cmd_curt());
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
    else if (unlikely(m_end_trigger_pending)) {m_end_trigger_waiting = m_end_trigger_pending;
        m_end_trigger_pending = 0;
        set_end_trigger(out); // out
    }
    // calculate actual output.
    // For testing purposes. At the moment keep the output simple.
    __m512i v = _mm512_set1_epi16(0);
    uint32_t _nchns = m_chns;
    for (uint32_t i = 0; i < _nchns; i++){
        // iterate through the number of channels
        auto &state = m_states[i];
        std::string fname = state.fname;
        double amp = state.amp;
        double damp = 0;
        // check active commands
        if (active_cmds.size() > 0) {
            auto it = active_cmds.begin();
            while(it != active_cmds.end()) {
                const Cmd* this_cmd = (*it)->m_cmd;
                if (this_cmd->chn == i) {
                    if (this_cmd->op() == CmdType::AmpFn || this_cmd->op() == CmdType::AmpVecFn) {
                        if (this_cmd->t + this_cmd->len > m_cur_t) {
                            std::pair<double, double> these_vals;
                            these_vals = (*it)->eval(m_cur_t - this_cmd->t);
                            amp = these_vals.first;
                            damp = these_vals.second;
                            state.amp = amp + damp;
                        }
                        else {
                            amp = this_cmd->final_val;
                            state.amp = amp;
                            it = active_cmds.erase(it); // no longer active
                            continue;
                        }
                    }
                    else if (this_cmd->op() == CmdType::FreqFn || this_cmd->op() == CmdType::FreqVecFn) {
                    }
                }
                ++it;
            }
        }
        // now deal with current command
        if (!cmd || cmd->chn != i) {
            // Compute and put into v
            FileCache::Entry* entry;
            m_fcache.get(fname, entry);
            FileManager fmngr = entry->m_fmngr;
            fmngr.get_data(&v, 1, BuffOp::Add, amp, damp);
        }
        else {
            do {
                //std::cout << (*cmd) << std::endl;
                if (cmd->op() == CmdType::FreqSet){
                }
                else if (cmd->op() == CmdType::FreqFn || cmd->op() == CmdType::FreqVecFn) {
                }
                else if (cmd->op() == CmdType::AmpSet) {
                    amp = cmd->final_val;
                    fname = cmd->fname;
                }
                else if (likely(cmd->op() == CmdType::AmpFn || cmd->op() == CmdType::AmpVecFn)) {
                    // first time seeing function command
                    fname = cmd->fname;
                    if (cmd->t + cmd->len > m_cur_t) {
                        // command still active
                        active_cmds.push_back(new activeCmd(cmd, t_serv_to_client));
                        std::pair<double, double> these_vals;
                        these_vals = active_cmds.back()->eval(m_cur_t - cmd->t);
                        amp = these_vals.first;
                        damp = these_vals.second;
                    }
                    else {
                        amp = cmd->final_val; // otherwise set to final value.
                    }
                }
                else if (unlikely(cmd->op() == CmdType::Phase)) {
                }
                else {
                    //encountered a non phase,amp,freq command
                    break;
                }
                cmd_next(); // increment cmd counter
                cmd = static_cast<const FCmd*>(get_cmd_curt()); // get command only if it's current
            } while (cmd && cmd->chn == i);
            FileCache::Entry* entry;
            m_fcache.get(fname, entry);
            FileManager fmngr = entry->m_fmngr;
            fmngr.get_data(&v, 1, BuffOp::Add, amp, damp);
            state.amp = amp + damp;
            state.fname = fname;
        }
    } // channel iteration
    // after done iterating channels
    m_cur_t++; // increment time
    //if (m_cur_t % uint32_t(1e6) == 0) {
    //  std::cout << "t: " << m_cur_t << std::endl;
    //}
    if (m_output_cnt % 19531250 == 0) { // 19531250
        // printf("m_output_cnt: %lu\n", m_output_cnt);
    }
    _mm512_store_si512(out, v);
}

void FStream::generate_page(){
    int16_t *out_ptr;
    while (true) {
        size_t sz_to_write;
        out_ptr = m_output.get_write_ptr(&sz_to_write);
        if (sz_to_write >= output_block_sz) {
            // If we are not waiting for a sequence, i.e. we are processing a sequence, or we are waiting
            // and the reader is less than wait_buf_sz bytes behind, we break out and generate data
            if (!wait_for_seq || m_output.check_reader(wait_buf_sz/2)) {
                break;
            }
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
        step(&out_ptr[i]);
        //std::cout << "stream stepped" << std::endl;
    }
    //std::cout << "Stream" << m_stream_num << " wrote " << *out_ptr << std::endl;
    m_output.wrote_size(output_block_sz); // alert reader that data is ready.
}

}
