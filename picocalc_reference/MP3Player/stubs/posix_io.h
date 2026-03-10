#ifndef POSIX_IO_H
#define POSIX_IO_H

#include "uart_rp2040.h"

class posix_io {
public:
    static posix_io inst;
    void register_stdin(uart_rp2040 &) {}
    void register_stdout(uart_rp2040 &) {}
    void register_stderr(uart_rp2040 &) {}
};

// instantiate static member
posix_io posix_io::inst;

#endif // POSIX_IO_H
