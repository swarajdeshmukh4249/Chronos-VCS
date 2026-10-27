#include <exception>
#include <iostream>

#include "TestHarness.h"

int main() {
    int crashed = 0;
    for (const harness::TestCase& test : harness::registry()) {
        try {
            test.run();
        } catch (const std::exception& e) {
            ++crashed;
            std::cerr << test.name << ": unexpected exception: " << e.what() << "\n";
        }
    }
    const int failed = harness::failures() + crashed;
    std::cout << harness::registry().size() << " tests, " << failed << " failures\n";
    return failed == 0 ? 0 : 1;
}
