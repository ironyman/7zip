// cl debounce.cpp /std:c++20 /EHsc /Zi
#include <windows.h>
#include <functional>
#include <cstdio>
#include <chrono>
#include <thread>
#include <queue>
#include <future>
#include <mutex>

template<typename F>
struct Debounce
{
    template<typename F, typename ...Args>
    struct CallbackContext {
        std::tuple<Args...> arguments;
        F callback;
    };
    F callback;
    PTP_TIMER timer;
    FILETIME delay;
    PVOID context;
    int delayMs;
    std::queue<std::future<void>> contextDeletionFutures;
    // Actually it just synchronizes accesses to the queue because this object can't be deleted while it's being used.
    // There needs to be a lock outside of this object to handle that.
    std::mutex selfDeletionMutex;

    Debounce(F callback, const int &delayMs)
    {
        this->delayMs = delayMs;
        this->delay.dwHighDateTime = 0;
        this->delay.dwLowDateTime = -delayMs*1000*10;
        this->callback = callback;
        this->timer = nullptr;
        this->context = nullptr;
    }
    ~Debounce()
    {
        std::lock_guard<std::mutex> guard(this->selfDeletionMutex);
        while (!this->contextDeletionFutures.empty())
        {
            this->contextDeletionFutures.front().wait();
            this->contextDeletionFutures.pop();
        }
    }

    template<typename F, typename ...Args>
    static
    VOID
    CALLBACK
    MyTimerCallback(
        PTP_CALLBACK_INSTANCE Instance,
        PVOID                 Parameter,
        PTP_TIMER             Timer
        )
    {
        UNREFERENCED_PARAMETER(Instance);
        UNREFERENCED_PARAMETER(Timer);
        auto callContext = *reinterpret_cast<CallbackContext<F, Args...>**>(Parameter);
        std::apply(callContext->callback, callContext->arguments);
    }

    void operator()(auto&&... args)
    {
        std::lock_guard<std::mutex> guard(this->selfDeletionMutex);

        if (this->timer == nullptr)
        {
            this->timer = CreateThreadpoolTimer((PTP_TIMER_CALLBACK)MyTimerCallback<F, decltype(args)...>, (PVOID)&this->context, NULL);
        }
        if (this->context != nullptr)
        {
            contextDeletionFutures.emplace(std::async(std::launch::async, [](Debounce *parent, CallbackContext<F, decltype(args)...> *callContext)
            {
                // Avoid deleting the context while it's being used.
                std::this_thread::sleep_for(std::chrono::milliseconds(2*parent->delayMs));
                delete callContext;
            }, this, (CallbackContext<F, decltype(args)...> *)this->context));
        }

        auto callContext = new CallbackContext<F, decltype(args)...> { std::tuple<decltype(args)...>(std::forward<decltype(args)>(args)...), this->callback, };

        this->context = (PVOID)callContext;
        SetThreadpoolTimer(this->timer, &this->delay, 0, 0);

        while (!contextDeletionFutures.empty() && contextDeletionFutures.front().wait_for(std::chrono::nanoseconds(1)) == std::future_status::ready)
        {
            contextDeletionFutures.pop();
        }
    }
};

int print(int a, int b)
{
    printf("hello %d %d\n", a, b);
    return 0;
}

int main()
{
    Debounce debouncedPrint(print, 100);
    debouncedPrint(1, 2);
    debouncedPrint(1, 1);
    debouncedPrint(1, 3);
    debouncedPrint(2, 6);
    debouncedPrint(1, 5);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    debouncedPrint(4, 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    return 0;
}