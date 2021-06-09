//

#include "SeqCache.h"

using namespace NaCs;

namespace Spcm {
template <typename T>
static inline size_t entrySize(const T &entry)
{
    return 16 + entry->second.m_seq.code_len; // use code_len as a proxy
}

static inline void bypassData(uint8_t* &msg_bytes, uint32_t &sz) {
    // use this function to go through the IData
    // [n_interp_data: 4B][[ID: 8B][name: NUL-term-string][0:1B] / ([1:1B] [len: 4B][[data: 8B] x len]) x ninter_data]
    uint32_t n_interp_data;
    memcpy(&n_interp_data, msg_bytes, 4);
    msg_bytes += 4;
    sz -= 4;
    while (n_interp_data > 0) {// bypass ID
        msg_bytes += 8;
        sz -= 8;
        std::string IData_name((char*) msg_bytes);
        uint32_t str_size = IData_name.size() + 1; // account for null character
        msg_bytes += str_size;
        sz -= str_size;
        uint8_t is_sent;
        memcpy(&is_sent, msg_bytes, 1);
        msg_bytes += 1;
        sz -= 1;
        if (is_sent) {
            uint32_t data_sz;
            memcpy(&data_sz, msg_bytes, 4);
            msg_bytes += 4;
            sz -= 4;
            msg_bytes += data_sz * 8;
            sz -= data_sz * 8;
        }
        n_interp_data--;
    }
}

NACS_EXPORT() SeqCache::SeqCache(size_t szlim) :
m_szlim(szlim)
{
}

NACS_EXPORT() bool SeqCache::getAndFill(uint64_t client_id, uint64_t seq_id, uint8_t* &msg_bytes, uint32_t &sz, Entry* &entry, bool is_seq_sent){
    if (is_seq_sent) {
        return getAndFill(client_id, seq_id, msg_bytes, sz, entry);
    }
    // assume we are in case when only data is sent
    // ([0: 1B][n_non_consts: 4B][[data: 8B] x n_non_consts])
    // The 1B 0 should be handled externally.
    // retrieve entry
    if (!get(client_id, seq_id, entry)) {
        return false;
    }
    uint32_t n_non_const;
    memcpy(&n_non_const, msg_bytes, 4);
    msg_bytes += 4;
    sz -= 4;
    memcpy((*entry).m_seq.values + (*entry).m_seq.nconsts, msg_bytes, 8 * n_non_const);
    msg_bytes += 8 * n_non_const;
    sz -= 8 * n_non_const;
    return true;
}

NACS_EXPORT() bool SeqCache::getAndFill(uint64_t client_id, uint64_t seq_id, uint8_t* &msg_bytes, uint32_t &sz, Entry* &entry){
    // This version is called if seq is sent!
    if (get(client_id, seq_id, entry)){
        // now fill in value
        bypassData(msg_bytes, sz);
        // bypass object file, value array name, nconsts and nvalues
        msg_bytes += (*entry).m_seq.code_len;
        sz -= (*entry).m_seq.code_len;
        memcpy((*entry).m_seq.values, msg_bytes, 8 * entry->m_seq.nvalues);
        msg_bytes += 8 * entry->m_seq.nvalues;
        sz -= 8 * entry->m_seq.nvalues;
        return true;
    }
    std::pair<uint64_t, uint64_t> this_id(client_id, seq_id);
    TotSequence seq(*this, client_id, msg_bytes, sz);
    if (!seq) {
        return false; // sequence not valid cause it's missing IData
    }
    auto it = m_cache.emplace(std::piecewise_construct, std::forward_as_tuple(std::move(this_id)), std::forward_as_tuple(std::move(seq))).first;
    m_totalsz += entrySize(it);
    ssize_t age = it->second.age.load(std::memory_order_relaxed);
    ssize_t new_age;
    do {
        if (age > 0) {
            new_age = age + 1;
        }
        else {
            new_age = 1;
        }} while (!it->second.age.compare_exchange_weak(age, new_age, std::memory_order_relaxed));
    entry = &(it->second);
    while (m_totalsz > m_szlim) {
        if (!ejectOldest()) {
            break;
        }
    }
    return true;
}

NACS_EXPORT() bool SeqCache::get(uint64_t client_id, uint64_t seq_id, Entry* &entry)
{
     // This is the only function that mutate the cache. Other thread can only call unref
    // anId can only mutate the age.
    // Since none of the mutation invalidates map iterator we don't need a lock.
    // For this to be safe, we also need `unref` to not do any cache lookup.
    std::pair<uint64_t, uint64_t> this_id(client_id, seq_id);
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
    //                     std::forward_as_tuple(std::move(cmds))).first;
    //m_totalsz += entrySize(it);
    //while (m_totalsz > m_szlim) {
    //    if (!ejectOldest()) {
    //        break;
    //    }
    //}
    return false;
}

bool SeqCache::ejectOldest()
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
    m_totalsz -= entrySize(entry);
    //unref IData
    //void IDataCache::unref_names(uint64_t client_id, uint64_t* start, size_t sz)
    m_data_cache.unref_names(entry->first.first, entry->second.m_seq.IData_ids.data(), entry->second.m_seq.IData_ids.size());
    m_engine.free(entry->second.m_seq.obj_id);
    m_cache.erase(entry);
    return true;
}

