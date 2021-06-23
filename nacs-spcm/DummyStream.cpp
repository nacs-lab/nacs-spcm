//Written by Kenneth Wang in Oct 2020

#include "DummyStream.h"

#include <nacs-spcm/spcm.h>
#include <nacs-utils/log.h>
#include <nacs-utils/mem.h>
#include <nacs-utils/thread.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <cstdlib>
#include <cmath>

#include <immintrin.h>
#include <sleef.h>
#include <typeinfo>

#include <chrono>

using namespace NaCs;

using namespace std::chrono_literals;

namespace Spcm {

constexpr long long int sample_rate = 625ll * 1000000ll;
constexpr int cycle = 1024/32;

constexpr uint64_t max_phase = uint64_t(sample_rate * 10);
constexpr double phase_scale = 2 / double(max_phase);
constexpr double freq_scale = 0.1 / (sample_rate / 32); // 1 cycle in 32 samples at 625 MHz sampling rate. Converts a frequency at 10 times the real frequency, hence the 0.1.
constexpr double amp_scale = 6.7465185e9f;


NACS_INLINE void DummyStreamBase::clear_underflow()
{
    m_cmd_underflow.store(0, std::memory_order_relaxed);
    m_underflow.store(0, std::memory_order_relaxed);
}

inline const Cmd *DummyStreamBase::get_cmd_curt()
{
    // check get_cmd returns something valid and if so is t less than the current time
    if (auto cmd = get_cmd()){
        //std::cout << *cmd << std::endl;
        if (cmd->t <= m_cur_t) {
            return cmd;
        }
    }
    return nullptr;
}

inline const Cmd *DummyStreamBase::get_cmd()
{
    // returns command at m_cmd_read location. If it's hit max, reset to zero and get a new pointer
    if (m_cmd_read == m_cmd_max_read) {
        m_cmd_read = 0;
        m_cmd_read_ptr = m_commands.get_read_ptr(&m_cmd_max_read);
        // check if m_cmd_max_read == 0
        if (!m_cmd_max_read) {
            return nullptr;
        }
    }
    //std::cout << "pointer " << m_cmd_read_ptr << std::endl;
    //std::cout << "m_cmd_read " << m_cmd_read << std::endl;
    return &m_cmd_read_ptr[m_cmd_read];
}

inline void DummyStreamBase::cmd_next()
{
    // increment m_cmd_read in the if statement. If hit max_read, alert writer that reading is done
    if (++m_cmd_read == m_cmd_max_read) {
        m_commands.read_size(m_cmd_max_read);
    }
    //std::cout << "m_cmd_max_read: " << m_cmd_max_read << std::endl;
    //std::cout << "m_cmd_read: " << m_cmd_read << std::endl;
}

// TRIGGER STUFF. COME BACK TO
inline bool DummyStreamBase::check_start(int64_t t, uint32_t id)
{
    // The corresponding time must be visible when the id is loaded
    // We don't load the time and the id atomically so it is possible
    // that the time could be the next trigger.
    // It is highly unlikely and we should never have that
    // situation in real experiment.
    // If it really happens, we'll simply wait until the corresponding
    // trigger id to be visible too.
    m_cur_t = t;
    if (m_start_trigger.load(std::memory_order_acquire) < id)
        goto not_yet;
    {
        auto global_time = uint64_t(m_step_t * m_output_cnt);
        auto trigger_time =
            m_start_trigger_time.load(std::memory_order_relaxed);
        if (time_offset() + global_time < trigger_time) {
            goto not_yet;
        }
    }
    m_slow_mode.store(false, std::memory_order_relaxed);
    return true;
not_yet:
    m_slow_mode.store(true, std::memory_order_relaxed);
    return false;
}

NACS_INTERNAL NACS_NOINLINE const Cmd*
DummyStreamBase::consume_old_cmds(State *states)
{
    // consumes old commands (updates the states) and returns a pointer to a currently active command.
    // If only commmands in future or no commands, then return nullptr
    auto cmd = get_cmd();
    std::cout << "consume_old_cmds called" << std::endl;
    if (cmd->t != 0)
        m_cmd_underflow.fetch_add(1, std::memory_order_relaxed);
    do {
        if (cmd->t == m_cur_t)
            return cmd;
        if (cmd->t > m_cur_t)
            return nullptr; // get_cmd returns something in the future
        switch (cmd->op()){
        case CmdType::Meta:
            if (cmd->chn == (uint32_t)CmdMeta::Reset) {
                m_cur_t = 0; // set time to 0 if consuming a Reset
                printf("Stream mngr %u Stream num %i processing reset in consume_old_cmds\n", m_stream_mngr_num, m_stream_num);
            }
            else if (cmd->chn == (uint32_t)CmdMeta::ResetAll){
                clear_underflow();
                m_cur_t = 0;
                m_chns = 0;
                m_slow_mode.store(false,std::memory_order_relaxed);
                printf("Stream mngr %u Stream num %i processing resetAll in consume_old_cmds\n", m_stream_mngr_num, m_stream_num);
            }
            else if (cmd-> chn == (uint32_t)CmdMeta::TriggerEnd) {
                m_end_trigger_pending = cmd->final_val;
                printf("Stream mngr %u Stream num %i processing triggerEnd id %f in consume_old_cmds\n", m_stream_mngr_num, m_stream_num, cmd->final_val);
            }
            else if (cmd-> chn == (uint32_t)CmdMeta::TriggerStart) {
                printf("Stream mngr %u Stream num %i processing triggerStart id %f in consume_old_cmds\n", m_stream_mngr_num, m_stream_num, cmd->final_val);
                if (!check_start(cmd->t, cmd->final_val)) {
                    return nullptr;
                }
            }
            break;
        case CmdType::AmpSet:
            printf("Stream mngr %u Stream num %i processing ampSet to %f in consume_old_cmds\n", m_stream_mngr_num, m_stream_num, cmd->final_val);
            states[cmd->chn].amp = cmd->final_val; // set amplitude of state
            break;
        case CmdType::FreqSet:
            printf("Stream mngr %u Stream num %i processing freqSet to %f in consume_old_cmds\n", m_stream_mngr_num, m_stream_num, cmd->final_val);
            states[cmd->chn].freq = cmd->final_val;
            break;
        case CmdType::AmpFn:
        case CmdType::AmpVecFn:
            // cmd pointer only increments. Should be safe to initialize an active command here
            printf("Stream mngr %u Stream num %i processing ampFn in consume_old_cmds\n", m_stream_mngr_num, m_stream_num);
            if (cmd->t + cmd->len > m_cur_t) {
                // command still active
                active_cmds.push_back(new activeCmd(cmd));
                std::pair<double, double> these_vals;
                these_vals = active_cmds.back()->eval(m_cur_t - cmd->t);
                states[cmd->chn].amp = these_vals.first + these_vals.second;
            }
            else {
                states[cmd->chn].amp = cmd->final_val; // otherwise set to final value.
            }
            break;
        case CmdType::FreqFn:
        case CmdType::FreqVecFn:
            printf("Stream mngr %u Stream num %i processing freqFn in consume_old_cmds\n", m_stream_mngr_num, m_stream_num);
            if (cmd->t + cmd->len > m_cur_t) {
                // command still active
                active_cmds.push_back(new activeCmd(cmd));
                std::pair<double, double> these_vals;
                these_vals = active_cmds.back()->eval(m_cur_t - cmd->t);
                states[cmd->chn].freq = uint64_t(these_vals.first + these_vals.second);
            }
            else {
                states[cmd->chn].freq = cmd->final_val; // otherwise set to final value.
            }
            break;
        case CmdType::Phase:
            printf("Stream mngr %u Stream num %i processing phase to %f in consume_old_cmds\n", m_stream_mngr_num, m_stream_num, cmd->final_val);
            states[cmd->chn].phase = cmd->final_val; // possibly a scale factor needed.TODO
            break;
        case CmdType::ModChn:
            if (cmd->chn == Cmd::add_chn) {
                states[m_chns] = {0, 0, 0}; // initialize new channel
                m_chns++;
                printf("Stream mngr %u Stream num %i adding chn %i in consume_old_cmds\n", m_stream_mngr_num, m_stream_num, m_chns);
            }
            else {
                m_chns--;
                states[cmd->chn] = states[m_chns]; // move last_chn to place of deleted channel
                printf("Stream mngr %u Stream num %i deleting chn %i in consume_old_cmds\n", m_stream_mngr_num, m_stream_num, cmd->chn);
            }
            break;
        }
        cmd_next(); //after interpretting this command, increment pointer to next one.
    } while((cmd = get_cmd())); // keep on going until there are no more commands or one reaches the present
    return nullptr;
}
__attribute__((target("avx512f,avx512bw"), flatten))
NACS_EXPORT() void DummyStreamBase::step(State *states)
{
    // Key function
    const Cmd *cmd;
retry:
    // returns command at current time or before
    if ((cmd = get_cmd_curt())){
        if (unlikely(cmd->t < m_cur_t)) {
            cmd = consume_old_cmds(states); //consume past commands
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
                printf("Stream mngr %u Stream num %i processing reset\n", m_stream_mngr_num, m_stream_num);
                m_cur_t = 0;
            }
            else if (cmd->chn == (uint32_t)CmdMeta::ResetAll) {
                printf("Stream mngr %u Stream num %i processing resetAll\n", m_stream_mngr_num, m_stream_num);
                clear_underflow();
                m_cur_t = 0;
                m_chns = 0;
                m_slow_mode.store(false, std::memory_order_relaxed);
            }
            else if (cmd->chn == (uint32_t)CmdMeta::TriggerEnd) {
                printf("Stream mngr %u Stream num %i processing triggerEnd id %f\n", m_stream_mngr_num, m_stream_num, cmd->final_val);
                m_end_trigger_pending = cmd->final_val;
            }
            else if (cmd->chn == (uint32_t)CmdMeta::TriggerStart) {
                printf("Stream mngr %u Stream num %i processing triggerStart id %f\n", m_stream_mngr_num, m_stream_num, cmd->final_val);
                if (!check_start(cmd->t, cmd->final_val)){
                    cmd = nullptr;
                    goto cmd_out;
                }
            }
            cmd_next();
            goto retry; // keep on going if it's a meta command
        }
        else {
            while (unlikely(cmd->op() == CmdType::ModChn)) {
                if (cmd->chn == Cmd::add_chn) {
                    states[m_chns] = {0, 0, 0};
                    m_chns++;
                    printf("Stream mngr %u Stream num %i adding channel %i\n", m_stream_mngr_num, m_stream_num, m_chns);
                }
                else {
                    printf("Stream mngr %u Stream num %i deleting channel %i\n", m_stream_mngr_num, m_stream_num, cmd->chn);
                    m_chns--;
                    states[cmd->chn] = states[m_chns];
                }
                cmd_next();
                cmd = get_cmd_curt();
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
        if (!cur_end_trigger) {
            m_end_triggered.store(m_end_trigger_waiting, std::memory_order_relaxed);
            m_end_trigger_waiting = m_end_trigger_pending;
            if (m_end_trigger_pending) {
                set_end_trigger(nullptr);
            }
        }
    }
    else if (unlikely(m_end_trigger_pending)) {
        m_end_trigger_waiting = m_end_trigger_pending;
        m_end_trigger_pending = 0;
        set_end_trigger(nullptr);
    }
    // calculate actual output.
    // For testing purposes. At the moment keep the output simple.
    // __m512 v1 = _mm512_set1_ps(0.0f);
    // __m512 v2 = _mm512_set1_ps(0.0f);
    uint32_t _nchns = m_chns;
    if(!cmd){
        //std::cout << "This command is null" << std::endl;
    }
    else {
        //std::cout << (*cmd) << std::endl;
    }
    for (uint32_t i = 0; i < _nchns; i++){
        // iterate through the number of channels
        auto &state = states[i];
        int64_t phase = state.phase;
        double amp = state.amp;
        uint64_t freq = state.freq;
        uint64_t df = 0;
        double damp = 0;
        // check active commands
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
                    if (this_cmd->t + this_cmd->len > m_cur_t) {
                        std::pair<double, double> these_vals;
                        these_vals = (*it)->eval(m_cur_t - this_cmd->t);
                        freq = uint64_t(these_vals.first);
                        df = uint64_t(these_vals.second);
                        state.freq = freq + df;
                    }
                    else {
                        freq = this_cmd->final_val;
                        state.freq = freq;
                        it = active_cmds.erase(it); // no longer active
                        continue;
                    }
                }
            }
            ++it;
        }
        // now deal with current command
        if (!cmd || cmd->chn != i) {
            //std::cout << phase << std::endl;
            //std::cout << "freq: " << freq << std::endl;
            if (damp != 0)
            {// && freq != 70e7)
                //std::cout << "df: " << float(df * freq_scale) << std::endl;
                //Log::log("Amp: %f\n", amp);
                //std::cout << "damp: " << damp << std::endl;
            }
            //compute_single_chn(v1, v2, float(phase * phase_scale), float(freq * freq_scale), float(df * freq_scale), amp, damp);
            /*if (freq != 0) {
                std::cout << "int64_t " <<typeid(int64_t(2)).name() << std::endl;
                std::cout << "uint64_t: " << typeid(uint64_t(2)).name() << std::endl;
            std::cout << "phase type: " << typeid(phase).name() << std::endl;
            std::cout << "freq type: " << typeid(freq).name() << std::endl;
            std::cout << "df type: " << typeid(df).name() << std::endl;
            std::cout << "state phase: " <<typeid(state.phase).name() << std::endl;
            }*/
            phase = phase + freq * 32 + df * 32 / 2;
            //if (freq != 0) {
            //std::cout << "phase type after: " << typeid(phase).name() << std::endl;
            //}
            //phase = phase + freq * 2;
            //compute_single_chn(out1freq, out2freq, freq, df);
        }
        else {
            do {
                //std::cout << (*cmd) << std::endl;
                if (cmd->op() == CmdType::FreqSet){
                    //std::cout << "in freq set" << std::endl;
                    printf("Stream mngr %u Stream num %i processing FreqSet to %f\n", m_stream_mngr_num, m_stream_num, cmd->final_val);
                    freq = cmd->final_val;
                }
                else if (cmd->op() == CmdType::FreqFn || cmd->op() == CmdType::FreqVecFn) {
                    printf("Stream mngr %u Stream num %i processing FreqFn\n", m_stream_mngr_num, m_stream_num);
                    // first time seeing function command
                    if (cmd->t + cmd->len > m_cur_t) {
                        // command still active
                        active_cmds.push_back(new activeCmd(cmd));
                        std::pair<float, float> these_vals;
                        these_vals = active_cmds.back()->eval(m_cur_t - cmd->t);
                        freq = uint64_t(these_vals.first);
                        df = uint64_t(these_vals.second);
                    }
                    else {
                        freq = cmd->final_val; // otherwise set to final value.
                    }
                }
                else if (cmd->op() == CmdType::AmpSet) {
                    printf("Stream mngr %u Stream num %i processing AmpSet to %f\n", m_stream_mngr_num, m_stream_num, cmd-> final_val);
                    amp = cmd->final_val;
                }
                else if (likely(cmd->op() == CmdType::AmpFn || cmd->op() == CmdType::AmpVecFn)) {
                    printf("Stream mngr %u Stream num %i processing AmpFn\n", m_stream_mngr_num, m_stream_num);
                    // first time seeing function command
                    if (cmd->t + cmd->len > m_cur_t) {
                        // command still active
                        active_cmds.push_back(new activeCmd(cmd));
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
                    printf("Stream mngr %u Stream num %i processing Phase to %f\n", m_stream_mngr_num, m_stream_num, cmd->final_val);
                    phase = cmd->final_val; // may need to be changed
                }
                else {
                    //encountered a non phase,amp,freq command
                    break;
                }
                cmd_next(); // increment cmd counter
                cmd = get_cmd_curt(); // get command only if it's current
            } while (cmd && cmd->chn == i);
            if (damp != 0)
            {
                //std::cout << "damp: " << damp << std::endl;
            }
            //compute_single_chn(v1, v2, float(phase * phase_scale), float(freq * freq_scale), float(df * freq_scale), amp, damp);
            phase = phase + freq * 32 + df * 32 / 2;
            //test_compute_single_chn(out1freq, out2freq, freq, df);
            state.amp = amp + damp;
            state.freq = freq + df;
        }
        // deal with phase wraparound
        if (phase > 0)
        {
          phase -= max_phase * 4;
          while (unlikely(phase > 0)) {
              phase -= max_phase * 4;
          }
        }
        state.phase = phase;
    } // channel iteration
    // after done iterating channels
    m_cur_t++; // increment time
    //if (m_cur_t % uint32_t(1e6) == 0) {
    //  std::cout << "t: " << m_cur_t << std::endl;
    //}
    // __m512i v;
    // v = _mm512_permutex2var_epi16(_mm512_cvttps_epi32(v1), (__m512i)mask0,
    //                           _mm512_cvttps_epi32(v2));
    // _mm512_store_si512(out, v);
}

