//

#include "Controller.h"
#include "Config.h"

#include <iostream>
#include <nacs-utils/log.h>
#include <chrono>
#include <thread>

using namespace NaCs;

using namespace std::chrono_literals;

namespace Spcm {
// TIME TRACKING STUFF

typedef int16_t m512i16 __attribute__((vector_size (64)));

static constexpr m512i16 interweave_idx1 = {0, 32, 1, 33, 2, 34, 3, 35, 4, 36, 5, 37,
                                 6, 38, 7, 39, 8, 40, 9, 41, 10, 42, 11, 43,
                                 12, 44, 13, 45, 14, 46, 15, 47};
static constexpr m512i16 interweave_idx2 = {16, 48, 17, 49, 18, 50, 19, 51, 20, 52,
                                 21, 53, 22, 54, 23, 55, 24, 56, 25, 57,
                                 26, 58, 27, 59, 28, 60, 29, 61, 30, 62,
                                 31,63}; // idx1 and idx2 used for interweaving in multi channel output

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
    if (req == WorkerRequest::Stop) {
        stopCard();
        return false;
    }
    // Unlock request
    tryUnlock();
    return true;
}
NACS_EXPORT() void Controller::startWorker()
{
    if (workerRunning()) {
        printf("worker already running\n");
        return;
    }
    printf("Now trying to acquire lock\n");
    {
        printf("Starting card\n");
        std::lock_guard<std::mutex> locker(m_worker_lock);
        ensureInit();
        initChnsAndBuffer();
        for (int i = 0; i < n_phys_chn; ++i) {
            (*m_stm_mngrs[m_out_chns[i]]).start_streams();
            (*m_stm_mngrs[m_out_chns[i]]).start_worker();
        }
        m_worker_req.store(WorkerRequest::None, std::memory_order_relaxed);
        m_worker = std::thread(&Controller::workerFunc, this);
    }
}
NACS_EXPORT() void Controller::stopWorker()
{
    if (!workerRunning())
        return;
    m_worker_req.store(WorkerRequest::Stop, std::memory_order_relaxed);
    printf("Now trying to acquire lock in stopWorker\n");
    {
        std::lock_guard<std::mutex> locker(m_worker_lock);
        for (int i = 0; i < n_phys_chn; ++i) {
            printf("Stopping stream %u\n", m_out_chns[i]);
            (*m_stm_mngrs[m_out_chns[i]]).stop_streams();
            (*m_stm_mngrs[m_out_chns[i]]).stop_worker();
        }
        //stopCard();
        m_worker.join();
    }
    printf("StopWorker finished\n");
}
NACS_EXPORT() void Controller::runSeq(uint32_t idx, Cmd *p, size_t sz, bool wait){
    uint32_t nwrote;
    do {
        nwrote = copy_cmds(idx, p, sz);
        p += nwrote;
        sz -= nwrote;
    } while (sz > 0);
    flush_cmd(idx);
    std::cout << "now distributing" << std::endl;
    distribute_cmds(idx);
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
    //hdl.ch_enable(CHANNEL0); // only one channel activated
    //hdl.enable_out(0, true);
    //hdl.set_amp(0, 2500);
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

    int64_t rate = m_conf.sample_rate;//int64_t(625e6);
    // hdl.set_param(SPC_CLOCKMODE, SPC_CM_INTPLL);
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

    hdl.set_param(SPCM_X2_MODE, SPCM_XMODE_TRIGOUT);
    // Enable output (since M4i).
    //hdl.set_param(SPC_ENABLEOUT0, 1);
    //hdl.set_param(SPC_FILTER0, 0);

    // Define transfer buffer
    //buff_ptr = (int16_t*)mapAnonPage(2 * buff_sz_nele, Prot::RW);
    //buff_pos = 0;
    //hdl.def_transfer(SPCM_BUF_DATA, SPCM_DIR_PCTOCARD, 4096 * 32,
    //                   (void*)buff_ptr, 0, 2 * buff_sz_nele);
//hdl.check_error();
    m_initialized = true;
}

