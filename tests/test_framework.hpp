// =============================================================================
//  tests/test_framework.hpp -- a deliberately tiny test harness
//
//  No external dependencies. The whole point of this repository's test suite is
//  that it builds and runs with nothing more than a C++ compiler, so that the
//  pixel algorithms can be developed and verified without an After Effects
//  installation anywhere in sight.
// =============================================================================
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace mgtk_test {

struct TestCase {
    const char* name;
    const char* file;
    void (*fn)();
};

// Defined in test_main.cpp.
std::vector<TestCase>& registry();
void report_check(bool passed, const char* expr, const char* file, int line,
                  const std::string& detail);

struct Registrar {
    Registrar(const char* name, const char* file, void (*fn)()) {
        registry().push_back(TestCase{name, file, fn});
    }
};

// Accumulated state, read by the runner in test_main.cpp.
extern int g_checks;
extern int g_failures;
extern const char* g_current_test;

// ---------------------------------------------------------------------------
//  Fatal failure support
// ---------------------------------------------------------------------------
struct FatalFailure {};

inline void fail_fatal(const char* expr, const char* file, int line,
                       const std::string& detail) {
    report_check(false, expr, file, line, detail);
    throw FatalFailure{};
}

// ---------------------------------------------------------------------------
//  Value formatting, so failures print something you can actually act on
// ---------------------------------------------------------------------------
template <typename T>
inline std::string to_string_value(const T& v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

inline std::string to_string_value(float v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
    return buf;
}

inline std::string to_string_value(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.12g", v);
    return buf;
}

}  // namespace mgtk_test

// ---------------------------------------------------------------------------
//  Registration macro
// ---------------------------------------------------------------------------
#define MGTK_TEST(test_name)                                                \
    static void test_name();                                                \
    static ::mgtk_test::Registrar mgtk_reg_##test_name(#test_name, __FILE__, \
                                                       test_name);          \
    static void test_name()

// ---------------------------------------------------------------------------
//  Assertions
// ---------------------------------------------------------------------------
#define CHECK(cond)                                                          \
    ::mgtk_test::report_check(static_cast<bool>(cond), #cond, __FILE__,      \
                              __LINE__, std::string())

#define CHECK_MSG(cond, msg)                                                 \
    ::mgtk_test::report_check(static_cast<bool>(cond), #cond, __FILE__,      \
                              __LINE__, (msg))

#define REQUIRE(cond)                                                        \
    do {                                                                     \
        if (!static_cast<bool>(cond)) {                                       \
            ::mgtk_test::fail_fatal(#cond, __FILE__, __LINE__, std::string()); \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        const auto mgtk_a_ = (a);                                            \
        const auto mgtk_b_ = (b);                                            \
        ::mgtk_test::report_check(                                           \
            mgtk_a_ == mgtk_b_, #a " == " #b, __FILE__, __LINE__,            \
            ::mgtk_test::to_string_value(mgtk_a_) + " vs " +                 \
                ::mgtk_test::to_string_value(mgtk_b_));                      \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                \
    do {                                                                     \
        const double mgtk_a_ = static_cast<double>(a);                       \
        const double mgtk_b_ = static_cast<double>(b);                       \
        const double mgtk_t_ = static_cast<double>(tol);                     \
        ::mgtk_test::report_check(                                           \
            std::fabs(mgtk_a_ - mgtk_b_) <= mgtk_t_,                         \
            #a " ~= " #b, __FILE__, __LINE__,                                \
            "expected " + ::mgtk_test::to_string_value(mgtk_b_) +            \
                ", got " + ::mgtk_test::to_string_value(mgtk_a_) +           \
                " (tol " + ::mgtk_test::to_string_value(mgtk_t_) + ") "      \
                "[actual: " #a ", expected: " #b "]");                       \
    } while (0)

// Assert that a float is finite (not NaN, not infinity). Pixel code that
// divides by a radius or a weight hits this constantly.
#define CHECK_FINITE(v)                                                      \
    ::mgtk_test::report_check(std::isfinite(static_cast<double>(v)),         \
                              #v " is finite", __FILE__, __LINE__,           \
                              ::mgtk_test::to_string_value(v))
