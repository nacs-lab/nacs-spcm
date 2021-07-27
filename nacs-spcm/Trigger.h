//

#ifndef _NACS_SPCM_TRIGGER_H
#define _NACS_SPCM_TRIGGER_H

#include <nacs-utils/utils.h>

#include <functional>

namespace Spcm {

struct Trigger {
    Trigger(const char *name);
    int fd;
    std::function<std::pair<uint32_t, int64_t>(int)> cb;
};

}



#endif
