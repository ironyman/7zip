// cl debounce.cpp /std:c++20 /EHsc
#include <windows.h>
#include <functional>
#include <cstdio>
#include <chrono>
#include <thread>

template<typename R, typename ...Args>
struct DebounceContext {
    std::function<void(std::function<R(Args(...args))>)> argsCache;
    std::function<R(Args(...args))> callback;
    PTP_TIMER timer;
    FILETIME delay;
};

template<typename R, typename ...Args>
VOID
CALLBACK
MyTimerCallback(
    PTP_CALLBACK_INSTANCE Instance,
    PVOID                 Parameter,
    PTP_TIMER             Timer
    )
{
    // Instance, Parameter, and Timer not used in this example.
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Parameter);
    UNREFERENCED_PARAMETER(Timer);
    auto context = reinterpret_cast<DebounceContext<R, Args...>*>(Parameter);
    context->argsCache(context->callback);
}

// function used to cache user arguments to be able to use them once timer times out
template<typename R, class ...Args>
void debounce_args_cache(const std::function<R(Args(...args))> &callback, Args(&...args))
{
    callback(args...);
}

template<typename R, class ...Args>
std::function<R(Args(...args))> debounce(const std::function<R(Args(...args))> &callback, const int &delayMs)
{
    auto context = new DebounceContext<R, Args...>;
    context->timer = CreateThreadpoolTimer((PTP_TIMER_CALLBACK)MyTimerCallback<R, Args...>, (PVOID)context, NULL);
    context->delay.dwHighDateTime = 0;
    context->delay.dwLowDateTime = -delayMs*1000*10;
    context->callback = callback;

    return [context, callback](Args(...args2)) {
        SetThreadpoolTimer(context->timer, &context->delay, 0, 0);

        context->argsCache = std::bind(
            debounce_args_cache<R, Args...>,
            std::placeholders::_1 /* for callback*/,
            args2...
        );

        // return default constructed return type (just to support any return type)
        return R();
    };
}

int print(int a)
{
    printf("hello %d\n", a);
    return 0;
}

int main()
{
    auto debounced = debounce(std::function<int(int)>(print), 100);
    debounced(2);
    debounced(1);
    debounced(3);
    debounced(6);
    debounced(4);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    debounced(3);

    return 0;
}