#include "platform.h"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#elif defined(__APPLE__)
#  include <dlfcn.h>
#  include <mach-o/dyld.h>
#  include <climits>
#  include <cstdint>
#else
#  include <dlfcn.h>
#  include <unistd.h>
#  include <climits>
#endif

#include <vector>

namespace y8960 {

std::filesystem::path executableDirectory() {
#if defined(_WIN32)
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) return std::filesystem::path(std::wstring(buf.data(), n)).parent_path();
        buf.resize(buf.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buf(size + 1);
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    std::error_code ec;
    auto p = std::filesystem::canonical(buf.data(), ec);
    return ec ? std::filesystem::path(buf.data()).parent_path() : p.parent_path();
#else
    std::vector<char> buf(PATH_MAX);
    const ssize_t n = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (n <= 0) return {};
    return std::filesystem::path(std::string(buf.data(), static_cast<size_t>(n))).parent_path();
#endif
}

DynamicLibrary::~DynamicLibrary() { close(); }

bool DynamicLibrary::open(const std::filesystem::path& path, std::string& error) {
    close();
#if defined(_WIN32)
    // 依存 DLL もライブラリ自身のフォルダから探させる。
    HMODULE h = LoadLibraryExW(path.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!h) {
        error = "読み込めません (Windows のエラー " + std::to_string(GetLastError()) + ")";
        return false;
    }
    handle_ = h;
#else
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        const char* msg = dlerror();
        error = msg ? msg : "読み込めません";
        return false;
    }
#endif
    return true;
}

void DynamicLibrary::close() {
    if (!handle_) return;
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
}

void* DynamicLibrary::symbol(const char* name) const {
    if (!handle_) return nullptr;
#if defined(_WIN32)
    return static_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name));
#else
    return dlsym(handle_, name);
#endif
}

std::string sharedLibraryFileName(const std::string& base) {
#if defined(_WIN32)
    return base + ".dll";
#elif defined(__APPLE__)
    return "lib" + base + ".dylib";
#else
    return "lib" + base + ".so";
#endif
}

} // namespace y8960
