#include <StdAfx.h>
#include <functional>
#include <chrono>
#include <thread>
#include "Debounce.h"

template<typename F>
void Debounce<F>::operator()(auto&&... args)
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

    auto callContext = new CallbackContext<F, decltype(args)...> { std::tuple<decltype(args)...>(std::forward<decltype(args)>(args)...), this->callback, };

    this->context = (PVOID)callContext;
    SetThreadpoolTimer(this->timer, &this->delay, 0, 0);

    while (!contextDeletionFutures.empty() && contextDeletionFutures.front().wait_for(std::chrono::nanoseconds(1)) == std::future_status::ready)
    {
        contextDeletionFutures.pop();
    }
}
