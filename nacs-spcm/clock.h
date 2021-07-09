//

#ifndef HELPERS_TEST_H
#define HELPERS_TEST_H

#include <nacs-utils/utils.h>
#include <nacs-utils/timer.h>

#include <map>
#include <string>

using namespace NaCs;

namespace Spcm {

// number of cycles since power-on
static NACS_INLINE uint64_t cycleclock() {
#if NACS_CPU_X86_64
    uint64_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return (high << 32) | low;
#elif NACS_CPU_X86
    uint64_t ret;
    __asm__volatile("rdtsc" : "=A"(ret));
    return ret;
#else
#warning No cycleclock() definition for your platform
    return 0;
#endif
}


}

#endif
