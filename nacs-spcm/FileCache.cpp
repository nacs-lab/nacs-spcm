//

#include "FileCache.h"

using namespace NaCs;

namespace Spcm {

template <typename T>
static inline size_t FCacheEntrySize(const T &entry)
{
    return 16 + entry->second.m_fmngr.sz / 8;
}

NACS_EXPORT() bool FileCache::get(std::string fname, Entry* & entry)
{
    auto it = m_cache.find(fname);
    if (it != m_cache.end()) {
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
    // Not found, create FileManager object
    FileManager fmngr = FileManager(fname);
    fmngr.load();
    auto it2 = m_cache.emplace(std::piecewise_construct, std::forward_as_tuple(std::move(fname)), std::forward_as_tuple(std::move(fmngr))).first;
    m_totalsz += FCacheEntrySize(it2);

    ssize_t age = it2->second.age.load(std::memory_order_relaxed);
    ssize_t new_age;
    do {
        if (age > 0) {
            new_age = age + 1;
        }
        else {
            new_age = 1;
        }
    } while (!it2->second.age.compare_exchange_weak(age, new_age, std::memory_order_relaxed));
    entry = &(it2->second);
    while (m_totalsz > m_szlim) {
        if (!ejectOldest()) {
            break;
        }
    }
    return true;
}
NACS_EXPORT() void FileCache::unref(const Entry &entry) const
{
    ssize_t global_age = -1;
    auto get_global_age = [&] {
        if (global_age == -1)
            global_age = m_age.fetch_add(1, std::memory_order_relaxed) + 1;
        return global_age;
    };

    ssize_t age = entry.age.load(std::memory_order_relaxed);
    ssize_t new_age;
    do {
        if (age > 1) {
            new_age = age - 1;
        }
        else {
            new_age = -get_global_age();
        }
    } while(!entry.age.compare_exchange_weak(age, new_age, std::memory_order_relaxed));
}
NACS_EXPORT() bool FileCache::ejectOldest()
{
    if (m_cache.empty()) {
        m_totalsz = 0;
        return false;
    }
    auto end = m_cache.end();
    auto entry = end;
    for (auto it = m_cache.begin(); it != end; ++it) {
        if (it ->second.age > 0)
            continue;
        if (entry == end || entry->second.age < it->second.age) {
            entry = it;
            continue;
        }
    }
    if (entry == end)
        return false;
    m_totalsz -= FCacheEntrySize(entry);
    //unref IData
    //void IDataCache::unref_names(uint64_t client_id, uint64_t* start, size_t sz)
    m_cache.erase(entry);
    return true;
}
}
