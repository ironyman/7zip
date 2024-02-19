#include <iostream>

template<typename Func>
void genericFunction(Func func) {
    // Call the function with some arguments
    // For demonstration, let's pass 5 and 10
    std::cout << "Result: " << func(5, 10) << std::endl;
}

// Example functions with different signatures
int add(int a, int b) {
    return a + b;
}

double multiply(double a, double b) {
    return a * b;
}

int main() {
    // Call genericFunction with different functions
    genericFunction(add);       // Passing a function pointer
    genericFunction(multiply);  // Passing a function pointer
    genericFunction([](int a, int b) { return a - b; });  // Passing a lambda

    return 0;
}
