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
        DataPipe(base, buff_sz, chunk_sz), // default chunk size of 4kB of data, but chunk_sz allowed
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
        __m512 val = _mm512_set1_ps(to_storef);
        Timer timer;
        timer.restart();
        while(j < m_ntrials){
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
void test_throughput(long long int ntrials, long long int buff_sz, long long int chunk_sz){
    BenchmarkPipe bp(ntrials, buff_sz, chunk_sz);
    long long int j = 0;
    volatile __m512 data;
    volatile __m512 tot;
    tot = _mm512_set1_ps(0.0f);
    Timer timer;
    timer.restart();
    while (j < ntrials){
        size_t sz;
        const __m512 *ptr;
        while (true){
            ptr = bp.get_read_ptr(&sz);
            if (sz < chunk_sz) { // read chunk sz
                CPU::pause();
                bp.sync_reader();
                continue;
            }
            break;
        }
        size_t read_sz = 0;
        for(; read_sz < sz; read_sz += 1){
            data = ptr[read_sz];
            tot += data;
        }
        bp.read_size(read_sz);
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
    bp.join();
}

int main (){
    // all bytes in units of 64 bytes, the size of a __m512i object
    int n_buffer_fills = 128;
    std::cout << "All sizes in units of 64 bytes" << std::endl;
    if (true) {
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

    std::cout << "TEST READER-WRITER COMMUNICATION" << std::endl;
    std::cout << "Number of Buffer fills: " << n_buffer_fills << std::endl;
    test_throughput(n_buffer_fills * 128/64, 128/64, 1);
    test_throughput(n_buffer_fills * 1024/64, 1024/64, 1024/64/4);
    test_throughput(n_buffer_fills * 1024 * 1024 / 64, 1024 * 1024/64, 1024 *1024/64/4);
    test_throughput(n_buffer_fills * 1024ll * 1024ll * 1024ll/64, 1024ll * 1024ll * 1024ll/64, 1024ll * 1024ll * 1024ll/64/4);
}
