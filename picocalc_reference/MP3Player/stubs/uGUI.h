#ifndef UGUI_H
#define UGUI_H

class ili9488_drv; // forward declaration

class uGUI {
public:
    uGUI() {}
    uGUI(ili9488_drv &) {}
    void FontSelect(void* ) {}
    void SetForecolor(int) {}
    void SetBackcolor(int) {}
    void PutString(int, int, const char*) {}
    void PutChar(char, int, int, int, int, bool) {}
    void FillFrame(int, int, int, int, int) {}
    void ConsoleSetForecolor(int) {}
    void ConsoleSetBackcolor(int) {}
    void ConsolePutString(const char*) {}
};

#endif // UGUI_H
