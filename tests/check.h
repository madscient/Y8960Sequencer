#pragma once

#include <cstdio>
#include <cstdlib>

#if defined(_MSC_VER)
#  include <crtdbg.h>
#endif

namespace check {

#if defined(_MSC_VER)
// Debug ビルドの CRT は、assert や STL の検査の失敗をモーダルのダイアログで出し、閉じられる
// まで試験が止まる。裏で回す試験では誰も閉じないので、標準エラーに書いて落ちるようにする。
inline bool quietCrt() {
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    return true;
}
inline const bool kQuietCrt = quietCrt();
#endif

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