NACS_EXPORT() void SeqCache::unref(const Entry &entry) const
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

NACS_EXPORT() bool SeqCache::hasSeq(uint64_t client_id, uint64_t seq_id) {
    std::pair<uint64_t, uint64_t> this_key{client_id, seq_id};
    return m_cache.count(this_key) >= 1;
}

NACS_EXPORT() SeqCache::TotSequence::TotSequence(SeqCache& cache, uint64_t client_id, uint8_t* &msg_bytes, uint32_t &sz) :
m_cache(cache)
{
    // [n_interp_data: 4B][[ID: 8B][name: NUL-term-string][0:1B] / ([1:1B] [len: 4B][[data: 8B] x len]) x ninter_data][objfile_size: 4B][object_file: objfile_size][value_array_name: NUL-term-string]
    // [nconsts: 4B][nvalues:4B][[data:8B] x nvalues]
    // [[types:1B] x nvalues];
    // [n_pulses: 4B][[start_time: 4B][len: 4B][endvalue: 4B][pulse_type: 1B][phys_chn_id: 1B][channel_id; 4B][funcname: NUL-term-string] x n_pulses]
    uint32_t n_interp_data;
    memcpy(&n_interp_data, msg_bytes, 4);
    msg_bytes += 4;
    sz -= 4;
    std::map<std::string, std::vector<uint64_t>> i_data_map;
    IData_ids.reserve(n_interp_data);
    uint32_t str_size;
    while (n_interp_data > 0) {
        uint64_t this_IData_id;
        memcpy(&this_IData_id, msg_bytes, 8);
        msg_bytes += 8;
        sz -= 8;
        std::string IData_name((char*) msg_bytes);
        str_size = IData_name.size() + 1; // account for null character
        msg_bytes += str_size;
        sz -= str_size;
        uint8_t is_sent;
        memcpy(&is_sent, msg_bytes, 1);
        msg_bytes += 1;
        sz -= 1;
        IDataCache::Entry* this_entry;
        if (is_sent) {
            uint32_t data_sz;
            memcpy(&data_sz, msg_bytes, 4);
            msg_bytes += 4;
            sz -= 4;
            if (m_cache.m_data_cache.get(client_id, this_IData_id, msg_bytes, data_sz, this_entry))
            {
                //std::pair<std::string, std::vector<uint64_t>> test(IData_name,this_entry->m_data);
                i_data_map.emplace(std::piecewise_construct,
                                   std::forward_as_tuple(std::move(IData_name)),
                                   std::forward_as_tuple(std::move(this_entry->m_data)));
                IData_ids.push_back(this_IData_id);
            }
            else {
                is_valid = false;
                return;
            }
            msg_bytes += data_sz * 8;
            sz -= data_sz * 8;
        }
        else {
            if (m_cache.m_data_cache.get(client_id, this_IData_id, this_entry))
            {
                //i_data_map.insert(std::pair<std::string, IDataCache::Entry>(IData_name, *this_entry));
                i_data_map.emplace(std::piecewise_construct,
                                   std::forward_as_tuple(std::move(IData_name)),
                                   std::forward_as_tuple(std::move(this_entry->m_data)));
                IData_ids.push_back(this_IData_id);
            }
            else {
                is_valid = false;
                return;
            }
        }
        n_interp_data--;
    }
    // now we are ready for object file
    code_len = 0;
    uint32_t obj_file_sz;
    memcpy(&obj_file_sz, msg_bytes, 4);
    msg_bytes += 4;
    sz -= 4;
    code_len += 4;
    std::function<uintptr_t(const std::string&)> resolver = [&i_data_map] (const std::string &name) -> uintptr_t {
        auto it = i_data_map.find(name);
        if (it != i_data_map.end()) {
            // found
            return (uintptr_t) (it->second).data();
        }
        return 0;
    };
    obj_id = m_cache.m_engine.load((char*)msg_bytes, obj_file_sz, resolver);
    msg_bytes += obj_file_sz;
    sz -= obj_file_sz;
    code_len += obj_file_sz;

    //value array name
    std::string val_array_name((char *) msg_bytes);
    str_size = val_array_name.size() + 1;
    msg_bytes += str_size;
    sz -= str_size;
    code_len += str_size;
    values = (Value*) m_cache.m_engine.get_symbol(val_array_name);

    //nconsts
    memcpy(&nconsts, msg_bytes, 4);
    msg_bytes += 4;
    sz -= 4;
    code_len += 4;

    //nvalues
    memcpy(&nvalues, msg_bytes, 4);
    msg_bytes += 4;
    sz -= 4;
    code_len += 4;

    //load in values to value array
    memcpy(values, msg_bytes, 8 * nvalues);
    msg_bytes += 8 * nvalues;
    sz -= 8 * nvalues;

    // fill in types
    uint8_t type;
    for (int i = 0; i < nvalues; i++) {
        memcpy(&type, msg_bytes, 1);
        msg_bytes += 1;
        sz -= 1;
        types.push_back(static_cast<Type>(type));
    };

    // pulses
    uint32_t n_pulses;
    memcpy(&n_pulses, msg_bytes, 4);
    msg_bytes += 4;
    sz -= 4;
    uint32_t t_start, len, end_val, chn_id, enabled, pulse_id;
    //std::string func_name;
    uint8_t phys_chn, functype;
    void(*fnptr)(void);
    while (n_pulses > 0) {
        //struct Pulse {
        //    uint32_t enabled
        //    uint32_t id;
        //    uint32_t t_start;
        //    uint32_t len;
        //    uint32_t endvalue;
        //    uint8_t functype;
        //    uint8_t phys_chn;
        //    uint32_t chn;
        //    void (*fnptr)(void);
        //} // [[enabled:4B][id:4B][start_time: 4B][len: 4B][endvalue: 4B][pulse_type: 1B][phys_chn_id: 1B][channel_id; 4B][funcname: NUL-term-string] x n_pulses]
        memcpy(&enabled, msg_bytes, 4);
        msg_bytes += 4;
        sz -= 4;
        
        memcpy(&pulse_id, msg_bytes, 4);
        msg_bytes += 4;
        sz -= 4;
        
        memcpy(&t_start, msg_bytes, 4);
        msg_bytes += 4;
        sz -= 4;

        memcpy(&len, msg_bytes, 4);
        msg_bytes += 4;
        sz -= 4;

        memcpy(&end_val, msg_bytes, 4);
        msg_bytes += 4;
        sz -= 4;

        memcpy(&functype, msg_bytes, 1);
        msg_bytes += 1;
        sz -= 1;

        memcpy(&phys_chn, msg_bytes, 1);
        msg_bytes += 1;
        sz -= 1;

        memcpy(&chn_id, msg_bytes, 4);
        msg_bytes += 4;
        sz -= 4;

        std::string funcname((char *) msg_bytes);
        str_size = funcname.size() + 1;
        msg_bytes += str_size;
        sz -= str_size;

        fnptr = (void(*)(void))m_cache.m_engine.get_symbol(funcname);

        /*pulses.push_back({
                .enabled = enabled;
                .id = pulse_id;
                .t_start = t_start;
                .len = len;
                .endvalue = end_val;
                .functype = functype;
                .phys_chn = phys_chn;
                .chn = chn_id;
                .fnptr = fnptr;
                }); */
        addPulse(enabled, pulse_id, t_start, len, end_val,
                 functype, phys_chn, chn_id, fnptr);
        n_pulses--;
}
    is_valid = true;
    m_cache.m_engine.reset_dyld();
}

NACS_EXPORT() Sequence& SeqCache::TotSequence::getSeq(uint32_t idx){
    if (idx >= seqs.size()) {
        // idx is not found, return invalid sequence
        return invalid_seq;
    }
    return seqs[idx];
}

NACS_EXPORT() void SeqCache::TotSequence::addPulse(uint32_t enabled, uint32_t id, uint32_t t_start,
                  uint32_t len, uint32_t endvalue, uint8_t functype,
                  uint8_t phys_chn, uint32_t chn, void (*fnptr)(void))
{
    //bool res = false;
    if (phys_chn >= seqs.size()) {
        seqs.emplace_back(&values, &types, true);
        //res = true;
    }
    seqs[phys_chn].addPulse(enabled, id, t_start, len, endvalue,
                            functype, phys_chn, chn, fnptr);
    //return res;
}

}