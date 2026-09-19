#pragma once
// 16bit ステレオの PCM を WAV ファイルに書く。

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace y8960 {

class WavWriter {
public:
    ~WavWriter();

    bool open(const std::filesystem::path& path, uint32_t sampleRate, std::string& error);
    // [-1, 1] の外は切り詰める。
    void write(const float* left, const float* right, uint32_t frames);
    // 見出しの長さを書き直して閉じる。
    bool close(std::string& error);

    uint64_t frames() const { return frames_; }

private:
    std::ofstream file_;
    uint64_t frames_ = 0;
};

} // namespace y8960
