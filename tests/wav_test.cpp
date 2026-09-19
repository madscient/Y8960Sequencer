#include "check.h"
#include "wavwriter.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace y8960;

namespace {

uint32_t le32(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24));
}

uint16_t le16(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}

int16_t sample(const std::vector<uint8_t>& b, size_t frame, int channel) {
    return static_cast<int16_t>(le16(b, 44 + frame * 4 + static_cast<size_t>(channel) * 2));
}

} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path() / "y8960_wav_test.wav";
    {
        WavWriter w;
        std::string err;
        CHECK(w.open(path, 48000, err));
        const float left[3]  = {0.0f, 1.0f, 2.0f};     // 2.0 は切り詰められる
        const float right[3] = {-1.0f, 0.5f, -3.0f};
        w.write(left, right, 3);
        CHECK(w.frames() == 3);
        CHECK(w.close(err));
    }

    std::ifstream f(path, std::ios::binary);
    const std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    std::filesystem::remove(path);

    CHECK(b.size() == 44 + 3 * 4);
    CHECK(std::string(b.begin(), b.begin() + 4) == "RIFF");
    CHECK(le32(b, 4) == b.size() - 8);
    CHECK(std::string(b.begin() + 8, b.begin() + 16) == "WAVEfmt ");
    CHECK(le16(b, 20) == 1);          // PCM
    CHECK(le16(b, 22) == 2);          // ステレオ
    CHECK(le32(b, 24) == 48000);
    CHECK(le32(b, 28) == 48000 * 4);
    CHECK(le16(b, 32) == 4);
    CHECK(le16(b, 34) == 16);
    CHECK(std::string(b.begin() + 36, b.begin() + 40) == "data");
    CHECK(le32(b, 40) == 3 * 4);

    CHECK(sample(b, 0, 0) == 0);
    CHECK(sample(b, 0, 1) == -32767);
    CHECK(sample(b, 1, 0) == 32767);
    CHECK(sample(b, 1, 1) == 16384);
    CHECK(sample(b, 2, 0) == 32767);  // 切り詰め
    CHECK(sample(b, 2, 1) == -32767);

    return check::finish("wav_test");
}
