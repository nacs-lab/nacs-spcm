//

#ifndef _NACS_SPCM_SEQ_H
#define _NACS_SPCM_SEQ_H

#include "Stream.h"
#include "Config.h"
#include "FileStream.h"
#include <algorithm>

//#include "TotSequence.h"

using namespace NaCs;

namespace Spcm {

enum class Type : uint8_t {
    Bool = 1,
    Int32 = 2,
    Float64 = 3,
    Int64 = 4,
};
union Value {
        bool b;
        int32_t i32;
        double f64;
        int64_t i64;
};

struct Sequence {
public:
    Sequence(Value* values, std::vector<Type> types, bool is_valid);
    Sequence(Sequence&&) = default;
    //uint64_t obj_id;
    //uint32_t nconsts;
    //uint32_t nvalues;
    Value* m_values {nullptr}; // refers to value array held by parent TotSequence
    std::vector<Type> m_types; // refers to type array held by parent TotSequence
    //uint32_t code_len;
    bool m_is_valid{false};
    operator bool() const
    {
          return m_is_valid;
    }
    void toCmds(std::vector<Cmd> &preSend, std::vector<Cmd> &cmds, std::vector<FCmd> &preSendf, std::vector<FCmd> &cmdsf, int64_t &seq_len);
    void addPulse(uint32_t enabled, uint32_t id, uint32_t t_start,
                  uint32_t len, uint32_t endvalue, uint8_t functype,
                  uint8_t phys_chn, uint32_t chn, void (*fnptr)(void),
                  uint8_t is_file_chn, std::string file_name);
    double get_value(uint32_t idx) const;
    int64_t get_time(uint32_t idx) const;
    bool get_enabled(uint32_t idx) const;
    Config m_conf;
private:
    struct Pulse {
        uint32_t enabled; // whether pulse is enabled or not. also an index into value array.
        uint32_t id; //second layer of sorting
        uint32_t t_start; // these are all indices into value array
        uint32_t len;
        uint32_t endvalue;
        uint8_t functype;
        uint8_t phys_chn; // not really necessary...but it's only 1 byte.
        uint32_t chn;
        void (*fnptr)(void);
    };
    struct FPulse{
        uint32_t enabled; // whether pulse is enabled or not. also an index into value array.
        uint32_t id; //second layer of sorting
        uint32_t t_start; // these are all indices into value array
        uint32_t len;
        uint32_t endvalue;
        uint8_t functype;
        uint8_t phys_chn; // not really necessary...but it's only 1 byte.
        uint32_t chn;
        void (*fnptr)(void);
        std::string file_name;
    };
public:
    std::vector<Pulse> pulses;
    std::vector<FPulse> fpulses;
    //std::vector<uint32_t> used_chns;
    //std::vector<uint64_t> IData_ids;
    //std::vector<Type> types;
};

}

#endif
