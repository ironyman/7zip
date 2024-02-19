// cl debounce.cpp /std:c++20 /EHsc /Zi
#include <windows.h>
#include <functional>
#include <cstdio>
#include <chrono>
#include <thread>
#include <queue>
#include <future>

template<typename F>
struct Debounce
{
    template<typename F, typename ...Args>
    struct CallbackContext {
        std::tuple<Args...> arguments;
        F callback;

        // Need to accept std::tuple<Args &&...> instead of std::tuple<Args...> somehow?
        // CallbackContext(std::tuple<Args...> *arguments, F callback) : arguments(arguments), callback(callback) {}
        ~CallbackContext()
        {
            printf("Destroying\n");
        }
    };
    F callback;
    PTP_TIMER timer;
    FILETIME delay;
    PVOID context;
    int delayMs;
    std::queue<std::future<void>> deletionThreads;

    Debounce(F callback, const int &delayMs) : context(NULL)
    {
        this->delayMs = delayMs;
        this->delay.dwHighDateTime = 0;
        this->delay.dwLowDateTime = -delayMs*1000*10;
        this->callback = callback;
        this->timer = nullptr;
    }
    ~Debounce()
    {
        while (!this->deletionThreads.empty())
        {
            this->deletionThreads.front().wait();
            this->deletionThreads.pop();
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
        printf("hello %p %p\n", callContext, callContext->callback);
        std::apply(callContext->callback, callContext->arguments);
    }

    void operator()(auto&&... args)
    {
        if (this->timer == nullptr)
        {
            this->timer = CreateThreadpoolTimer((PTP_TIMER_CALLBACK)MyTimerCallback<F, decltype(args)...>, (PVOID)&this->context, NULL);
            // this->timer = CreateThreadpoolTimer((PTP_TIMER_CALLBACK)MyTimerCallback<F, std::decay_t<decltype(args)>...>, (PVOID)&this->context, NULL);
            // don't use different type signatures, that will bork it
        }
        if (this->context != nullptr)
        {
            // You cannot exit this scope before this thread is done if you allocate it on stack, unless if you call detach.
            // std::thread([](Debounce *parent, CallbackContext<F, decltype(args)...> *callContext)
            // {
            //     // Avoid deleting the context while it's being used.
            //     std::this_thread::sleep_for(std::chrono::milliseconds(2*parent->delayMs));
            //     delete callContext;
            // }, this, (CallbackContext<F, decltype(args)...> *)this->context).detach();

            deletionThreads.emplace(std::async(std::launch::async, [](Debounce *parent, CallbackContext<F, decltype(args)...> *callContext)
            {
                // Avoid deleting the context while it's being used.
                std::this_thread::sleep_for(std::chrono::milliseconds(2*parent->delayMs));
                delete callContext;
            }, this, (CallbackContext<F, decltype(args)...> *)this->context));
        }

        // this->context = (PVOID)new CallbackContext<F, decltype(args)...> { std::tuple(std::forward<decltype(args)>(args)...), this->callback, };

        // auto callContext = new CallbackContext<F, decltype(args)...> { std::tuple(std::forward<decltype(args)>(args)...), this->callback, };
        auto callContext = new CallbackContext<F, decltype(args)...> {  std::tuple<decltype(args)...>(std::forward<decltype(args)>(args)...), this->callback, };

        this->context = (PVOID)callContext;
        printf("saved pointer %p %p\n", callContext, callContext->callback);
        // OK
        // std::apply(context->callback, context->arguments);

        // ((CallbackContext<F, decltype(args)...>*)context)->arguments = std::tuple(std::forward<decltype(args)>(args)...);
        SetThreadpoolTimer(this->timer, &this->delay, 0, 0);

        while (!deletionThreads.empty() && deletionThreads.front().wait_for(std::chrono::nanoseconds(1)) == std::future_status::ready)
        {
            deletionThreads.pop();
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