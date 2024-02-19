#include <iostream>
#include <functional>

template<typename F, typename... Args>
auto genericLambda(F func, Args... args) {
    return [=]() mutable {
        return func(args...);
    };
}

// Example function to be called
void exampleFunction(int a, double b, const std::string& c) {
    std::cout << "Called with: " << a << ", " << b << ", " << c << std::endl;
}


template<typename Func>
auto genericLambda2(Func func) {
    return [func](auto&&... args) {
        return func(std::forward<decltype(args)>(args)...);
    };
}

int main() {
    // Create a generic lambda that takes any number of parameters and passes them to exampleFunction
    auto lambda = genericLambda(exampleFunction, 42, 3.14, "Hello");

    // Call the lambda
    lambda();

    // Create a generic lambda that takes any number of parameters and passes them to exampleFunction
    auto lambda2 = genericLambda2(exampleFunction);

    // Call the lambda with variadic arguments
    lambda2(42, 3.111, "Hello");
    // lambda2(100, 2.71, "World", 5); // Extra arguments are ignored


    return 0;
}
