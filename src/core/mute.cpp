#include "mute.h"

#include <cctype>

namespace y8960 {

namespace {

const char* const kSymbols[kDeviceCount] = {
    "SSGS", "OPLLEX1", "OPLLEX2", "OPL2EX1", "OPL2EX2", "DCSG1", "DCSG2", "SCC",
};

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

// そのチップが持つチャンネルか（bytecode.md「デバイス番号とチャンネル番号」）。
bool channelExists(Device device, unsigned channel) {
    switch (device) {
    case Device::SSGS:    return channel < 6;
    case Device::OPLLEX1:
    case Device::OPLLEX2: return channel <= 8 || channel == kChannelRhythm;
    case Device::OPL2EX1:
    case Device::OPL2EX2: return channel <= kChannelRhythm;
    case Device::DCSG1:
    case Device::DCSG2:   return channel < 4;
    case Device::SCC:     return channel < 5;
    }
    return false;
}

} // namespace

const char* deviceSymbol(Device device) {
    return kSymbols[static_cast<size_t>(device)];
}

bool parseMuteSpec(const std::string& text, MuteSpec& out, std::string& error) {
    out = MuteSpec{};
    std::string s = upper(text);
    if (!s.empty() && s[0] == '!') {
        out.except = true;
        s.erase(0, 1);
    }
    if (s.empty()) {
        error = "ミュートの指定が空です";
        return false;
    }

    // A-P の1文字はトラック。T は範囲の外なので、T<番号> と取り違えない。
    if (s.size() == 1 && s[0] >= 'A' && s[0] <= 'P') {
        out.kind  = MuteSpec::Kind::Track;
        out.track = static_cast<uint8_t>(s[0] - 'A');
        return true;
    }
    if (s[0] == 'T' && allDigits(s.substr(1))) {
        const unsigned n = static_cast<unsigned>(std::stoul(s.substr(1)));
        if (n >= static_cast<unsigned>(kTrackCount)) {
            error = "トラック番号は 0-15 です: " + text;
            return false;
        }
        out.kind  = MuteSpec::Kind::Track;
        out.track = static_cast<uint8_t>(n);
        return true;
    }

    const size_t comma = s.find(',');
    const std::string chip = s.substr(0, comma);
    int found = -1;
    for (int d = 0; d < kDeviceCount; ++d) {
        if (chip == kSymbols[d]) found = d;
    }
    if (found < 0) {
        error = "知らない指定です: " + text;
        return false;
    }
    out.device = static_cast<Device>(found);
    if (comma == std::string::npos) {
        out.kind = MuteSpec::Kind::Chip;
        return true;
    }

    const std::string ch = s.substr(comma + 1);
    if (!allDigits(ch) || ch.size() > 3 || !channelExists(out.device, static_cast<unsigned>(std::stoul(ch)))) {
        error = std::string(kSymbols[found]) + " にチャンネル " + ch + " はありません";
        return false;
    }
    out.kind    = MuteSpec::Kind::Channel;
    out.channel = static_cast<uint8_t>(std::stoul(ch));
    return true;
}

uint16_t resolveMutes(const std::vector<MuteSpec>& specs, const SequenceBlock& block) {
    uint16_t assigned = 0;
    for (int i = 0; i < kTrackCount; ++i) {
        if (block.tracks[static_cast<size_t>(i)].assigned) assigned = static_cast<uint16_t>(assigned | (1u << i));
    }

    auto tracksOf = [&](const MuteSpec& spec) {
        uint16_t mask = 0;
        for (int i = 0; i < kTrackCount; ++i) {
            const TrackData& t = block.tracks[static_cast<size_t>(i)];
            bool hit = false;
            switch (spec.kind) {
            case MuteSpec::Kind::Track:   hit = (i == spec.track); break;
            case MuteSpec::Kind::Chip:    hit = t.assigned && t.device == spec.device; break;
            case MuteSpec::Kind::Channel: hit = t.assigned && t.device == spec.device &&
                                                t.channel == spec.channel; break;
            }
            if (hit) mask = static_cast<uint16_t>(mask | (1u << i));
        }
        return mask;
    };

    bool     anyExcept = false;
    uint16_t kept      = 0;
    uint16_t muted     = 0;
    for (const MuteSpec& spec : specs) {
        if (spec.except) {
            anyExcept = true;
            kept = static_cast<uint16_t>(kept | tracksOf(spec));
        } else {
            muted = static_cast<uint16_t>(muted | tracksOf(spec));
        }
    }
    if (anyExcept) muted = static_cast<uint16_t>(muted | (assigned & ~kept));
    return static_cast<uint16_t>(muted & assigned);
}

} // namespace y8960
