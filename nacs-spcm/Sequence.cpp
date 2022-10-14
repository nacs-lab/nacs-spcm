//

/* int64_t t; // start time for command
    int64_t t_client; // time for client and that the function pointer takes
    uint32_t id;
    uint8_t _op:op_bits; // op should only contain op_bits amount of information.
    uint32_t chn:chn_bits;
    double final_val; // final value at end of command.
    double len = 0; // length of pulse
    void(*fnptr)(void) = nullptr; // function pointer */

#include "Sequence.h"
#include <cassert>

using namespace NaCs;

namespace Spcm {

NACS_EXPORT() Sequence::Sequence(Value* values, std::vector<Type> types, bool is_valid) :
                       m_values(values),
                       m_types(types),
                       m_is_valid(is_valid)
{
}

NACS_EXPORT() std::vector<Cmd> Sequence::toCmds(std::vector<Cmd> &preSend, int64_t &seq_len) {
    //printf("at address: %p\n", (*m_values));
    /*printf("types size: %u", m_types.size());
    printf("v0: %li\n", m_values[0].i64);
    printf("v1: %li\n", m_values[1].i64);
    printf("v2: %u\n", m_values[2].b);
    printf("v3: %f\n", m_values[3].f64);
    printf("v4: %f\n", m_values[4].f64);
    printf("v5: %u\n", m_values[5].b);
    printf("v6: %f\n", m_values[6].f64);
    printf("v7: %li\n", m_values[7].i64);
    printf("v8: %f\n", m_values[8].f64);
    */

    //printf("types at address: %p\n", m_types);
// go through pulses, add them to cmd_vector and then sort.
    std::vector<uint32_t> active_chns;
    std::vector<Cmd> cmds;
    if (pulses.size() == 0) {
        return cmds;
    }
    cmds.reserve(pulses.size());
    int64_t t, t_sample;
    uint32_t chn;
    double len, final_val;
    //printf("pulses sz: %u\n", pulses.size());
    for (int i = 0; i < pulses.size(); i++) {
        /*if (pulses[i].enabled != uint32_t(-1)) {
            printf("pulses[%i].enabled: array_idx: %u, val: %d, raw_val: %f", i, pulses[i].enabled, get_enabled(pulses[i].enabled), get_value(pulses[i].enabled));
            }*/
        if (pulses[i].enabled != uint32_t(-1) && get_value(pulses[i].enabled) == 0)
        {
            continue;
        }
        t = get_time(pulses[i].t_start);
        t_sample = t * 625 / 1e6; // t / 1e12 * 625e6
        if (pulses[i].len == uint32_t(-1))
        {
            // May be a AmpSet pulse
            if (pulses[i].functype == uint8_t(CmdType::AmpSet)) {
                len = t_sample % 32;
            }
            else {
                len = 0;
            }
        }
        else
        {
            len = get_value(pulses[i].len) * 625/ (32e6); // convert to AWG time
        }
        final_val = get_value(pulses[i].endvalue);
        //printf("v%i: %f\n", i, final_val);
        chn = pulses[i].chn;
        auto it = active_chns.begin();
        for (; it != active_chns.end(); ++it) {
            if (*it == chn) {
                break;
            }
        }
        if (it == active_chns.end()) {
            active_chns.push_back(chn);
        }
        if (t > seq_len) {
            seq_len = t;
        }
        cmds.push_back({
                .t = t_sample / 32,
                    .t_client = t,
                    .id = pulses[i].id,
                    ._op = pulses[i].functype,
                    .chn = chn,
                    .final_val = final_val,
                    .len = len,
                .fnptr = pulses[i].fnptr
            });
    }
    for (int i = 0; i < active_chns.size(); ++i) {
        preSend.push_back(Cmd::getAddChn(0,0,0, active_chns[i]));
    }
    std::sort(cmds.begin(), cmds.end(), [&] (auto &p1, auto &p2) {
        if (p1.t_client < p2.t_client)
            return true;
        if (p1.t_client > p2.t_client)
            return false;
        return p1.id < p2.id;
    });
    seq_len = seq_len * 625 / (32e6);
    //printf("Now printing cmds\n");
    //for (int i = 0; i < cmds.size(); i++) {
    //    std::cout << cmds[i] << std::endl;
    //}
    return cmds;
}

NACS_EXPORT() void Sequence::addPulse(uint32_t enabled, uint32_t id,
                                      uint32_t t_start, uint32_t len,
                                      uint32_t endvalue, uint8_t functype,
                                      uint8_t phys_chn, uint32_t chn, void (*fnptr)(void))
{
    pulses.push_back({
            .enabled = enabled,
                .id = id,
                .t_start = t_start,
                .len = len,
                .endvalue = endvalue,
                .functype = functype,
                .phys_chn = phys_chn,
                .chn = chn,
            .fnptr = fnptr
        });

}

NACS_EXPORT() double Sequence::get_value(uint32_t idx) const
{
    auto type = m_types[idx];
    auto value = m_values[idx];
    //printf("type: %u\n", type);
    switch (type) {
    case Type::Bool: {
        //printf("getting boolean\n");
        return value.b;
    }
    case Type::Int32:
        return value.i32;
    case Type::Float64:
        //printf("getting float\n");
        return value.f64;
    case Type::Int64:
        return (double)value.i64;
    default:
        return 0;
    }
}

NACS_EXPORT() int64_t Sequence::get_time(uint32_t idx) const
{
    //assert((*m_types)[idx] == Type::Int64);
    return m_values[idx].i64;
}

NACS_EXPORT() bool Sequence::get_enabled(uint32_t idx) const
{
    //assert((*m_types)[idx] == Type::Bool);
    //printf("Type of enabled v: %d\n", m_types[idx]);
    return m_values[idx].b;
}

}
