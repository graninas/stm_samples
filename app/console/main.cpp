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
    return stm::modifyTVarRet<int>(tCounter, [](int i) { return i + 1; });
}

stm::STML<std::optional<int>> eventualIncrement(
        const std::function<bool(int)>& cond,
        const stm::TVar<int>& tCounter)
{
    std::function<stm::STML<std::optional<int>>(int)> f = [&](int counter)
    {
        if (cond(counter))
        {
            return stm::with<int, std::optional<int>>
                        (incrementCounter(tCounter),
                         [](int i){ return stm::pure(std::make_optional(i)); });
        }
        else
        {
            return stm::pure<std::optional<int>>(std::nullopt);
        }
    };

    return stm::withTVar<int, std::optional<int>>(tCounter, f);
}

stm::STML<int> guaranteedIncrement(
        const std::function<bool(int)>& cond,
        const stm::TVar<int>& tCounter)
{
    std::function<stm::STML<int>(int)> f = [&](int counter)
    {
        if (cond(counter))
        {
            return stm::with<int, int>(incrementCounter(tCounter), stm::mPure);
        }
        else
        {
            return stm::retry<int>();
        }
    };

    return stm::withTVar<int, int>(tCounter, f);
}

stm::STML<stm::Unit> fibonacci(const stm::TVar<int>& tFib0,
                               const stm::TVar<int>& tFib1)
{
    std::function<stm::STML<stm::Unit>(int, int)> f = [&](int fib0, int fib1)
    {
        return stm::bothVoided(stm::writeTVar(tFib0, fib1),
                               stm::writeTVar(tFib1, fib0 + fib1));
    };

    return stm::withTVars(tFib0, tFib1, f);
}

struct FibRt
{
    stm::Context& context;
    stm::TVar<int> tCounter;
    stm::TVar<int> tFib0;
    stm::TVar<int> tFib1;
    int limit;
};

struct CounterRt
{
    std::mutex& logLock;
    stm::Context& context;
    stm::TVar<int> tCounter;
    int limit;
    bool odd;
    int microsecsTimeout;
};

const auto oddCond  = [](int counter) { return (counter % 2) != 0; };
const auto evenCond = [](int counter) { return (counter % 2) == 0; };

void eventualCounterWorker(const CounterRt& rt)
{
    int i = 0;
    while (true)
    {
        std::optional<int> incremented =
                stm::atomically(rt.context,
                                eventualIncrement(rt.odd ? oddCond : evenCond, rt.tCounter));
        {
            std::lock_guard g(rt.logLock);
            std::cout << "[" << i << (rt.odd ? "] odd" : "] even") << ": "
                      << (incremented.has_value() ? std::to_string(incremented.value())
                                                  : "not incremented.") << std::endl;
        }

        i++;

        if (i >= rt.limit)
            return;

        std::chrono::microseconds interval(rt.microsecsTimeout);
        std::this_thread::sleep_for(interval);
    }
}

void guaranteedCounterWorker(const CounterRt& rt)
{
    int i = 0;
    while (true)
    {
        int incremented = stm::atomically(
                    rt.context,
                    guaranteedIncrement(rt.odd ? oddCond : evenCond, rt.tCounter));
        {
            std::lock_guard g(rt.logLock);
            std::cout << "[" << i << (rt.odd ? "] odd" : "] even") << ": "
                      << incremented << std::endl;
        }

        i++;

        if (i >= rt.limit)
            return;

        std::chrono::microseconds interval(rt.microsecsTimeout);
        std::this_thread::sleep_for(interval);
    }
}

void run2CompetingThreads()
{
    stm::Context ctx;
    stm::TVar<int> tCounter = stm::newTVarIO(ctx, 0);

    std::mutex logLock;

    std::cout << "--> Eventual Counter: start." << endl;

    std::vector<std::thread> threads;
    threads.push_back(std::thread(eventualCounterWorker, CounterRt {logLock, ctx, tCounter, 25, true , 300}));
    threads.push_back(std::thread(eventualCounterWorker, CounterRt {logLock, ctx, tCounter, 25, false, 300}));

    for (auto& t: threads)
        t.join();

    int result = stm::atomically(ctx, stm::readTVar(tCounter));
    std::cout << "--> Eventual Counter: threads ended. Result: " << result << endl;

    std::cout << "--> Guaranteed Counter: start." << endl;

    threads.clear();
    stm::atomically(ctx, writeTVar(tCounter, 0));

    threads.push_back(std::thread(guaranteedCounterWorker, CounterRt {logLock, ctx, tCounter, 25, true , 400}));
    threads.push_back(std::thread(guaranteedCounterWorker, CounterRt {logLock, ctx, tCounter, 25, false, 400}));

    for (auto& t: threads)
        t.join();

    result = stm::atomically(ctx, stm::readTVar(tCounter));
    std::cout << "--> Guaranteed Counter: threads ended. Result: " << result << endl;
}

int main()
{
    run2CompetingThreads();
    return 0;
}