NACS_EXPORT() void DummyStreamBase::generate_page(State *states)
{
    // int16_t *out_ptr;
    // while (true) {
    //     size_t sz_to_write;
    //     out_ptr = m_output.get_write_ptr(&sz_to_write);
    //     if (sz_to_write >= output_block_sz) {
    //         break;
    //     }
    //     if (sz_to_write > 0) {
    //         m_output.sync_writer();
    //     }
    //     CPU::pause();
    // }
    //std::cout << "ready to write" << std::endl;
    // Now ready to write to output. Write in output_block_sz chunks
    for (uint32_t i = 0; i < output_block_sz; i += 32) {
        // for now advance one position at a time.
        m_output_cnt += 1;
        step(states);
        //std::cout << "stream stepped" << std::endl;
        // report state here
        for (int i = 0; i < m_chns; i++) {
            printf("Stream Manager %i, Stream number: %i Num channels: %i Channel %i: Freq: %lu, Amp: %f, Phase: %lu\n", m_stream_mngr_num, m_stream_num, m_chns, i, states[i].freq, states[i].amp, states[i].phase);
        }
        std::this_thread::sleep_for(1000ms);
    }
    //std::cout << "Stream" << m_stream_num << " wrote " << *out_ptr << std::endl;
    //m_output.wrote_size(output_block_sz); // alert reader that data is ready.
}

}
