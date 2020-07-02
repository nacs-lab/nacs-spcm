#include <iostream>
#include <vector>


void bar(int *a){
    *a = 42;
}
void foo(int *a){
    bar(a);
}


int main()
{
    //std::cout << "Hello World!" << std::endl;
    //std::cout << "Does this work?" << std::endl;
    size_t sz = 127;
    size_t sz2 = 25;
    sz &= ~(size_t)(64/2 - 1);
    //std::cout << sz << std::endl;
    //std::cout << std::min(sz, sz2) << std::endl;
    int * ptr;
    int * ptr2;
    int x = 3;
    int y = 5;
    ptr = &x;
    ptr2 = &y;
    std::vector<int*> vect;
    vect.push_back(ptr);
    vect.push_back(ptr2);
    size_t test = 1;
    //std::cout << *ptr << std::endl;
    //std::cout << ptr << std::endl;
    //std::cout << ptr + 1 << std::endl;
    //std::cout << &ptr[test] << std::endl;
    //std::cout << sizeof(test) << std::endl;
    //std::cout << vect[0] << std::endl;
    //std::cout << *vect[0] << std::endl;
    std::cout << x << std::endl;
    foo(&x);
    std::cout << x << std::endl;
}
