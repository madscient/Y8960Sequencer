#include "fmengine.h"

namespace y8960 {

namespace {

template <typename Fn>
bool bind(const DynamicLibrary& lib, const char* name, Fn& out, std::string& error) {
    void* p = lib.symbol(name);
    if (!p) {
        error = std::string("missing ") + name;
        return false;
    }
    out = reinterpret_cast<Fn>(p);
    return true;
}

} // namespace

bool FmEngineLibrary::open(const std::filesystem::path& path, std::string& error) {
    if (!lib_.open(path, error)) return false;
    // 外部メモリの関数は仕様では任意のエクスポートだが、ADPCM メモリを複数の
    // チップで共有するのに要る（Y8960Chips::open）。無いライブラリは受け付けない。
    // FmEngine_GetMemoryCount は呼ばないが、有無を見る。これを持たないライブラリの
    // FmEngine_SetMemoryEx は第3引数がメモリの番号で、渡した名前の番地を番号として読む。
    decltype(&FmEngine_GetMemoryCount) getMemoryCount = nullptr;
    bool ok = bind(lib_, "FmEngine_Create",         create,         error) &&
              bind(lib_, "FmEngine_Destroy",        destroy,        error) &&
              bind(lib_, "FmEngine_AddChip",        addChip,        error) &&
              bind(lib_, "FmEngine_Write",          write,          error) &&
              bind(lib_, "FmEngine_SetGain",        setGain,        error) &&
              bind(lib_, "FmEngine_GetMemoryCount", getMemoryCount, error) &&
              bind(lib_, "FmEngine_SetMemoryEx",    setMemoryEx,    error) &&
              bind(lib_, "FmEngine_Generate",       generate,       error);
    if (!ok) lib_.close();
    return ok;
}

FmEngine::~FmEngine() {
    if (handle_) lib_->destroy(handle_);
}

bool FmEngine::create(const FmEngineLibrary& lib, uint32_t sampleRate, std::string& error) {
    lib_ = &lib;
    handle_ = lib.create(sampleRate);
    if (!handle_) {
        error = "cannot create an engine";
        return false;
    }
    return true;
}

bool FmEngine::addChip(const char* name, uint32_t clock, uint32_t& outId, std::string& error) {
    const FmResult r = lib_->addChip(handle_, name, clock, &outId);
    if (r != FM_OK) {
        error = std::string("cannot add chip ") + name + " (" + std::to_string(static_cast<int>(r)) + ")";
        return false;
    }
    return true;
}

bool FmEngine::setMemoryEx(uint32_t chipId, const char* memory, uint32_t base,
                           uint8_t* data, uint32_t size, FmMemoryAccess access) {
    return lib_->setMemoryEx(handle_, chipId, memory, base, data, size, access) == FM_OK;
}

} // namespace y8960
