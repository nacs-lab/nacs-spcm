//

#include "IDataCache.h"

//using namespace NaCs;

namespace Spcm {
template <typename T>
static inline size_t IDataEntrySize(const T &entry)
{
    return 16 + entry->second.m_data.size() * 8;
}

NACS_EXPORT() bool IDataCache::get(uint64_t client_id, uint64_t data_id, const uint8_t *in_ptr, size_t in_sz, Entry* &entry) {
    if (get(client_id, data_id, entry)){
        return true;
    }
    std::pair<uint64_t, uint64_t> this_id(client_id, data_id);
    std::vector<uint64_t> data(in_ptr, in_ptr + in_sz);
    auto it = m_cache.emplace(std::piecewise_construct, std::forward_as_tuple(std::move(this_id)), std::forward_as_tuple(std::move(data))).first;
    m_totalsz += IDataEntrySize(it);
    ssize_t age = it->second.age.load(std::memory_order_relaxed);
        ssize_t new_age;
        do {
            if (age > 0) {
                new_age = age + 1;
            }
            else {
                new_age = 1;
            }
        } while (!it->second.age.compare_exchange_weak(age, new_age, std::memory_order_relaxed));
        entry = &(it->second);
    while (m_totalsz > m_szlim) {
        if (!ejectUnused()) {
            break;
        }
    }
    return true;
}

NACS_EXPORT() bool IDataCache::get(uint64_t client_id, uint64_t data_id, Entry* &entry)
{
     // This is the only function that mutate the cache. Other thread can only call unref
    // and can only mutate the age.
    // Since none of the mutation invalidates map iterator we don't need a lock.
    // For this to be safe, we also need `unref` to not do any cache lookup.
    std::pair<uint64_t, uint64_t> this_id(client_id, data_id);
    auto it = m_cache.find(this_id);
    if (it != m_cache.end()){
// In fact, no other thread should mutate anything other than `age` so we don't
        // need any synchronization when accessing `age`. We only need `relaxed` ordering
        // to ensure there's no data race.
        ssize_t age = it->second.age.load(std::memory_order_relaxed);
        ssize_t new_age;
        do {
            if (age > 0) {
                new_age = age + 1;
            }
            else {
                new_age = 1;
            }
        } while (!it->second.age.compare_exchange_weak(age, new_age, std::memory_order_relaxed));
        entry = &(it->second);
        return true;
    }
    // INSERT COMMAND FoRMATION HERE
    //auto cmds = SeqBuilder::fromBinary(m_tstep, m_fcenter, m_max_amp, (const uint8_t*)bytes.data(), bytes.size(), m_exectx.get()).schedule();
    //it = m_cache.emplace(std::piecewise_construct, std::forward_as_tuple(std::move(bytes)),
    //                    std::forward_as_tuple(std::move(cmds))).first;
    //m_totalsz += entrySize(it);
    //while (m_totalsz > m_szlim) {
    //    if (!ejectOldest()) {
    //        break;
    //    }
    // }
    return false;
}

NACS_EXPORT() bool IDataCache::ejectUnused()
{
    bool res = false;
    if (m_cache.empty()) {
        m_totalsz = 0;
        return false;
    }
    auto end = m_cache.end();
    //auto entry = end;
    for (auto it = m_cache.begin(); it != end; ++it) {
        if (it ->second.age > 0)
            continue;
        //if (entry == end || entry->second.age < it->second.age) {
        //    entry = it;
        //    continue;
        //}
        // erase
        m_totalsz -= IDataEntrySize(it);
        m_cache.erase(it);
        res = true;
    }
    //if (entry == end)
    //    return false;
    //m_totalsz -= entrySize(entry);
    //m_cache.erase(entry);
    return res;
}

NACS_EXPORT() void IDataCache::unref(const Entry &entry) const
{
    ssize_t age = entry.age.load(std::memory_order_relaxed);
    ssize_t new_age;
    do {
        if (age > 0) {
            new_age = age - 1;
        }
    } while(!entry.age.compare_exchange_weak(age, new_age, std::memory_order_relaxed));
}

NACS_EXPORT() void IDataCache::unref_names(uint64_t client_id, uint64_t* start, size_t sz) {
    std::pair<uint64_t, uint64_t> this_id;
    for (int i = 0; i < sz; i++) {
        this_id = std::make_pair(client_id, start[i]);
        auto it = m_cache.find(this_id);
        if (it == m_cache.end()) {
            // already ejected, really shouldn't be the case....
            continue;
        }
        unref(it->second);
    }
}

NACS_EXPORT() bool IDataCache::hasData(uint64_t client_id, uint64_t data_id) {
    std::pair<uint64_t, uint64_t> this_key{client_id, data_id};
    return m_cache.count(this_key) >= 1;
}
}
