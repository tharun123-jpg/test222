// =============================================================================
//  tests/test_main.cpp -- test registry and runner
// =============================================================================
#include <cstring>
#include <exception>

#include "test_framework.hpp"

namespace mgtk_test {

int g_checks = 0;
int g_failures = 0;
const char* g_current_test = "";

std::vector<TestCase>& registry() {
    // Function-local static: avoids the static-initialisation-order problem
    // that a namespace-scope vector would introduce, since the Registrar
    // objects in each translation unit run before main().
    static std::vector<TestCase> instance;
    return instance;
}

void report_check(bool passed, const char* expr, const char* file, int line,
                  const std::string& detail) {
    ++g_checks;
    if (passed) return;
    ++g_failures;

    std::fprintf(stderr, "FAIL %s\n  at %s:%d\n  check: %s\n", g_current_test, file,
                 line, expr);
    if (!detail.empty()) {
        std::fprintf(stderr, "  detail: %s\n", detail.c_str());
    }
}

}  // namespace mgtk_test

int main(int argc, char** argv) {
    using namespace mgtk_test;

    // Optional positional filter: run only tests whose name contains the
    // argument. Handy when iterating on one effect.
    const char* filter = (argc > 1) ? argv[1] : nullptr;

    auto& all = registry();
    std::vector<TestCase> selected;
    for (const auto& tc : all) {
        if (filter == nullptr || std::strstr(tc.name, filter) != nullptr) {
            selected.push_back(tc);
        }
    }

    std::printf("Motion Graphics Toolkit -- core test suite\n");
    if (filter) {
        std::printf("running %zu of %zu tests matching \"%s\"\n\n", selected.size(),
                    all.size(), filter);
    } else {
        std::printf("running %zu tests\n\n", selected.size());
    }

    int passed = 0;
    int failed = 0;

    for (const auto& tc : selected) {
        const int failures_before = g_failures;
        g_current_test = tc.name;

        try {
            tc.fn();
        } catch (const FatalFailure&) {
            // Already reported by fail_fatal.
        } catch (const std::exception& e) {
            ++g_failures;
            std::fprintf(stderr, "FAIL %s\n  uncaught exception: %s\n", tc.name,
                         e.what());
        } catch (...) {
            ++g_failures;
            std::fprintf(stderr, "FAIL %s\n  uncaught non-standard exception\n",
                         tc.name);
        }

        if (g_failures == failures_before) {
            std::printf("  ok    %s\n", tc.name);
            ++passed;
        } else {
            std::printf("  FAIL  %s\n", tc.name);
            ++failed;
        }
    }

    std::printf("\n%d passed, %d failed, %d checks total\n", passed, failed,
                g_checks);
    return failed == 0 ? 0 : 1;
}
