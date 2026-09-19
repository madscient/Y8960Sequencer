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
    bool ok = bind(lib_, "FmEngine_Create",    create,    error) &&
              bind(lib_, "FmEngine_Destroy",   destroy,   error) &&
              bind(lib_, "FmEngine_AddChip",   addChip,   error) &&
              bind(lib_, "FmEngine_Write",     write,     error) &&
              bind(lib_, "FmEngine_SetGain",   setGain,   error) &&
              bind(lib_, "FmEngine_SetMemory", setMemory, error) &&
              bind(lib_, "FmEngine_Generate",  generate,  error);
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
    const int32_t r = lib_->addChip(handle_, name, clock, &outId);
    if (r != kFmOk) {
        error = std::string("cannot add chip ") + name + " (" + std::to_string(r) + ")";
        return false;
    }
    return true;
}

bool FmEngine::setMemory(uint32_t chipId, const uint8_t* data, uint32_t size) {
    return lib_->setMemory(handle_, chipId, kFmMemAdpcmB, data, size) == kFmOk;
}

} // namespace y8960
