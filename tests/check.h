#pragma once

#include <cstdio>

namespace check {

inline int& failures() {
    static int n = 0;
    return n;
}

inline void report(bool ok, const char* expr, const char* file, int line) {
    if (!ok) {
        std::fprintf(stderr, "%s:%d: FAILED: %s\n", file, line, expr);
        ++failures();
    }
}

inline int finish(const char* name) {
    if (failures() == 0) {
        std::printf("%s: ok\n", name);
        return 0;
    }
    std::printf("%s: %d failed\n", name, failures());
    return 1;
}

} // namespace check

#define CHECK(expr) ::check::report(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
