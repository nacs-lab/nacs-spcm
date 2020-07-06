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
    std::cout << sizeof(int16_t) << std::endl;
    std::cout << sizeof(__m512) << std::endl;
    int16_t a[2] = {1,1};
    __m512 *test2;
    test2 = (__m512*)a; 
    std::cout << a[0] << std::endl;
    std::cout << a[1] << std::endl;
    std::cout << a << std::endl;
    std::cout << test2 << std::endl;
    std::cout << test2 + 1 << std::endl;
    size_t sz = 33;
    std::cout << sz/2 << std::endl;
    std::cout << std::thread::hardware_concurrency() << std::endl;
}
