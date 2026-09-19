#include "wavwriter.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace y8960 {

namespace {

constexpr uint16_t kChannels      = 2;
constexpr uint16_t kBitsPerSample = 16;
constexpr uint32_t kHeaderSize    = 44;
constexpr uint64_t kMaxDataBytes  = 0xFFFFFFFFull - (kHeaderSize - 8);   // RIFF の長さは 32bit

void put16(std::ofstream& f, uint16_t v) {
    const char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
    f.write(b, 2);
}

void put32(std::ofstream& f, uint32_t v) {
    const char b[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                       static_cast<char>((v >> 16) & 0xFF), static_cast<char>(v >> 24)};
    f.write(b, 4);
}

int16_t toPcm(float v) {
    const float c = std::clamp(v, -1.0f, 1.0f);
    return static_cast<int16_t>(std::lround(c * 32767.0f));
}

} // namespace

WavWriter::~WavWriter() {
    std::string ignored;
    if (file_.is_open()) close(ignored);
}

bool WavWriter::open(const std::filesystem::path& path, uint32_t sampleRate, std::string& error) {
    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_) {
        error = "書き込めません";
        return false;
    }
    frames_ = 0;
    const uint16_t blockAlign = kChannels * kBitsPerSample / 8;
    file_.write("RIFF", 4);
    put32(file_, 0);                              // 閉じるときに書き直す
    file_.write("WAVE", 4);
    file_.write("fmt ", 4);
    put32(file_, 16);
    put16(file_, 1);                              // PCM
    put16(file_, kChannels);
    put32(file_, sampleRate);
    put32(file_, sampleRate * blockAlign);
    put16(file_, blockAlign);
    put16(file_, kBitsPerSample);
    file_.write("data", 4);
    put32(file_, 0);                              // 閉じるときに書き直す
    return static_cast<bool>(file_);
}

void WavWriter::write(const float* left, const float* right, uint32_t frames) {
    std::vector<char> buf(static_cast<size_t>(frames) * 4);
    for (uint32_t i = 0; i < frames; ++i) {
        const int16_t l = toPcm(left[i]);
        const int16_t r = toPcm(right[i]);
        buf[i * 4 + 0] = static_cast<char>(l & 0xFF);
        buf[i * 4 + 1] = static_cast<char>((l >> 8) & 0xFF);
        buf[i * 4 + 2] = static_cast<char>(r & 0xFF);
        buf[i * 4 + 3] = static_cast<char>((r >> 8) & 0xFF);
    }
    file_.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    frames_ += frames;
}

bool WavWriter::close(std::string& error) {
    const uint64_t dataBytes = std::min<uint64_t>(frames_ * 4, kMaxDataBytes);
    file_.seekp(4);
    put32(file_, static_cast<uint32_t>(dataBytes + kHeaderSize - 8));
    file_.seekp(40);
    put32(file_, static_cast<uint32_t>(dataBytes));
    const bool ok = static_cast<bool>(file_);
    file_.close();
    if (!ok) error = "書き込みに失敗しました";
    return ok;
}

} // namespace y8960
