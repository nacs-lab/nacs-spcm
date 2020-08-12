#include <nacs-spcm/spcm.h>
#include <nacs-utils/log.h>
#include <nacs-utils/mem.h>
#include <nacs-utils/thread.h>
#include <nacs-utils/timer.h>

#include <cstdlib>
#include <cmath>

#include <immintrin.h>

#include <atomic>
#include <thread>
#include <vector>

#include <iostream>
#include <chrono>
#include <random>

using namespace NaCs;

const int to_store = 2500;
const float to_storef = 0.001f;

SpinLock coutLock;

__attribute__((target("avx512f,avx512bw"), flatten))
NACS_NOINLINE void benchmark_store(long long int trials, size_t buff_sz){
    auto buff = (__m512i*)mapAnonPage(buff_sz * sizeof(__m512i), Prot::RW);
    __m512i val = _mm512_set1_epi16(to_store);
    int i = 0;
    int buff_pos = 0;
    Timer timer;
    timer.restart();
    while(i < trials){
        if (buff_pos >= buff_sz){
            buff_pos = 0;
        }
        _mm512_store_si512(&buff[buff_pos], val);
        i++;
        buff_pos++;
    }
    auto time = timer.elapsed();
    std::cout<< " # of stores: " << trials << ", Buffer size : " << buff_sz
             << ", Avg per store: " << double(time) / double(trials) << " ns" << std::endl;
    unmapPage(buff, buff_sz * sizeof(__m512i));
}

__attribute__((target("avx512f,avx512bw"), flatten))
NACS_NOINLINE void benchmark_stream_store(long long int trials, size_t buff_sz){
    auto buff = (__m512i*)mapAnonPage(buff_sz * sizeof(__m512i), Prot::RW);
    __m512i val = _mm512_set1_epi16(to_store);
    int i = 0;
    int buff_pos = 0;
    Timer timer;
    timer.restart();
    while(i < trials){
        if (buff_pos >= buff_sz){
            buff_pos = 0;
        }
        _mm512_stream_si512(&buff[buff_pos], val);
        i++;
        buff_pos++;
    }
    auto time = timer.elapsed();
    std::cout<< " # of stream stores: " << trials << ", Buffer size : " << buff_sz
             << ", Avg per store: " << double(time) / double(trials) << " ns" << std::endl;
    unmapPage(buff, buff_sz * sizeof(__m512i));
}

