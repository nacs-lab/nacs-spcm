// Written by Kenneth Wang Oct 2020

#include "StreamManager.h"

#include <nacs-utils/mem.h>
#include <nacs-utils/thread.h>

using namespace NaCs;

namespace Spcm {

template<typename T> inline void sort_cmd_chn(T begin, T end)
{
    // sort amp and freq commands by stream number and then by id within that channel
    return std::sort(begin, end, [] (auto &p1, auto &p2) {
            std::pair<uint32_t, uint32_t> chn_info1, chn_info2;
            chn_info1 = chn_map.at(p1.chn);
            chn_info2 = chn_map.at(p2.chn);
            if (chn_info1.first != chn_info2.first)
                return chn_info1.first < chn_info2.first;
            return chn_info1.second < chn_info2.second;
        });
}




}; // namespace brace
