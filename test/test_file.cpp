#include <iostream>
#include <vector>

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

typedef int16_t my_type __attribute__((vector_size (64)));

__attribute__((target("avx512f,avx512bw"), flatten))
int main()
{
    //std::cout << "Hello World!" << std::endl;
    //std::cout << "Does this work?" << std::endl;
    //size_t sz = 127;
    //size_t sz2 = 25;
    //sz &= ~(size_t)(64/2 - 1);
    //std::cout << sz << std::endl;
    //std::cout << std::min(sz, sz2) << std::endl;
    //int * ptr;
    //int * ptr2;
    //int x = 3;
    //int y = 5;
    //ptr = &x;
    //ptr2 = &y;
    //std::vector<int*> vect;
    //vect.push_back(ptr);
    //vect.push_back(ptr2);
    //size_t test = 1;
    //std::cout << *ptr << std::endl;
    //std::cout << ptr << std::endl;
    //std::cout << ptr + 1 << std::endl;
    //std::cout << &ptr[test] << std::endl;
    //std::cout << sizeof(test) << std::endl;
    //std::cout << vect[0] << std::endl;
    //std::cout << *vect[0] << std::endl;
    //std::cout << x << std::endl;
    //foo(&x);
    //std::cout << x << std::endl;
    //std::cout << sizeof(int16_t) << std::endl;
    //std::cout << sizeof(__m512) << std::endl;
    //int16_t a[2] = {1,1};
    //__m512 *test2;
    //test2 = (__m512*)a; 
    //std::cout << a[0] << std::endl;
    //std::cout << a[1] << std::endl;
    //std::cout << a << std::endl;
    //std::cout << test2 << std::endl;
    //std::cout << test2 + 1 << std::endl;
    //size_t sz = 33;
    //std::cout << sz/2 << std::endl;
    //std::cout << std::thread::hardware_concurrency() << std::endl;
    //std::cout << ceil(sz/2) << std::endl;
    //int c = 3;
    //int d = 4;
    //std::vector<const int*> intptr;
    //intptr.push_back(&c);
    //intptr.push_back(&d);
    //const int **ptrptr = intptr.data();
    //std::cout << **ptrptr << std::endl;
    //std::cout << (*ptrptr)[1] << std::endl;
    //*ptrptr ++;
    //std::cout << *ptrptr << std::endl;
    
    __m512i C,D;
    my_type B = {1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31,
                 33,35,37,39,41,43,45,47,49,51,53,55,57,59,61,63};
    my_type A = {0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,
                 32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62};
    // my_type idx1 = {47,15,46,14,45,13,44,12,43,11,42,10,41,9,40,8,39,7,
    //                                 38,6,37,5,36,4,35,3,34,2,33,1,32,0};
    my_type idx1 = {0,32,1,33,2,34,3,35,4,36,5,37,6,38,7,39,8,40,9,41,10,42,11,43,12,44,
                    13,45,14,46,15,47};
// my_type idx2 = {63,31,62,30,61,29,60,28,59,27,58,26,57,25,56,24,
    //                              55,23,54,22,53,21,52,20,51,19,50,18,49,17,48,16};
    my_type idx2 = {16,48,17,49,18,50,19,51,20,52,21,53,22,54,23,55,24,56,25,57,26,58,
                    27,59,28,60,29,61,30,62,31,63};
    __m512i AtoUse = (__m512i) A;
    __m512i BtoUse = (__m512i) B;
    __m512i idx1ToUse = (__m512i) idx1;
    __m512i idx2ToUse = (__m512i) idx2;
    C = _mm512_mask_permutex2var_epi16(AtoUse, 0xFFFFFFFF, idx1ToUse, BtoUse);
    D = _mm512_mask_permutex2var_epi16(AtoUse, 0xFFFFFFFF, idx2ToUse, BtoUse);
    // int16_t val[32];
    __m512i test = _mm512_set1_epi16(0);
    __m512i* ptr = &test;
    _mm512_stream_si512(ptr, C);
    int16_t* val = (int16_t*) ptr;
    for (int i = 0; i < 32; i++) {
        std::cout << val[i] << ",";
    }
    std::cout << std::endl;
    _mm512_stream_si512((__m512i*) ptr, D);
    for (int i = 0; i < 32; i++) {
         std::cout << val[i] << ",";
    }
    std::cout << std::endl;
}
