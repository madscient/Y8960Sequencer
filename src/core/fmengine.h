#pragma once
// FmEngineApi 準拠の共有ライブラリを実行時に読み込む。
//
// Y8960emu・EPSGemuEngine・DSAemuEngine はどれも同じ名前の関数を公開するので、
// ヘッダを取り込んでリンクすることはできない。関数の形だけをここに写し、
// ライブラリごとに関数ポインタで呼ぶ。形の出典は各リポジトリの src/FmEngineApi.h。

#include "platform.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace y8960 {

#if defined(_WIN32)
#  define Y8960_FMENGINE_CALL __cdecl
#else
#  define Y8960_FMENGINE_CALL
#endif

enum FmEngineResult : int32_t {
    kFmOk = 0,
};

enum FmEngineMemory : int32_t {
    kFmMemAdpcmB = 2,
};

class FmEngineLibrary {
public:
    bool open(const std::filesystem::path& path, std::string& error);
    bool isOpen() const { return lib_.isOpen(); }

    using Handle = void*;

    Handle      (Y8960_FMENGINE_CALL* create)(uint32_t sampleRate) = nullptr;
    void        (Y8960_FMENGINE_CALL* destroy)(Handle) = nullptr;
    int32_t     (Y8960_FMENGINE_CALL* addChip)(Handle, const char* name, uint32_t clock, uint32_t* outId) = nullptr;
    int32_t     (Y8960_FMENGINE_CALL* write)(Handle, uint32_t chipId, uint8_t reg, uint8_t value, uint32_t port) = nullptr;
    int32_t     (Y8960_FMENGINE_CALL* setGain)(Handle, uint32_t chipId, float gainL, float gainR) = nullptr;
    int32_t     (Y8960_FMENGINE_CALL* setMemory)(Handle, uint32_t chipId, int32_t memType, const uint8_t* data, uint32_t size) = nullptr;
    int32_t     (Y8960_FMENGINE_CALL* generate)(Handle, float* outL, float* outR, uint32_t samples) = nullptr;

private:
    DynamicLibrary lib_;
};

// 1つのライブラリから作ったエンジン1つ。チップを足して鳴らす。
class FmEngine {
public:
    FmEngine() = default;
    ~FmEngine();
    FmEngine(const FmEngine&) = delete;
    FmEngine& operator=(const FmEngine&) = delete;

    bool create(const FmEngineLibrary& lib, uint32_t sampleRate, std::string& error);
    bool addChip(const char* name, uint32_t clock, uint32_t& outId, std::string& error);
    void write(uint32_t chipId, uint8_t reg, uint8_t value) { lib_->write(handle_, chipId, reg, value, 0); }
    void setGain(uint32_t chipId, float l, float r) { lib_->setGain(handle_, chipId, l, r); }
    bool setMemory(uint32_t chipId, const uint8_t* data, uint32_t size);
    void generate(float* l, float* r, uint32_t n) { lib_->generate(handle_, l, r, n); }

private:
    const FmEngineLibrary* lib_    = nullptr;
    FmEngineLibrary::Handle handle_ = nullptr;
};

} // namespace y8960
