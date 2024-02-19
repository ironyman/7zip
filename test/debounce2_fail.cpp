// cl debounce.cpp /std:c++20 /EHsc
#include <windows.h>
#include <functional>
#include <cstdio>
#include <chrono>
#include <thread>

template<typename F, typename ...Args>
struct DebounceContext {
    std::tuple<Args...> arguments;
    F callback;
    PTP_TIMER timer;
    FILETIME delay;
};

template<typename F, typename ...Args>
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
    auto context = reinterpret_cast<DebounceContext<F, Args...>*>(Parameter);
    std::apply(context->callback, context->arguments);
}


template<typename F>
auto debounce(F callback, const int &delayMs)
{
    auto context = new DebounceContext<F, int>;
    context->delay.dwHighDateTime = 0;
    context->delay.dwLowDateTime = -delayMs*1000*10;
    context->callback = callback;
    context->timer = nullptr;

    return [context, callback](auto&&... args) {
        if (context->timer == nullptr)
        {
            context->timer = CreateThreadpoolTimer((PTP_TIMER_CALLBACK)MyTimerCallback<F, std::decay_t<decltype(args)>...>, (PVOID)context, NULL);
        }
        // if we pass arguments different than what DebounceContext expects then it fails
        ((DebounceContext<F, decltype(args)...>*)context)->arguments = std::make_tuple(std::forward<decltype(args)>(args)...);
        SetThreadpoolTimer(context->timer, &context->delay, 0, 0);
    };
}


int print(int a, int b)
{
    printf("hello %d %d\n", a, b);
    return 0;
}

int main()
{
    auto debounced = debounce(print, 100);
    debounced(1, 2);
    debounced(1, 1);
    debounced(1, 3);
    debounced(2, 6);
    debounced(1, 5);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    debounced(4, 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    return 0;
}