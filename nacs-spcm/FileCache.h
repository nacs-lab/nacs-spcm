//

#ifndef _NACS_SPCM_FILECACHE_H
#define _NACS_SPCM_FILECACHE_H

#include <nacs-spcm/FileManager.h>
#include <atomic>

using namespace NaCs;

namespace Spcm {

class FileCache {
public:
    struct Entry {
        FileManager m_fmngr;
        Entry(FileManager data)
            : m_fmngr(std::move(data)),
              age(1)
        {
        }
        Entry(const Entry&) = delete;
        Entry(Entry&&) = default;
    private:
        // > 0 means refcount
        // < 0 means age
        mutable std::atomic<ssize_t> age;
        friend class FileCache;
    };
    FileCache(size_t szlim) : m_szlim(szlim)
    {
    }

    bool get(std::string fname, Entry* & entry);
    void unref(const Entry &entry) const;
    bool ejectOldest();
private:
    const size_t m_szlim;
    size_t m_totalsz{0};
    std::map<std::string, Entry> m_cache{};
    mutable std::atomic<ssize_t> m_age __attribute__((aligned(64))){0};
};

}

#endif
