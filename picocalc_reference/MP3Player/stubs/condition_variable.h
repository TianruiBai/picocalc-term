#ifndef CONDITION_VARIABLE_H
#define CONDITION_VARIABLE_H

#include "mutex.h"
#include <functional>

template<typename T>
class condition_variable {
public:
    void notify_one() {}
    void notify_all() {}

    // wait without predicate
    void wait(mutex<T> &m) {}
    
    // wait with predicate
    template<typename Pred>
    void wait(mutex<T> &m, Pred pred) {
        (void)pred;
    }
};

#endif // CONDITION_VARIABLE_H