void Controller::initChnsAndBuffer()
{
    // initialize channels and buffers based on m_out_chns
    if (DMA_started)
        stopCard();
    uint8_t chn_bits = 0;
    for (int i = 0; i < n_phys_chn; i++) {
        if (m_out_chns[i] == 0) {
            chn_bits = chn_bits | CHANNEL0;
            hdl.enable_out(0, true);
            hdl.set_amp(0, 2500);
            hdl.set_param(SPC_FILTER0, 0);
        }
        else if (m_out_chns[i] == 1) {
            chn_bits = chn_bits | CHANNEL1;
            hdl.enable_out(1, true);
            hdl.set_amp(1, 2500);
            hdl.set_param(SPC_FILTER1, 0);
        }
        else if (m_out_chns[i] == 2) {
            chn_bits = chn_bits | CHANNEL2;
            hdl.enable_out(2, true);
            hdl.set_amp(2, 2500);
            hdl.set_param(SPC_FILTER2, 0);
        }
        else if (m_out_chns[i] == 3) {
            chn_bits = chn_bits | CHANNEL3;
            hdl.enable_out(3, true);
            hdl.set_amp(3, 2500);
            hdl.set_param(SPC_FILTER3, 0);
        }
    }
    hdl.ch_enable(chn_bits);
    // set up hardware buffer
    hdl.set_param(SPC_DATA_OUTBUFSIZE, 2 * hw_buff_sz_nele * n_phys_chn);
    hdl.write_setup();

    uint32_t active_chns;
    uint32_t ch_count;
    hdl.get_param(SPC_CHENABLE, &active_chns);
    hdl.get_param(SPC_CHCOUNT, &ch_count);
    printf("Activated channels bitmask is: 0x%08x\n", active_chns);
    printf("Number of activated channels: %d\n", ch_count);
    buff_ptr = (int16_t*)mapAnonPage(2 * buff_sz_nele * n_phys_chn, Prot::RW);
    buff_pos = 0;
    // TODO: Buffer size?
    hdl.def_transfer(SPCM_BUF_DATA, SPCM_DIR_PCTOCARD, notif_size,
                     (void*)buff_ptr, 0, 2 * buff_sz_nele * n_phys_chn);
    hdl.check_error();
}

