//

#ifndef _NACS_SPCM_SEQCACHE_H
#define _NACS_SPCM_SEQCACHE_H

#include "Stream.h"
#include "Sequence.h"
#include "IDataCache.h"

#include <nacs-utils/ir.h>

#include <nacs-utils/llvm/execute.h>

#include <map>

using namespace NaCs;

namespace Spcm {

class SeqCache {
public:
    struct TotSequence {
    public:
        TotSequence(SeqCache& cache, uint64_t client_id, uint8_t* &msg_bytes, uint32_t &sz);
        TotSequence(TotSequence&&) = default;
        uint64_t obj_id;
        uint32_t nconsts;
        uint32_t nvalues;
        Value *values;
        uint32_t code_len;
        bool is_valid{false};
        operator bool() const
        {
            return is_valid;
        }
        Sequence& getSeq(uint32_t idx);
    //double getValue(uint32_t idx);
    //int64_t getTime(uint32_t idx);
    //std::vector<Pulse> pulses;
        std::vector<Sequence> seqs;
        std::vector<uint64_t> IData_ids;
        std::vector<Type> types;
        SeqCache& m_cache;
    private:
        void addPulse(uint32_t enabled, uint32_t id, uint32_t t_start,
                  uint32_t len, uint32_t endvalue, uint8_t functype,
                  uint8_t phys_chn, uint32_t chn, void (*fnptr)(void));
        Sequence invalid_seq{nullptr, nullptr, false};
        friend class SeqCache;
    };
    struct Entry {
        TotSequence m_seq;
        Entry(TotSequence seq)
            : m_seq(std::move(seq)),
              age(1)
        {
        }
        Entry(const Entry&) = delete;
        Entry(Entry&&) = default;
    private:
        // >0 means refcount
        // <0 means age
        mutable std::atomic<ssize_t> age;
        friend class SeqCache;
    };

    SeqCache(size_t szlim);
    bool getAndFill(uint64_t client_id, uint64_t seq_id, uint8_t* &msg_bytes, uint32_t &sz, Entry* &entry, bool is_seq_sent);
    bool getAndFill(uint64_t client_id, uint64_t seq_id, uint8_t* &msg_bytes, uint32_t &sz, Entry* &entry);
    bool get(uint64_t client_id, uint64_t seq_id, uint8_t* &msg_bytes, uint32_t &sz, Entry* &entry);
    void unref(const Entry &entry) const;
    bool hasSeq(uint64_t client_id, uint64_t seq_id);

private:
    bool ejectOldest();
    bool get(uint64_t client_id, uint64_t seq_id, Entry* &entry);
    //const double m_tstep;
    //const double m_fcenter;
    //const double m_max_amp;
    const size_t m_szlim;
    size_t m_totalsz{0};
    std::map<std::pair<uint64_t, uint64_t>, Entry> m_cache{};
    mutable std::atomic<ssize_t> m_age __attribute__((aligned(64))){0};
    LLVM::Exe::Engine m_engine{};
    IDataCache m_data_cache{128 * 1000ll * 1000ll}; // pretty arbitrary size limit for now
    //std::unique_ptr<IR::ExeContext> m_exectx;
};

}

#endif
