
#include <nacs-spcm/spcm.h>
#include <nacs-utils/log.h>
#include <nacs-utils/mem.h>
#include <nacs-utils/thread.h>
#include <nacs-utils/timer.h>
#include <nacs-utils/number.h>
#include <nacs-utils/processor.h>

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

#include "calc_wave_helper.h"


#include <assert.h>

#include <cmath>
#include <iostream>
#include <random>
#include <vector>

using namespace NaCs;
using namespace NaCs::Spcm;


constexpr long long int sample_rate = 625ll * 1000000ll;
constexpr int cycle = 1024 / 32;

int main()
{
    int32 mem_size = 1024;
    
    NaCs::Spcm::Spcm hdl("/dev/spcm0");
    hdl.ch_enable(CHANNEL0);
    hdl.enable_out(0, true);
    hdl.set_amp(0, 2500);
    hdl.set_param(SPC_CLOCKMODE, SPC_CM_INTPLL);
    hdl.set_param(SPC_CARDMODE, SPC_REP_STD_SINGLE);
    hdl.set_param(SPC_MEMSIZE, mem_size);
    hdl.set_param(SPC_LOOPS, 10000);
    
    int16 vals[mem_size];
    for(int16 i = 0; i < mem_size; i++) {
        vals[i] = i*100;
    }
    int16 * buff_ptr = vals;
    
    hdl.def_transfer(SPCM_BUF_DATA, SPCM_DIR_PCTOCARD, 0,
                     (void*)buff_ptr, 0, 2 * mem_size);
    hdl.cmd(M2CMD_DATA_STARTDMA | M2CMD_DATA_WAITDMA);
//    hdl.check_error();

    hdl.cmd(M2CMD_CARD_START | M2CMD_CARD_ENABLETRIGGER | M2CMD_CARD_WAITREADY);
    //hdl.force_trigger();

    //hdl.check_error();
    uint32_t status;
    hdl.get_param(SPC_M2STATUS, &status);
    Log::log("Status: 0x%x\n", status);
    hdl.cmd(M2CMD_CARD_STOP);
    
//        hdl.cmd(M2CMD_DATA_STARTDMA | M2CMD_DATA_WAIT_DMA);
}
