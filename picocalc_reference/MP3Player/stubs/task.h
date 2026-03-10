#ifndef TASK_H
#define TASK_H

class task {
public:
    enum State { RUNNING, SUSPENDED };

    task(const char* name, int stacksize) {}
    virtual ~task() {}
    virtual void run() {}
    bool isAlive() const { return true; }
    void start() {}
    void start(int prio, bool flag) {}
    int getState() const { return RUNNING; }
    void resume() {}
    void suspend() {}
    static void sleep_ms(int ms) {}
    static void start_scheduler() {}
};

#endif // TASK_H
