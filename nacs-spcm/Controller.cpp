//

#include "Controller.h"
#include "Config.h"

#include <iostream>
#include <nacs-utils/log.h>

using namespace NaCs;

namespace Spcm {
// TIME TRACKING STUFF

void Controller::tryUnlock(){
    m_worker_lock.unlock();
    for (int i = 0; i < 16; i++) {
        // Just to make it more likely that the release works.
        // If it doesn't work this time it's totally fine
        CPU::pause();
    }
    m_worker_lock.lock();
}

// must be called by the worker thread with the lock hold.
inline bool Controller::checkRequest()
{
    auto req = m_worker_req.load(std::memory_order_relaxed);
    if (likely(req == WorkerRequest::None))
        return true;
    if (req == WorkerRequest::Stop)
        return false;
    // Unlock request
    tryUnlock();
    return true;
}
NACS_EXPORT() void Controller::startWorker()
{
    ensureInit();
    if (workerRunning())
        return;
    m_stm_mngr.start_streams();
    m_stm_mngr.start_worker();
    m_worker = std::thread(&Controller::workerFunc, this);
}
NACS_EXPORT() void Controller::stopWorker()
{
    if (!workerRunning())
        return;
    m_worker_req.store(WorkerRequest::Stop, std::memory_order_relaxed);
    m_stm_mngr.stop_streams();
    m_stm_mngr.stop_worker();
    m_worker.join();
}
NACS_EXPORT() void Controller::runSeq(Cmd *p, size_t sz, bool wait){
    uint32_t nwrote;
    do {
        nwrote = copy_cmds(p, sz);
        p += nwrote;
        sz -= nwrote;
    } while (sz > 0);
    flush_cmd();
    std::cout << "now distributing" << std::endl;
    distribute_cmds();
    std::cout << "after command distribution" << std::endl;
    bool wasRunning = workerRunning();
    if (!wasRunning)
        startWorker();
    if (wait) {
        //Trigger stuff
    }
    if (wait) {
        // Trigger stuff
        if (!wasRunning) {
            stopWorker();
        }
    }
}

void Controller::init()
{
    hdl.ch_enable(CHANNEL0); // only one channel activated
    hdl.enable_out(0, true);
    hdl.set_amp(0, 2500);
    hdl.set_param(SPC_CLOCKMODE, SPC_CM_INTPLL);
    hdl.set_param(SPC_CARDMODE, SPC_REP_FIFO_SINGLE); // set the FIFO single replay mode
    hdl.write_setup();
    // starting with firmware version V9 we can program the hardware
    // buffer size to reduce the latency
    auto ver = hdl.pci_version();
    Log::log("Version: %d.%d\n", ver.first, ver.second);
    Log::log("Max rate: %f MS/s\n", double(hdl.max_sample_rate()) / 1e6);
    Log::log("1\n");

    int32_t lib_ver;
    hdl.get_param(SPC_GETDRVVERSION, &lib_ver);

    int32_t max_dac;
    hdl.get_param(SPC_MIINST_MAXADCVALUE, &max_dac);
    if ((lib_ver & 0xffff) >= 3738)
        max_dac--;
    printf("max_dac: %d\n", max_dac);

    int64_t rate = int64_t(625e6);
    hdl.set_param(SPC_CLOCKMODE, SPC_CM_INTPLL);
    // hdl.set_param(SPC_CLOCKMODE, SPC_CM_EXTREFCLOCK);
    // hdl.set_param(SPC_REFERENCECLOCK, 100 * 1000 * 1000);

    hdl.set_param(SPC_SAMPLERATE, rate);
    hdl.get_param(SPC_SAMPLERATE, &rate);
    printf("Sampling rate set to %.1lf MHz\n", (double)rate / MEGA(1));
    int64_t clock_rate;
    hdl.set_param(SPC_CLOCKOUT, 1);
    hdl.get_param(SPC_CLOCKOUTFREQUENCY, &clock_rate);
    printf("Clock out rate is %.1lf MHz\n", (double)clock_rate / MEGA(1));
    hdl.set_param(SPC_TRIG_ORMASK, SPC_TMASK_SOFTWARE);
    hdl.set_param(SPC_TRIG_ANDMASK, 0);
    hdl.set_param(SPC_TRIG_CH_ORMASK0, 0);
    hdl.set_param(SPC_TRIG_CH_ORMASK1, 0);
    hdl.set_param(SPC_TRIG_CH_ANDMASK0, 0);
    hdl.set_param(SPC_TRIG_CH_ANDMASK1, 0);

    // Enable output (since M4i).
    hdl.set_param(SPC_ENABLEOUT0, 1);
    hdl.set_param(SPC_FILTER0, 0);

    // Define transfer buffer
    buff_ptr = (int16_t*)mapAnonPage(2 * buff_sz_nele, Prot::RW);
    buff_pos = 0;
    hdl.def_transfer(SPCM_BUF_DATA, SPCM_DIR_PCTOCARD, 4096 * 32,
                     (void*)buff_ptr, 0, 2 * buff_sz_nele);
    hdl.check_error();
}
NACS_EXPORT() void Controller::force_trigger()
{
    std::cout << "calling force trigger" << std::endl;
    hdl.cmd(M2CMD_CARD_START | M2CMD_CARD_ENABLETRIGGER);
    hdl.force_trigger();
    hdl.check_error();
    uint32_t status;
    int32_t chns;
    hdl.get_param(SPC_M2STATUS, &status);
    chns = hdl.ch_enable();
    Log::log("Status: 0x%x\n", status);
    Log::log("Channel Enable: %i\n", chns);
}
inline void Controller::startDMA(uint64_t sz)
{
    if (DMA_started)
    {
        return;
    }
    if (sz)
        hdl.set_param(SPC_DATA_AVAIL_CARD_LEN, sz);
    hdl.cmd(M2CMD_DATA_STARTDMA | M2CMD_DATA_WAITDMA);
    hdl.set_param(SPC_TRIG_ORMASK, SPC_TMASK_SOFTWARE);
    //hdl.cmd(M2CMD_CARD_START | M2CMD_CARD_ENABLETRIGGER);
    //hdl.force_trigger();
    hdl.check_error();
    DMA_started = true;
}
__attribute__((target("avx512f,avx512bw"), flatten))
void Controller::workerFunc()
{
    // set_fifo_sched();

    std::lock_guard<std::mutex> locker(m_worker_lock);

    // TIMING STUFF
    bool first_avail = true;
    while (checkRequest()) {
        //std::cout << "working" << std::endl;
        // relay data from StreamManager to card
        size_t sz;
        auto *ptr = m_stm_mngr.get_output(sz);
        //std::cout << "sz: " << sz << std::endl;
        if (sz < 4096 / 2) {
            // data not ready
            CPU::pause();
            continue;
        }
        //std::cout << "stream manager ready" << std::endl;
        sz = sz & ~(size_t)2047; // make it chunks of 2048
        //read out available number of bytes
        uint64_t count = 0;
        hdl.get_param(SPC_DATA_AVAIL_USER_LEN, &count);
        hdl.check_error();
        if (first_avail)
        {
            std::cout << "first avail: " << count << std::endl;
            first_avail = false;
        }
        card_avail.store(count, std::memory_order_relaxed); // Not a huge deal if this is wrong...
        count = std::min(count, uint64_t(sz * 2));
        if (!count)
            continue;
        if (count & 1)
            abort();
        int16_t* curr_ptr;
        //std::cout << "storing data" << std::endl;
        for(int i = 0; i < (count / 2); i += 64/2) {
            curr_ptr = buff_ptr + buff_pos;
            // count/2 number of int16_t, each advance advances 512 bytes, so 32 int_16
            //std::cout << "writing" << std::endl;
            _mm512_stream_si512((__m512*)curr_ptr, *(__m512*) ptr);
            buff_pos += 64/2;
            ptr += 64/2;
            if (buff_pos >= buff_sz_nele) {
                buff_pos = 0;
            }
        }
        //std::cout << "done writing" << std::endl;
        hdl.set_param(SPC_DATA_AVAIL_CARD_LEN, count);
        hdl.check_error();
        m_stm_mngr.consume_output(count / 2);
        if (!DMA_started)
        {
            //startDMA(0);
            hdl.cmd(M2CMD_DATA_STARTDMA | M2CMD_DATA_WAITDMA);
            hdl.set_param(SPC_TRIG_ORMASK, SPC_TMASK_SOFTWARE);
            //hdl.cmd(M2CMD_CARD_START | M2CMD_CARD_ENABLETRIGGER);
            //hdl.force_trigger();
            hdl.check_error();
            DMA_started = true;
        }
        CPU::wake();
    }
}
}
