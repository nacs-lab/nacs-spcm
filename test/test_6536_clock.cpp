#include <nacs-spcm/spcm.h>
#include <nacs-utils/log.h>
#include <nacs-utils/mem.h>
#include <nacs-utils/thread.h>
#include <nacs-utils/timer.h>

#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

#include <cstdlib>
#include <cmath>

#include <immintrin.h>
#include <sleef.h>

#include <atomic>
#include <thread>
#include <vector>

#include <exception>
#include <stdexcept>
#include <iostream>


using namespace NaCs;

static std::exception_ptr teptr = nullptr;

constexpr long long int sample_rate = 20ll * 1000000ll;
constexpr int cycle = 1024 / 32;

static constexpr __m512 tidxs = {0.0, 0.0625, 0.125, 0.1875, 0.25, 0.3125, 0.375, 0.4375,
                                 0.5, 0.5625, 0.625, 0.6875, 0.75, 0.8125, 0.875, 0.9375};
typedef short v32si __attribute__((vector_size(64)));
static constexpr v32si mask0 = {1, 3, 5, 7, 9, 11, 13, 15,
                                17, 19, 21, 23, 25, 27, 29, 31,
                                33, 35, 37, 39, 41, 43, 45, 47,
                                49, 51, 53, 55, 57, 59, 61, 63};
int main()
{
    NaCs::Spcm::Spcm hdl("/dev/spcm0");
    hdl.set_param(SPC_CLOCKMODE, SPC_CM_INTPLL); // Enables internal programmable quartz 1
    hdl.set_param(SPC_SAMPLERATE, 10000000); // Set internal sampling rate to 62.5 MHz
    hdl.set_param(SPC_CLOCKOUT, 1); // enable the clock output of the card
    //hdl.get_param(SPC_SAMPLERATE, &lSamplerate); // Read back the programmed sample rate and print
    //printf („Sample rate = %d\n“, lSamplerate); // it. Output should be „Sample rate = 62500000“
    printf("We clocking baby!");
    hdl.set_param(SPC_TRIG_ORMASK, SPC_TMASK_SOFTWARE);
    hdl.set_param(SPCM_X2_MODE,SPCM_XMODE_TRIGOUT);
    hdl.set_param(SPC_TIMEOUT,1000);
}

