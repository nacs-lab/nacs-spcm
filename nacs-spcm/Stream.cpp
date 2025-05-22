//Written by Kenneth Wang in Oct 2020

#include "Stream.h"
#include "StreamManager.h"

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

using namespace NaCs;

namespace Spcm{

template<typename T>
static NACS_INLINE void accum_nonzero(T &out, T in, float s)
{
    if (__builtin_constant_p(s) && s == 0)
        return;
    out += in * s;
}

//constexpr long long int sample_rate = 625ll * 1000000ll;
constexpr int cycle = 1024/32;

//constexpr uint64_t max_phase = uint64_t(sample_rate * 10);
//constexpr double phase_scale = 2 / double(max_phase); // convert from "phase_cnt" which is tracked by state.phase to phase in units of pi radians that compute_single_chn wants.
//constexpr double phase_scale_client = 625e7; // converts from 0 to 1 scale to phase_cnt
constexpr double freq_scale_client = 10; // converts from real frequency to freq_cnt.
//constexpr double freq_scale = 0.1 / (sample_rate / 32); // 1 cycle in 32 samples at 625 MHz sampling rate. Converts a frequency at 10 times the real frequency, hence the 0.1.
//constexpr double amp_scale = 6.7465185e9f / 8;

//__m512 is a vector type that can hold 16 32 bit floats
static constexpr __m512 tidxs = {0.0, 0.0625, 0.125, 0.1875, 0.25, 0.3125, 0.375, 0.4375,
                                 0.5, 0.5625, 0.625, 0.6875, 0.75, 0.8125, 0.875, 0.9375};


typedef short v32si __attribute__((vector_size(64)));
static constexpr v32si mask0 = {1, 3, 5, 7, 9, 11, 13, 15,
                                17, 19, 21, 23, 25, 27, 29, 31,
                                33, 35, 37, 39, 41, 43, 45, 47,
                                49, 51, 53, 55, 57, 59, 61, 63};
// d is a vector of phases
__attribute__((target("avx512f,avx512bw"), flatten))
__m512 xsinpif_pi(__m512 d)
{
    __m512i q = _mm512_cvtps_epi32(d);
    d = d - _mm512_cvtepi32_ps(q);

    __m512 s = d * d;

    auto neg = _mm512_test_epi32_mask(q, _mm512_set1_epi32(1));
    d = (__m512)_mm512_mask_xor_epi32((__m512i)d, neg, (__m512i)d, _mm512_set1_epi32(0x80000000));

    auto u = 0.024749093f * s - 0.19045785f;
    u = u * s + 0.8117177f;
    u = u * s - 1.6449335f;
    return (s * d) * u + d;
}

// Amplitude normalized to 6.7465185f9 (2^31 - 1) * pi
// Phase is in unit of pi
// Frequency of 1 means one full cycle per 32 samples. At 625 MHz sampling rate, this is 19.531250 MHz
__attribute__((target("avx512f,avx512bw"), flatten))
void compute_single_chn(__m512 &v1, __m512 &v2, float phase, float freq,
                        float df, float amp, float damp)
{
    //int64_t phase_cnt = *phase;
    //float phase_f = float(double(phase_cnt * phase_scale));
    __m512 phase_v1 = phase + freq * tidxs; // first 16 samples
    __m512 phase_v2 = phase + freq * (tidxs + 1); // next 16 samples with no df
    accum_nonzero(phase_v1, tidxs, df / 2);
    accum_nonzero(phase_v2, (tidxs + 1), df / 2);
    __m512 amp_v1 = _mm512_set1_ps(amp);
    __m512 amp_v2 = _mm512_set1_ps(amp + damp / 2);
    accum_nonzero(amp_v1, tidxs, damp / 2); // accumulate half amplitude in one go
    accum_nonzero(amp_v2, tidxs, damp / 2); // accumulate next half
    //float amp_v1 = amp;
    //float amp_v2 = amp;
    v1 += xsinpif_pi(phase_v1) * amp_v1;
    v2 += xsinpif_pi(phase_v2) * amp_v2;
    //*phase = *phase + uint64_t(freq / freq_scale) * 32;
    //if (*phase > 0) {
    //    *phase -= max_phase * 4;
    //    while (unlikely(*phase > 0)) {
    //        *phase -= max_phase * 4;
    //    }
    //}
}

__attribute__((target("avx512f,avx512bw"), flatten))
void compute_single_chn(__m512 &v1, __m512 &v2, float phase, float freq,
                        float df, __m512 &amp1, __m512 &amp2)
{
    __m512 phase_v1 = phase + freq * tidxs; // first 16 samples
    __m512 phase_v2 = phase + freq * (tidxs + 1); // next 16 samples with no df
    accum_nonzero(phase_v1, tidxs, df / 2);
    accum_nonzero(phase_v2, (tidxs + 1), df / 2);
    //__m512 amp_v1 = _mm512_set1_ps(amp);
    //__m512 amp_v2 = _mm512_set1_ps(amp + damp / 2);
//accum_nonzero(amp_v1, tidxs, damp / 2); // accumulate half amplitude in one go
    //accum_nonzero(amp_v2, tidxs, damp / 2); // accumulate next half
    v1 += xsinpif_pi(phase_v1) * amp1;
    v2 += xsinpif_pi(phase_v2) * amp2;
}


void test_compute_single_chn(int& out1, int& out2, int val, int dval) {
    out1 += val;
    out2 = out2 + val + dval;
}

NACS_EXPORT() const char *Cmd::name() const
{
    // gives the name of the Cmd
    switch(op()){
    case CmdType::AmpSet:
        return "ampSet";
    case CmdType::AmpFn:
        return "ampFn";
    case CmdType::AmpVecFn:
        return "ampVecFn";
    case CmdType::FreqSet:
        return "freqSet";
    case CmdType::FreqFn:
        return "freqFn";
    case CmdType::FreqVecFn:
        return "freqVecFn";
    case CmdType::Phase:
        return "phase";
    case CmdType::ModChn:
        if (chn == add_chn)
            return "add_chn";
        return "del_chn";
    case CmdType::Meta:
        if (chn == (uint32_t)CmdMeta::Reset)
            return "reset";
        if (chn == (uint32_t)CmdMeta::ResetAll)
            return "reset_all";
        if (chn == (uint32_t)CmdMeta::TriggerEnd)
            return "trigger_end";
        if (chn == (uint32_t)CmdMeta::TriggerStart)
            return "trigger_start";
    default:
        return "(unknown)";
    }
}

NACS_EXPORT() std::ostream &operator<<(std::ostream &stm, const Cmd &cmd)
{
    // defines how a command behaves with std::cout << Cmd for instance
    stm << cmd.name() << "(t =" << cmd.t;
    if (cmd.op() == CmdType::Meta &&
        (cmd.chn == (uint32_t)CmdMeta::TriggerEnd || cmd.chn == (uint32_t)CmdMeta::TriggerStart))
        stm << ", id=" << cmd.final_val;
    if (cmd.op() == CmdType::ModChn && cmd.chn != Cmd::add_chn)
        stm << ", chn=" << cmd.chn;
    if (cmd.op() == CmdType::FreqSet || cmd.op() == CmdType::AmpSet || cmd.op() == CmdType::Phase)
        stm << ", chn=" << cmd.chn << ", val=" << cmd.final_val << ", len=" << cmd.len;
    if (cmd.op() == CmdType::AmpFn || cmd.op() == CmdType::FreqFn ||
        cmd.op() == CmdType::AmpVecFn || cmd.op() == CmdType::FreqVecFn)
        stm << ", chn=" << cmd.chn << ", final_val=" << cmd.final_val << ", len=" << cmd.len;
    stm << ")";
    return stm;
}

NACS_EXPORT() void Cmd::dump() const
{
    std::cerr << *this << std::endl;
}

NACS_EXPORT() std::ostream &operator<<(std::ostream &stm, const std::vector<Cmd> &cmds)
{
    for (auto &cmd: cmds)
        stm << cmd << std::endl;
    return stm;
}
__attribute__((target("avx512f,avx512dq")))
std::pair<double, double> activeCmd::eval(int64_t t) {
    // t is time from beginning of pulse
    double val, dval;
    /*if (m_cmd->op() == CmdType::AmpVecFn || m_cmd->op() == CmdType::FreqVecFn) {
        // assume all values are precalculated
        val = vals[t];
        dval = vals[t + 1] - vals[t];
    }
    else if (m_cmd->op() == CmdType::AmpFn || m_cmd->op() == CmdType::FreqFn) {
        // check if t and t + 1 is evaluated
        while (vals.size() < (t + 2)) {
            double thisval;
            thisval = ((double(*)(int64_t))(m_cmd->fnptr))((int64_t) (vals.size() * t_serv_to_client));
            vals.push_back(thisval);
        }
        val = vals[t];
        dval = vals[t + 1] - vals[t];
    }
    else {
        val = 0;
        dval = 0; // default behavior
        }*/
    if (is_vec) {
        if (time_base == t && nsteps > 1) {
            nsteps--;
            time_base++;
            val = buffer[7 - nsteps];
            dval = buffer[8 - nsteps] - val;
        }
        else if (time_base == t && nsteps == 1) {
            //nsteps--;
            time_base++;
            val = buffer[7];
            // calculate next batch
            auto ts __attribute__((aligned(64))) = (double(time_base) + _mm512_load_pd(times) * 8) * t_serv_to_client;
            auto func = (void (*)(double*, const double*))ramp_func;
            func(buffer, (const double*)&ts);
            nsteps = 8;
            dval = buffer[0] - val;
        }
        else {
            // just recalculate. Probably won't hit this branch at all except for the very first calculation
            auto ts __attribute__((aligned(64))) = (double(t) + _mm512_load_pd(times) * 8) * t_serv_to_client;
            auto func = (void (*)(double*, const double*))ramp_func;
            func(buffer, (const double*)&ts);
            nsteps = 7;
            time_base = t + 1;
            val = buffer[0];
            dval = buffer[1] - val;
        }
    }
    else {
        // scalar version
        if (time_base == t && nsteps == 2) {
            // previous value had been calculated and is in position 1 in the buffer.
            nsteps--;
            time_base++;
            val = buffer[1];
            auto func = (double (*)(double))ramp_func;
            buffer[0] = func(double(time_base) * t_serv_to_client);
            dval = buffer[0] - val;
        }
        else if (time_base == t && nsteps == 1) {
            // previous value is in position 0 in the buffer and has been calculated.
            nsteps++;
            time_base++;
            val = buffer[0];
            auto func = (double (*)(double))ramp_func;
            buffer[1] = func(double(time_base) * t_serv_to_client);
            dval = buffer[1] - val;
        }
        else {
            // need to recalculate
            nsteps = 2;
            auto func = (double (*)(double))ramp_func;
            buffer[0] = func(double(t) * t_serv_to_client);
            time_base = t + 1;
            buffer[1] = func(double(time_base) * t_serv_to_client);
            val = buffer[0];
            dval = buffer[1] - val;
        }
    }
    return std::make_pair(val, dval);
}

inline void StreamBase::reqRestart(uint32_t id) {
    auto res = m_stm_mngr.reqRestart(id);
}

NACS_INLINE void StreamBase::clear_underflow()
{
    m_cmd_underflow.store(0, std::memory_order_relaxed);
    m_underflow.store(0, std::memory_order_relaxed);
}

inline const Cmd *StreamBase::get_cmd_curt()
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

inline const Cmd *StreamBase::get_cmd()
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

inline void StreamBase::cmd_next()
{
    // increment m_cmd_read in the if statement. If hit max_read, alert writer that reading is done
    if (++m_cmd_read == m_cmd_max_read) {
        m_commands.read_size(m_cmd_max_read);
    }
    //std::cout << "m_cmd_max_read: " << m_cmd_max_read << std::endl;
    //std::cout << "m_cmd_read: " << m_cmd_read << std::endl;
}

// TRIGGER STUFF. COME BACK TO
inline bool StreamBase::check_start(int64_t t, uint32_t id)
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
        auto global_time = m_output_cnt;
        auto trigger_time =
            m_start_trigger_time.load(std::memory_order_relaxed);
        if (time_offset() + global_time < trigger_time) {
            //printf("not yet after receiving trigger\n");
            goto not_yet;
        }
        else if (time_offset() + global_time > trigger_time) {
            printf("Noticed trigger too late %lu, controller_cnt: %lu\n", time_offset() + global_time, m_stm_mngr.getControllerOutputCnt());
            // request card restart which will also notify the client of the bad sequence.
            reqRestart(id);
        }
    }
    m_slow_mode.store(false, std::memory_order_relaxed);
    //printf("Processed trigger\n");
    return true;
not_yet:
    //printf("waiting for trigger\n");
    m_slow_mode.store(true, std::memory_order_relaxed);
    return false;
}

// template <uint32_t max_chns>
// NACS_INTERNAL NACS_NOINLINE const Cmd*
// Stream<max_chns>::consume_old_cmds(State *states)
// {
    
// }

NACS_EXPORT() void StreamBase::consume_all_cmds()
{
    // This function consumes all commands in the command buffer.
    while(get_cmd()) {
        cmd_next();
    }
}

// template <uint32_t max_chns>
// __attribute__((target("avx512f,avx512bw"), flatten))
// NACS_EXPORT() void Stream<max_chns>::step(int16_t *out, State *states)
// {

// }

// template <uint32_t max_chns>
// NACS_EXPORT() void Stream<max_chns>::generate_page(State *states)


}
