#pragma once

#include <memory>

template<typename T>
class FreeDeleter {
public:
    typedef T* pointer;

    void operator()(pointer p) {
        free(p);
    }
};

template<typename T>
using unique_free_ptr = std::unique_ptr<T, FreeDeleter<T>>;