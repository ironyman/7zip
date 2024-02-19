#include <iostream>
#include <tuple>
#include <functional>

template<typename Func>
auto genericLambda(Func func) {
    return [func](auto&&... args) {
        // Save the arguments to a tuple
        auto arguments = std::make_tuple(std::forward<decltype(args)>(args)...);

        // You can perform any other operations here before calling the function

        // Call the function with saved arguments later
        return std::apply(func, arguments);
    };
}

// Example function to be called
void exampleFunction(int a, double b, const std::string& c) {
    std::cout << "Called with: " << a << ", " << b << ", " << c << std::endl;
}

int main() {
    // Create a generic lambda that takes any number of parameters and passes them to exampleFunction
    auto lambda = genericLambda(exampleFunction);

    // Call the lambda with variadic arguments
    lambda(42, 3.14, "Hello");

    return 0;
}

#if 0
#include <functional>

// Forward declaration of std::tuple
namespace std {
    template<typename... Types>
    class tuple;
}

template<typename Func>
auto genericLambda(Func func) {
    return [func](auto&&... args) {
        // Save the arguments to a tuple
        std::tuple<std::decay_t<decltype(args)>...> arguments(std::forward<decltype(args)>(args)...);

        // You can perform any other operations here before calling the function

        // Call the function with saved arguments later
        return std::apply(func, arguments);
    };
}
#endif