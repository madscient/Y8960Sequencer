#pragma once
// FmEngineApi 準拠の共有ライブラリを実行時に読み込む。
//
// DSAemuEngine・YMEngine はどちらも同じ名前の関数を公開するので、リンクして呼ぶことは
// できない。FmEngineApi.h からは型と関数の形だけを取り、ライブラリごとに関数ポインタで呼ぶ。
// FmEngineApi.h は FMEngineTest の include/FmEngineApi.h の写しで、ここでは編集しない。

#include "FmEngineApi.h"
#include "platform.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace y8960 {

class FmEngineLibrary {
public:
    bool open(const std::filesystem::path& path, std::string& error);
    bool isOpen() const { return lib_.isOpen(); }

    decltype(&FmEngine_Create)      create      = nullptr;
    decltype(&FmEngine_Destroy)     destroy     = nullptr;
    decltype(&FmEngine_AddChip)     addChip     = nullptr;
    decltype(&FmEngine_Write)       write       = nullptr;
    decltype(&FmEngine_SetGain)     setGain     = nullptr;
    decltype(&FmEngine_SetMemoryEx) setMemoryEx = nullptr;
    decltype(&FmEngine_Generate)    generate    = nullptr;

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
    // port はアドレス端子 A1。2組のレジスタを持つチップの2組目が 1。
    void write(uint32_t chipId, uint8_t reg, uint8_t value, uint8_t port) {
        lib_->write(handle_, chipId, reg, value, port);
    }
    void setGain(uint32_t chipId, float l, float r) { lib_->setGain(handle_, chipId, l, r); }
    // data をチップの外部メモリ memory の [base, base + size) に割り当てる。memory は
    // エンジンがチップごとに持つ名前。RAM として割り当てると、エンジンは複製せずに
    // その場で読み書きする。data はエンジンを壊すまで手放さないこと。
    bool setMemoryEx(uint32_t chipId, const char* memory, uint32_t base,
                     uint8_t* data, uint32_t size, FmMemoryAccess access);
    void generate(float* l, float* r, uint32_t n) { lib_->generate(handle_, l, r, n); }

private:
    const FmEngineLibrary* lib_ = nullptr;
    FmEngineHandle handle_      = nullptr;
};

} // namespace y8960
