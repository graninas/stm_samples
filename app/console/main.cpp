#include <iostream>
#include <mutex>
#include <list>
#include <thread>
#include <chrono>
#include <random>
#include <optional>

using namespace std;

#include <stm.h>

stm::STML<int> incrementCounter(const stm::TVar<int>& tCounter)
{
    stm::STML<stm::Unit> modified =
            stm::modifyTVar<int>(tCounter, [](int i) { return i + 1; });

    return stm::bind<stm::Unit, int>(modified,
                     [&](const stm::Unit&){ return stm::readTVar<int>(tCounter); });
}

// Thread runtime info
struct CounterRt
{
    int thread;                // Thread number
    std::mutex& logLock;       // Mutex for logging
    stm::Context& context;     // STM Context
    stm::TVar<int> tCounter;   // Shared counter TVar
};

// Thread worker function
void counterWorker(const CounterRt& rt)
{
    for (int i = 0; i < 50; ++i)
    {
        int counter = stm::atomically(rt.context, incrementCounter(rt.tCounter));
        std::lock_guard g(rt.logLock);
        std::cout << "thread: [" << rt.thread << "] counter: " << counter << std::endl;
    }
}

int main()
{
    stm::Context ctx;
    stm::TVar<int> tCounter = stm::newTVarIO(ctx, 0);

    std::mutex logLock;

    std::vector<std::thread> threads;
    threads.push_back(std::thread(counterWorker, CounterRt {1, logLock, ctx, tCounter}));
    threads.push_back(std::thread(counterWorker, CounterRt {2, logLock, ctx, tCounter}));

    for (auto& t: threads)
        t.join();

    return 0;
}
