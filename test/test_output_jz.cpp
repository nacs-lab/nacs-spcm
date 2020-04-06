
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



int main() {
    
    NaCs::Spcm::Spcm hdl("/dev/spcm0");
    hdl.ch_enable(CHANNEL0);
    hdl.enable_out(0, true);
    hdl.set_param(SPC_ENABLEOUT0, 1); // what is this for?
    hdl.set_amp(0, 2500);
    hdl.set_param(SPC_FILTER0, 0);

//    hdl.set_param(SPC_CLOCKMODE, SPC_CM_INTPLL);
    hdl.set_param(SPC_CARDMODE, SPC_REP_FIFO_SINGLE); // set the FIFO single replay mode
//    hdl.write_setup();
    
//    hdl.set_param(SPC_TRIG_ORMASK, SPC_TMASK_SOFTWARE);
    //   hdl.set_param(SPC_TRIG_ANDMASK, 0);
//    hdl.set_param(SPC_TRIG_CH_ORMASK0, 0);
//    hdl.set_param(SPC_TRIG_CH_ORMASK1, 0);
//    hdl.set_param(SPC_TRIG_CH_ANDMASK0, 0);
//    hdl.set_param(SPC_TRIG_CH_ANDMASK1, 0);

    //hdl.set_param(SPC_MEMSIZE, mem_size);
    //hdl.set_param(SPC_LOOPS, 10000);

    uint64 buff_size_s = 67108864/4;
    uint32 notify_size_s = 4096*32;
    std::cout << "buff_size_b: " << buff_size_s * 2 << "notify_size_b: " << notify_size_s * 2 << "\n";
    
    hdl.set_param(SPC_SAMPLERATE, 50000000);
    
    int16_t * buff_ptr = new int16_t[buff_size_s];

    hdl.def_transfer(SPCM_BUF_DATA, SPCM_DIR_PCTOCARD, 2*notify_size_s,
                     (void*)buff_ptr, 0, 2*buff_size_s);
    hdl.set_param(SPC_DATA_AVAIL_CARD_LEN, 2*buff_size_s);
    
    uint64_t user_len_b;
    uint64_t user_pos_b;
    hdl.get_param(SPC_DATA_AVAIL_USER_LEN, &user_len_b);
    hdl.get_param(SPC_DATA_AVAIL_USER_POS, &user_pos_b);
    std::cout << "START \n avail_user_len: " << user_len_b << "; avail_user_pos: " << user_pos_b << ";\n";

    int16_t phase_cnt = 0;
    auto gen_data = [&] {
        for(uint64_t i = user_pos_b / 2; i < (user_pos_b+user_len_b) / 2; i++) {
            uint64_t n = i & (buff_size_s-1);
            if(phase_cnt > 0)
                buff_ptr[n] = (int16_t) phase_cnt;
            else
                buff_ptr[n] = 0;
            phase_cnt++;
        }
    };

    gen_data();    
    hdl.cmd(M2CMD_DATA_STARTDMA | M2CMD_DATA_WAITDMA);
    hdl.set_param(SPC_TRIG_ORMASK, SPC_TMASK_SOFTWARE);
    hdl.cmd(M2CMD_CARD_START | M2CMD_CARD_ENABLETRIGGER);
    hdl.force_trigger();
    hdl.check_error();
        
    int cnt = 0;
    while (cnt < 1000) {
        hdl.get_param(SPC_DATA_AVAIL_USER_LEN, &user_len_b);
        hdl.get_param(SPC_DATA_AVAIL_USER_POS, &user_pos_b);            
        std::cout << "avail_user_len: " << user_len_b << "; avail_user_pos: " << user_pos_b << ";\n";
        if (user_len_b >= 2*notify_size_s) {
            /*user_len_b = (user_pos_b + user_len_b) & (2*buff_size_s-1);
            if (user_pos_b + user_len_b >= 2*buff_size_s){
                user_len_b = 2*buff_size_s - user_pos_b;
                std::cout << "hello\n";
                }*/
            gen_data();
            hdl.set_param(SPC_DATA_AVAIL_CARD_LEN, user_len_b);
            hdl.cmd(M2CMD_DATA_WAITDMA);
            hdl.check_error();
        }
        cnt++;
    }
}
