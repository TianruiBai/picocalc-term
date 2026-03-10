#ifndef FF_H
#define FF_H

#include <cstdint>

class FatFs {
public:
    enum FRESULT { FR_OK = 0 };
    struct DIR {};
    struct FILEINFO { char fname[256]; int fsize; };
    struct FILE {};

    FatFs() {}
    // accept arbitrary driver object
    template<typename T>
    FatFs(T&) {}

    FRESULT mount(int) { return FR_OK; }
    FRESULT findfirst(DIR*, FILEINFO*, const char*, const char*) { return FR_OK; }
    FRESULT findnext(DIR*, FILEINFO*) { return FR_OK; }
    void closedir(DIR*) {}
    void close(FILE*) {}
    FRESULT open(FILE*, const char*, int) { return FR_OK; }
    FRESULT read(FILE*, void*, int, uint16_t*) { return FR_OK; }
};

// file attribute flags used by this project
#define FA_OPEN_EXISTING 0x01
#define FA_READ 0x02

inline bool f_eof(FatFs::FILE*) { return true; }

#endif // FF_H
