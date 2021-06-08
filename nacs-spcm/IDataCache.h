//

#ifndef _NACS_SPCM_IDATACACHE_H
#define _NACS_SPCM_IDATACACHE_H

//#include "Stream.h"

//#include <nacs-utils/ir.h>

#include <nacs-utils/thread.h>

#include <map>
#include <vector>
#include <atomic>

//using namespace NaCs;

namespace Spcm {

class IDataCache {
public:
    struct Entry {
        std::vector<uint64_t> m_data;
        Entry(std::vector<uint64_t> data)
            : m_data(std::move(data)),
              age(1)
        {
        }
        Entry(const Entry&) = delete;
        Entry(Entry&&) = default;
    private:
        // >0 means refcount
        // <0 means age
        mutable std::atomic<ssize_t> age;
        friend class IDataCache;
    };

    IDataCache(size_t szlim) :m_szlim(szlim)
    {
    }
    //bool store(std::string name, uint64_t* start, size_t sz);
    bool get(uint64_t client_id, uint64_t data_id, uint8_t *in_ptr, size_t sz, Entry* &entry);
    bool get(uint64_t client_id, uint64_t data_id, Entry* & entry);
    void unref(const Entry &entry) const;
    bool hasData(uint64_t client_id, uint64_t data_id);
    bool ejectUnused();
    void unref_names(uint64_t client_id, uint64_t* start, size_t sz);
private:
    //const double m_tstep;
    //const double m_fcenter;
    //const double m_max_amp;
    const size_t m_szlim;
    size_t m_totalsz{0};
    std::map<std::pair<uint64_t, uint64_t>, Entry> m_cache{};
    mutable std::atomic<ssize_t> m_age __attribute__((aligned(64))){0};
    //LLVM::Exe::Engine m_engine{};
    //std::unique_ptr<IR::ExeContext> m_exectx;
};

}

#endif
