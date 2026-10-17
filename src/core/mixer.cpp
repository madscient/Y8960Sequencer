#include "mixer.h"

namespace y8960 {

float mixerGain(uint8_t level) {
    if (level >= kMixerLevelMax) return 1.0f;
    return static_cast<float>(level) / kMixerLevelMax;
}

bool parseMixer(const std::string& text, MixerLevels& out, std::string& error) {
    constexpr size_t kFields = 5;
    std::array<uint8_t, kFields> values{};
    size_t field = 0;
    size_t pos = 0;
    for (;;) {
        const size_t comma = text.find(',', pos);
        const std::string v = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        bool digits = !v.empty() && v.size() <= 3;
        for (char c : v) digits = digits && c >= '0' && c <= '9';
        if (!digits || std::stoi(v) > kMixerLevelMax) {
            error = "each level must be 0 to 128: " + text;
            return false;
        }
        if (field == kFields) {
            error = "needs 5 levels (master, SSGS, OPLLEX, OPL2EX, SCC): " + text;
            return false;
        }
        values[field++] = static_cast<uint8_t>(std::stoi(v));
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    if (field != kFields) {
        error = "needs 5 levels (master, SSGS, OPLLEX, OPL2EX, SCC): " + text;
        return false;
    }
    out.master = values[0];
    for (size_t i = 0; i < out.blocks.size(); ++i) out.blocks[i] = values[i + 1];
    return true;
}

} // namespace y8960