struct BenchmarkPipe : DataPipe<__m512>{
    BenchmarkPipe(long long int ntrials, long long int buff_sz, long long int chunk_sz) :
        BenchmarkPipe((__m512*)mapAnonPage(buff_sz * 64, Prot::RW), ntrials, buff_sz,
                      chunk_sz) //buff_sz is in units of 64 bytes
    {
    }
    void join(){
        worker.join();
    }
private:
    BenchmarkPipe(__m512* base, long long int ntrials, long long int buff_sz, long long int chunk_sz) :
        DataPipe(base, buff_sz, (chunk_sz >= 4 * 1024 / 64) ? chunk_sz : 4096/64), // default chunk size of 4kB of data, but chunk_sz allowed
        m_base(base),
        m_ntrials(ntrials),
        m_buff_sz(buff_sz),
        m_chunk_sz(chunk_sz),
        worker()
    {
        worker = std::thread(&BenchmarkPipe::generate, this);
    }
    NACS_NOINLINE __attribute__((target("avx512f,avx512bw"), flatten)) void generate()
    {
        long long int j = 0;
        std::default_random_engine generator;
        std::uniform_real_distribution<float> distribution(0.0,1.0);
        Timer timer;
        timer.restart();
        while(j < m_ntrials){
            //std::this_thread::sleep_for(std::chrono::nanoseconds(10));
            float this_val = distribution(generator);
            __m512 val = _mm512_set1_ps(this_val);
            size_t sz;
            auto ptr = get_write_ptr(&sz);
            //std::cout << sz;
            if (sz < m_chunk_sz) { // force to be larger than a chunk size
                CPU::pause();
                sync_writer();
                continue;
            }
            size_t write_sz = 0;
            for (; write_sz < sz; write_sz += 1) {
                _mm512_store_ps(&ptr[write_sz], val);
            }
            wrote_size(write_sz);
            CPU::wake();
            j = j + (long long int) write_sz;
            // std::cout << "j: " << j << "," << std::endl;
        }
        auto res = timer.elapsed();
        coutLock.lock();
        std::cout << "Writer thread done!" << std::endl;
        std::cout << "WRITER REPORT: Buffer size: " << m_buff_sz << ", Number of Trials: " << m_ntrials
                  << ", Avg per trial " << double(res) / double(m_ntrials) << " ns " << std::endl;
        coutLock.unlock();
    }
    __m512 *const m_base;
    long long int m_ntrials;
    long long int m_buff_sz;
    long long int m_chunk_sz;
    std::thread worker;
};
NACS_NOINLINE __attribute__((target("avx512f,avx512bw"), flatten))
void test_throughput(long long int ntrials, long long int buff_sz, long long int chunk_sz, int n_threads){
    std::vector<BenchmarkPipe*> bpvec;
    for (int i = 0; i < n_threads; ++i){
        BenchmarkPipe *bpptr;
        bpptr = new BenchmarkPipe(ntrials, buff_sz, chunk_sz);
        bpvec.push_back(bpptr);
    }
    long long int j = 0;
    volatile __m512 data;
    volatile __m512 tot;
    tot = _mm512_set1_ps(0.0f);
    chunk_sz = (chunk_sz >= 4096 / 64) ? chunk_sz : 4096/64;
    Timer timer;
    timer.restart();
    while (j < ntrials){
        // std::this_thread::sleep_for(std::chrono::nanoseconds(10));
        size_t this_sz;
        //size_t min_sz = 48 * 1024ll * 1024ll * 1024ll;
        const __m512 *this_ptr;
        int k = 0;
        std::vector<const __m512*> bpptrs;
        while (k < n_threads){
            //std::cout << " k:" << k << std::endl;
            //std::cout << "size: " << bpvec.size() << std::endl;
            this_ptr = (*bpvec[k]).get_read_ptr(&this_sz);
            if (this_sz < chunk_sz) { // read chunk sz
                CPU::pause();
                (*bpvec[k]).sync_reader();
                continue;
            } else {
                //if (min_sz > this_sz){
                //    min_sz = this_sz;
                //}
                bpptrs.push_back(this_ptr);
                k += 1;
            }
        }
        size_t read_sz = 0;
        for(; read_sz < chunk_sz; read_sz += 1){
            for (int k = 0; k < n_threads; ++k){
                data = *bpptrs[k];
                tot += data;
                bpptrs[k] += 1;
            }
        }
        for (int m = 0; m <n_threads; ++m){
            (*bpvec[m]).read_size(read_sz);
        }
        CPU::wake();
        j = j + (long long int)read_sz;
    }
    auto res = timer.elapsed();
    auto totptr = &tot;
    coutLock.lock();
    std::cout << "Reader thread done!" << std::endl;
    std::cout << "READER REPORT: Buffer size: " << buff_sz << ", Number of Trials: " << ntrials
              << ", Avg per trial " << double(res) / double(ntrials) << " ns " << std::endl;
    std::cout << *(int16_t*)totptr << std::endl;
    coutLock.unlock();
    for (int k = 0; k < n_threads; ++k){
        (*bpvec[k]).join();
    }
}

int main (){
    // all bytes in units of 64 bytes, the size of a __m512i object
    int n_buffer_fills = 128;
    std::cout << "All sizes in units of 64 bytes" << std::endl;
    if (false) {
        std::cout << "TEST STREAMSTORE PERFORMANCE" << std::endl;
        std::cout << "Number of Buffer fills: " << n_buffer_fills << std::endl;
        benchmark_store(n_buffer_fills * 128/64, 128/64);
        benchmark_stream_store(n_buffer_fills * 128/64, 128/64);

        benchmark_store(n_buffer_fills * 1024/64, 1024/64);
        benchmark_stream_store(n_buffer_fills * 1024/64, 1024/64);

        benchmark_store(n_buffer_fills * 1024 * 1024/64, 1024 * 1024/64);
        benchmark_stream_store(n_buffer_fills * 1024 * 1024/64, 1024 * 1024/64);

        benchmark_store(n_buffer_fills * 1024ll * 1024ll * 1024ll / 64, 1024ll * 1024ll * 1024ll / 64);
        benchmark_stream_store(n_buffer_fills * 1024ll * 1024ll * 1024ll / 64, 1024ll * 1024ll * 1024ll / 64);
    }
    int nthreads = 3;
    std::cout << "TEST READER-WRITER COMMUNICATION" << std::endl;
    std::cout << "Number of Buffer fills: " << n_buffer_fills << std::endl;
    // test_throughput(n_buffer_fills * 128/64, 128/64, 1, nthreads);
    //test_throughput(n_buffer_fills * 1024/64, 1024/64, 1, nthreads);
    test_throughput(n_buffer_fills * 1024 * 1024 / 64, 1024 * 1024/64, 1024 * 1024/64/2, nthreads);
    test_throughput(n_buffer_fills * 1024ll * 1024ll * 1024ll/64, 1024ll * 1024ll * 1024ll/64,1, nthreads);
    test_throughput(n_buffer_fills * 4*  1024ll * 1024ll * 1024ll/64, 4 * 1024ll * 1024ll * 1024ll/64,1024ll * 1024ll * 1024ll / 64, nthreads);
}
