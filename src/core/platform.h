#pragma once

#include <filesystem>
#include <string>

namespace y8960 {

// 実行ファイルのあるフォルダ。取れなければ空。
std::filesystem::path executableDirectory();

class DynamicLibrary {
public:
    DynamicLibrary() = default;
    ~DynamicLibrary();
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    bool open(const std::filesystem::path& path, std::string& error);
    void close();
    void* symbol(const char* name) const;
    bool isOpen() const { return handle_ != nullptr; }

private:
    void* handle_ = nullptr;
};

// 各 OS の共有ライブラリのファイル名。base は "DSAemuEngine" など。
std::string sharedLibraryFileName(const std::string& base);

} // namespace y8960
