
#include <nacs-spcm/spcm.h>
//#include <nacs-utils/log.h>
//#include <nacs-utils/mem.h>
#include <nacs-utils/thread.h>
//#include <nacs-utils/timer.h>
//#include <nacs-utils/number.h>
//#include <nacs-utils/processor.h>

#include <stdint.h>
//#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

#include <cstdlib>
#include <cmath>

//#include <immintrin.h>
//#include <sleef.h>

#include <atomic>
#include <thread>
#include <mutex>
//#include "calc_wave_helper.h"


//#include <assert.h>

#include <cmath>
#include <iostream>
#include <random>
#include <vector>


using namespace NaCs;
using namespace NaCs::Spcm;


class Stream {
//    NaCs::SpinLock mtx;
//    std::mutex mtx;
    std::thread data_thread;
    std::atomic<uint64_t> avail_data_s;
    uint64_t data_cursor_s;
    int16_t* buff_ptr;
    uint64_t buff_sz_s;
    
public:    
    Stream(uint64_t buff_sz_s){
        //lock = false;
        buff_ptr = new int16_t[buff_sz_s];
        this->buff_sz_s = buff_sz_s;
        avail_data_s = 0;
        data_cursor_s = 0;
        data_thread = std::thread(&Stream::gen_data, this);
    }
    uint64_t get_s() {
        //return avail_data_s;
        return avail_data_s.load(std::memory_order_release);;
    }
    void set_s(uint64_t s) {
        //this->mtx.lock();
        //avail_data_s = avail_data_s - s;
        //this->mtx.unlock();
        avail_data_s.store(avail_data_s - s, std::memory_order_acquire);
    }

private:
    void gen_data() {
        int16_t phase_cnt = 0;
        int16_t refresh_b = 1024;
        // write in 64byte pages
        while(true) {
            //this->mtx.lock();
            if (this->avail_data_s.load(std::memory_order_release) + refresh_b/2 >= this->buff_sz_s) {
                //this->mtx.unlock();
                std::cout << "hello\n";
                CPU::pause();                
                continue;
            }

            for(uint64_t i = data_cursor_s; i < data_cursor_s + refresh_b/2; i++){                
                uint64_t n = i & (this->buff_sz_s - 1);
                if(true) {
                    buff_ptr[n] = (int16_t) phase_cnt;
                } else {
                    buff_ptr[n] = (int16_t) 0;
                }
                phase_cnt++;
            }

            // need lock here            
            data_cursor_s = (data_cursor_s + refresh_b/2) & (this->buff_sz_s - 1);

            //this->mtx.lock();
            this->avail_data_s.store(this->avail_data_s.load(std::memory_order_acquire) + 64/2, std::memory_order_acquire);
            //avail_data_s = avail_data_s + refresh_b/2;
//            std::cout << this->avail_data_s;
            //this->mtx.unlock();
            //CPU::pause();
        }
    }
};



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

    uint64 buff_size_s = 67108864;
    uint32 notify_size_s = 4096*32;
    
    hdl.set_param(SPC_SAMPLERATE, 50000000);
    
    int16_t * buff_ptr = new int16_t[buff_size_s];

    hdl.def_transfer(SPCM_BUF_DATA, SPCM_DIR_PCTOCARD, 2*notify_size_s,
                     (void*)buff_ptr, 0, 2*buff_size_s);
    //hdl.set_param(SPC_DATA_AVAIL_CARD_LEN, 2*buff_size_s);
    
    uint64_t user_len_b;
    uint64_t user_pos_b;

    Stream stream{buff_size_s};
    
    auto send_data = [&] {
        hdl.get_param(SPC_DATA_AVAIL_USER_LEN, &user_len_b);
        hdl.get_param(SPC_DATA_AVAIL_USER_POS, &user_pos_b);            
        if (user_len_b >= 2*notify_size_s) {
            if(user_len_b + user_pos_b > buff_size_s * 2)
            // get available sizes
            uint64_t avail_b;
            uint64_t data_len_b = stream.get_s()*2;
            avail_b = std::min(data_len_b, user_len_b);

            if(avail_b > 0) {
                hdl.set_param(SPC_DATA_AVAIL_CARD_LEN, avail_b);
                stream.set_s(avail_b / 2);

                hdl.cmd(M2CMD_DATA_WAITDMA);
                hdl.check_error();
            } else {
                CPU::pause();
            }
        }
    };
    
    send_data();    
    hdl.cmd(M2CMD_DATA_STARTDMA | M2CMD_DATA_WAITDMA);
    hdl.set_param(SPC_TRIG_ORMASK, SPC_TMASK_SOFTWARE);
    hdl.cmd(M2CMD_CARD_START | M2CMD_CARD_ENABLETRIGGER);
    hdl.force_trigger();
    hdl.check_error();
        
    int cnt = 0;
    while (true) {
        send_data();
        //cnt++;
        //std::cout << "cnt: " << cnt << "\n"; 
    }
}
