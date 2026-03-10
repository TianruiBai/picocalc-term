#ifndef MUTEX_H
#define MUTEX_H

#include "lock_base_rp2040.h"

template<typename T>
class mutex {
public:
    void lock() {}
    void unlock() {}
};

#endif // MUTEX_H
