#ifndef ZIP7_INC_DEBOUNCE_H
#define ZIP7_INC_DEBOUNCE_H
#include "../../../Common/MyWindows.h"
#include <queue>
// Disable noisy stl warnings. https://developercommunity.visualstudio.com/t/stl-header-causes-c4355-and-c5204/1046989
#pragma warning( disable  : 4355 )
#pragma warning( disable  : 5204 )
#pragma warning( disable  : 5204 )
#pragma warning( disable  : 5220 )
#include <future>
#include <mutex>
#include <tuple>
#include <functional>
#include <chrono>
#include <thread>

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

    // This must be defined in class body.
    // Putting this in cpp file will error.
    // Debounce.cpp(18): error C2768: 'Debounce<F>::TimerCallback': illegal use of explicit template arguments
    // template<typename F>
    // template<typename G, typename ...Args>
    // static
    // VOID
    // CALLBACK
    // Debounce<F>::TimerCallback<G, Args...>(
    //     PTP_CALLBACK_INSTANCE Instance,
    //     PVOID                 Parameter,
    //     PTP_TIMER             Timer
    // )
    // {
    template<typename G, typename ...Args>
    static
    VOID
    CALLBACK
    TimerCallback(
        PTP_CALLBACK_INSTANCE Instance,
        PVOID                 Parameter,
        PTP_TIMER             Timer
    )
    {
        UNREFERENCED_PARAMETER(Instance);
        UNREFERENCED_PARAMETER(Timer);
        auto callContext = *reinterpret_cast<CallbackContext<G, Args...>**>(Parameter);
        std::apply(callContext->callback, callContext->arguments);
    }

    void operator()(auto... args)
    {
        std::lock_guard<std::mutex> guard(this->selfDeletionMutex);

        if (this->timer == nullptr)
        {
            this->timer = CreateThreadpoolTimer((PTP_TIMER_CALLBACK)TimerCallback<F, decltype(args)...>, (PVOID)&this->context, NULL);
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

        auto callContext = new CallbackContext<F, decltype(args)...>{std::tuple<decltype(args)...>(std::forward<decltype(args)>(args)...), this->callback};

        this->context = (PVOID)callContext;
        SetThreadpoolTimer(this->timer, &this->delay, 0, 0);

        while (!contextDeletionFutures.empty() && contextDeletionFutures.front().wait_for(std::chrono::nanoseconds(1)) == std::future_status::ready)
        {
            contextDeletionFutures.pop();
        }
    }
};

#endif