void Controller::stopCard()
{
    printf("Stopping card\n");
    // stop DMA transfer and card.
    hdl.cmd(M2CMD_DATA_STOPDMA);
    hdl.check_error();
    hdl.cmd(M2CMD_CARD_STOP);
    hdl.check_error();
    DMA_started = false;
    printf("Card stop finished\n");
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
    int32_t lSerialNumber;
    hdl.get_param(SPC_PCISERIALNO, &lSerialNumber);
    printf("Serial number: %05d\n", lSerialNumber);
    uint64_t initial_clock = cycleclock();
    struct DebugInfo {
        const DebugInfo& operator= (const DebugInfo &di) {
            for (int i = 0; i < di.avails.size(); i++) {
                avails.push_back(di.avails[i]);
            }
            for (int i = 0; i < di.clocks_before.size(); i++) {
                clocks_before.push_back(di.clocks_before[i]);
            }
            for (int i = 0; i < di.can_write_amts.size(); i++) {
                can_write_amts.push_back(di.can_write_amts[i]);
            }
            for (int i = 0; i < di.clocks.size(); i++) {
                clocks.push_back(di.clocks[i]);
            }
            return *this;
        }
        std::vector <uint64_t>avails;
        std::vector <double> clocks_before;
        std::vector <uint64_t> can_write_amts;
        std::vector <double> clocks;
    };
    std::lock_guard<std::mutex> locker(m_worker_lock);
    //printf("size of size_t: %u", sizeof(size_t));
    // TIMING STUFF
    bool first_avail = true;
    uint32_t cons_count = 0;
    uint32_t mem_size = 100000;
    uint32_t mem_idx = 0;
    std::vector <uint64_t> avails;
    std::vector <double> clocks_before;
    std::vector <uint64_t> can_write_amts;
    std::vector <double> clocks;
    std::vector <double> fill_clocks;
    uint64_t prev_max, max = 0;
    uint64_t nfills = 0;
    avails.resize(mem_size);
    clocks_before.resize(mem_size);
    can_write_amts.resize(mem_size);
    clocks.resize(mem_size);
    std::vector<DebugInfo> full_infos;
    while (checkRequest()) {
        //std::cout << "working" << std::endl;
        // relay data from StreamManager to card
    retry:
        //min_sz = 8 * 1024ll * 1024ll * 1024ll;
        std::vector<const int16_t*> ptrs(n_phys_chn, nullptr);
        size_t sz;
        size_t min_sz = 8 * 1024ll * 1024ll * 1024ll; // cannot be this large.
        for (int i = 0; i < n_phys_chn; ++i)
        {
            ptrs[i] = (*m_stm_mngrs[m_out_chns[i]]).get_output(sz);
            //std::cout << "sz: " << sz << std::endl;
            if (sz < notif_size / 2 / n_phys_chn) { // 4096
                // data not ready
                //if (sz > 0)
                //     (*m_stm_mngrs[m_out_chns[i]]).sync_reader();
                CPU::pause();
                //toCont = true;
                goto retry;
            }
            if (sz < min_sz) {
                min_sz = sz;
            }
        }
        
        //std::cout << "stream manager ready" << std::endl;
        min_sz = min_sz & ~(uint64_t)(notif_size / 2 / n_phys_chn - 1); // make it chunks of 2048
        //read out available number of bytes
        uint64_t count,card_count = 0;
        //clocks_before[mem_idx] = (cycleclock() - initial_clock) / (3e9);
        hdl.get_param(SPC_DATA_AVAIL_USER_LEN, &card_count);
        hdl.check_error();
        //clocks[mem_idx] = (cycleclock() - initial_clock)/(3e9);
        // if (counter % 1000 == 0 && counter < 20001)
        //     printf("%lu check avail: %lu\n",counter, count);
        // else if (counter % 100000 == 0)
        //     printf("%lu check avail: %lu\n", counter, count);
        // counter++;
        if (first_avail)
        {
            std::cout << "first avail: " << count << std::endl;
            first_avail = false;
        }
        count = card_count & ~(uint64_t)(notif_size - 1);
        if (card_count >= max && card_count != 4 * 1024ll * 1024ll * 1024ll) {
            prev_max = max;
            max = card_count;
        }
        card_avail.store(count, std::memory_order_relaxed); // Not a huge deal if this is wrong...
        count = std::min(count, uint64_t(min_sz * 2 * n_phys_chn)); // count is number of bytes we can fill. need to account for number of physical channels.
        // log availability and how much i am trying to write
        //avails[mem_idx] = card_count;
        //can_write_amts[mem_idx] = min_sz * 2;
        /*if (card_count == 4 * 1024ll * 1024ll * 1024ll) {
            nfills++;
            fill_clocks.push_back((cycleclock() - initial_clock)/(3e9));
            DebugInfo di;
            uint32_t samples = 20;
            uint32_t mem_idx_to_use = mem_idx;
            for (uint32_t i = 0; i < samples; i++) {
                di.avails.push_back(avails[mem_idx_to_use]);
                di.clocks_before.push_back(clocks_before[mem_idx_to_use]);
                di.can_write_amts.push_back(can_write_amts[mem_idx_to_use]);
                di.clocks.push_back(clocks[mem_idx_to_use]);
                if(mem_idx_to_use == 0) {
                    mem_idx_to_use = mem_size - 1;
                }
                else {
                    mem_idx_to_use--;
                }
            }
            full_infos.push_back(di);
            printf("stored full info with size %u\n", full_infos[nfills - 1].avails.size());
        }
        mem_idx++;
        if (mem_idx >= mem_size)
        {
            mem_idx = 0;
        }
        if (cons_count == mem_size - 50) {
            // print and throw error
            uint32_t tot_to_print = 50;
            uint32_t printed = 0;
            uint32_t mem_idx_now = mem_idx;
            for (; mem_idx < mem_size; mem_idx++) {
                printf("print: %u, avail: %lu, can_write: %lu, clock before %f, clock: %f\n", printed, avails[mem_idx], can_write_amts[mem_idx], clocks_before[mem_idx], clocks[mem_idx]);
                printf("prev_max: %lu, max: %lu\n", prev_max, max);
                printed++;
                if (printed == tot_to_print) {
                    goto done;
                }
            }
            mem_idx = 0;
            for (; mem_idx <= mem_idx_now; mem_idx++) {
                printf("print: %u, avail: %lu, can_write: %lu, clock_before: %f, clock: %f\n", printed, avails[mem_idx], can_write_amts[mem_idx], clocks_before[mem_idx], clocks[mem_idx]);
                printf("prev_max: %lu, max: %lu\n", prev_max, max);
                printed++;
                if (printed == tot_to_print) {
                    break;
                }
            }
        done:
            //throw std::runtime_error("card output has failed");
            printf("nfills:%lu\n", nfills);
            for (uint32_t i = 0; i < fill_clocks.size(); i++)
            {
                printf("fill clock %u, value: %f\n", i, fill_clocks[i]);
                for (uint32_t j = 0; j < full_infos[i].avails.size(); j++) {
                    printf("avail: %lu, can_write: %lu, clock_before: %f, clock: %f\n", full_infos[i].avails[j], full_infos[i].can_write_amts[j], full_infos[i].clocks_before[j], full_infos[i].clocks[j]);
                }
            }
            printf("\n");
            mem_idx = mem_idx_now;
            cons_count = 0;
        }
        */
        if (DMA_started && !count) {
            //uint64_t pos;
            //cons_count++;
            //hdl.get_param(SPC_DATA_AVAIL_USER_POS, &pos);
            //if (not_counter % 100 == 0 && not_counter < 1001) {
            //    printf("Count min not reached %u, count %lu, avail: %lu, min_sz: %u, loc: %p, pos: %lu\n", not_counter, count, check_avail(), min_sz, ptrs[0], pos);
            //}
            //else if (not_counter % 10000000 == 0)
            //    printf("Count min not reached %u, count %lu, avail: %lu, min_sz: %u, loc: %p, pos: %lu\n", not_counter, count, check_avail(), min_sz, ptrs[0], pos);
            //not_counter++;
            continue;
        }
        else {
            cons_count = 0;
        }
        if (count & 1)
            abort();
        int16_t* curr_ptr;
        int16_t* curr_ptr2;
        //std::cout << "storing data" << std::endl;
        if (n_phys_chn == 1) {
            for(int i = 0; i < (count / 2); i += 64/2) {
                curr_ptr = buff_ptr + buff_pos;
                // count/2 number of int16_t, each advance advances 512 bytes, so 32 int_16
                //std::cout << "writing" << std::endl;
                _mm512_stream_si512((__m512i*)curr_ptr, *(__m512i*) ptrs[0]);
                buff_pos += 64/2;
                ptrs[0] += 64/2;
                if (buff_pos >= buff_sz_nele) {
                    buff_pos = 0;
                }
            }
        }
        else if (n_phys_chn == 2) {
            for (int i = 0; i < (count / 2); i+= 64) {
                curr_ptr = buff_ptr + buff_pos;
                curr_ptr2 = curr_ptr + 32;
                __m512i out1, out2, data1, data2;
                data1 = *(__m512i*)ptrs[0];
                data2 = *(__m512i*)ptrs[1];
                out1 = _mm512_mask_permutex2var_epi16(data1, 0xFFFFFFFF,
                                                      (__m512i) interweave_idx1, data2);
                out2 = _mm512_mask_permutex2var_epi16(data1, 0xFFFFFFFF,
                                                      (__m512i) interweave_idx2, data2);
                _mm512_stream_si512((__m512i*)curr_ptr, out1);
                _mm512_stream_si512((__m512i*)curr_ptr2, out2);
                ptrs[0] += 32;
                ptrs[1] += 32;
                buff_pos += 64;
                if (buff_pos >= buff_sz_nele * 2) {
                    buff_pos = 0;
                }
            }
        }
        // NO SUPPORT FOR MORE THAN 2 PHYS OUTPUTS AT THE MOMENT
        //std::cout << "done writing" << std::endl;
        hdl.set_param(SPC_DATA_AVAIL_CARD_LEN, count);
        hdl.check_error();
        for (int i = 0; i < n_phys_chn; ++i) {
            (*m_stm_mngrs[m_out_chns[i]]).consume_output(count / 2 / n_phys_chn);
        }
        if (!DMA_started && check_avail() < 1000)
        {
            //startDMA(0);
            hdl.cmd(M2CMD_DATA_STARTDMA | M2CMD_DATA_WAITDMA);
            hdl.set_param(SPC_TRIG_ORMASK, SPC_TMASK_SOFTWARE);
            hdl.cmd(M2CMD_CARD_START | M2CMD_CARD_ENABLETRIGGER);
            printf("force trigger at count %lu\n", count);
            std::this_thread::sleep_for(1ms);
            hdl.force_trigger();
            hdl.check_error();
            DMA_started = true;
            prev_max = 0;
            max = 0;
            nfills = 0;
        }
        CPU::wake();
    }
    printf("Worker finishing\n");
}
}
