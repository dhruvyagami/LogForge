#include "test_framework.hpp"
#include <iostream>

int main() {
    auto& tests = test_framework::get_registry();
    std::cout << "Running " << tests.size() << " unit tests...\n";

    size_t passed = 0;
    size_t failed = 0;

    for (const auto& test : tests) {
        std::cout << "  [RUN] " << test.name << " ... ";
        try {
            test.func();
            std::cout << "PASSED\n";
            passed++;
        } catch (const std::exception& e) {
            std::cout << "FAILED!\n";
            std::cerr << "        " << e.what() << "\n";
            failed++;
        }
    }

    std::cout << "\n===============================\n";
    std::cout << "Test Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "===============================\n";

    return failed > 0 ? 1 : 0;
}
