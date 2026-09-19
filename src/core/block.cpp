#include "block.h"

#include <cstring>

namespace y8960 {

namespace {

constexpr uint8_t kSignature[4] = {'Y', '8', 'S', 'Q'};
constexpr size_t  kHeaderSize   = 7;
constexpr uint8_t kReaderVersion = 1;

constexpr size_t kBsaveHeaderSize = 7;
constexpr uint8_t kBsaveId        = 0xFE;

constexpr uint8_t kChunkTrack   = 0x00;
constexpr uint8_t kChunkFmVoice = 0x01;
constexpr uint8_t kChunkSccWave = 0x02;
constexpr uint8_t kChunkAdpcm   = 0x03;
constexpr uint8_t kChunkSkippableFirst = 0x80;

constexpr size_t kAdpcmChunkSize = 7;
constexpr uint16_t kAdpcmRateMin = 1800;
constexpr uint16_t kAdpcmRateMax = 16000;

constexpr size_t kTrackHeadSize = 3;   // トラック番号、デバイス、チャンネル

bool hasSignature(const uint8_t* p, size_t size) {
    return size >= sizeof kSignature && std::memcmp(p, kSignature, sizeof kSignature) == 0;
}

uint16_t readLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

bool isFmDevice(Device d) {
    return d == Device::OPLLEX1 || d == Device::OPLLEX2 ||
           d == Device::OPL2EX1 || d == Device::OPL2EX2;
}

// リズムモードを決める前に分かる範囲の検査。6-8 と 10 の両立は後で見る。
bool channelInRange(Device d, uint8_t ch) {
    switch (d) {
    case Device::SSGS:    return ch < 6;
    case Device::OPLLEX1:
    case Device::OPLLEX2: return ch <= 8 || ch == kChannelRhythm;
    case Device::OPL2EX1:
    case Device::OPL2EX2: return ch <= kChannelRhythm;
    case Device::DCSG1:
    case Device::DCSG2:   return ch < 4;
    case Device::SCC:     return ch < 5;
    }
    return false;
}

std::string hex2(unsigned v) {
    static const char digits[] = "0123456789ABCDEF";
    std::string s = "00h";
    s[0] = digits[(v >> 4) & 0xF];
    s[1] = digits[v & 0xF];
    return s;
}

} // namespace

BlockLocation locateBlock(const uint8_t* data, size_t size) {
    BlockLocation loc;
    if (hasSignature(data, size)) {
        loc.found = true;
        loc.offset = 0;
    } else if (size > kBsaveHeaderSize && data[0] == kBsaveId &&
               hasSignature(data + kBsaveHeaderSize, size - kBsaveHeaderSize)) {
        loc.found = true;
        loc.offset = kBsaveHeaderSize;
    }
    return loc;
}

bool parseBlock(const uint8_t* data, size_t size, SequenceBlock& out, std::string& error) {
    out = SequenceBlock{};

    if (size < kHeaderSize || !hasSignature(data, size)) {
        error = "no Y8SQ header";
        return false;
    }
    out.version = data[4];
    if (out.version > kReaderVersion) {
        error = "block version " + std::to_string(out.version) + " is newer than this player";
        return false;
    }
    const size_t blockSize = readLe16(data + 5);
    if (blockSize < kHeaderSize) {
        error = "block length " + std::to_string(blockSize) + " is shorter than the header";
        return false;
    }
    if (blockSize > size) {
        error = "block length " + std::to_string(blockSize) + " exceeds the " +
                std::to_string(size) + " bytes left in the file";
        return false;
    }

    size_t pos = kHeaderSize;
    while (pos < blockSize) {
        if (blockSize - pos < 3) {
            error = "chunk header at offset " + std::to_string(pos) + " is cut short";
            return false;
        }
        const uint8_t type = data[pos];
        const size_t  len  = readLe16(data + pos + 1);
        const size_t  body = pos + 3;
        if (len > blockSize - body) {
            error = "chunk at offset " + std::to_string(pos) + " runs past the end of the block";
            return false;
        }
        const uint8_t* p = data + body;

        if (type == kChunkTrack) {
            if (len < kTrackHeadSize + 1 || len > kTrackHeadSize + kMaxTrackBytes) {
                error = "track chunk length " + std::to_string(len) + " is out of range";
                return false;
            }
            const uint8_t trackNo = p[0];
            const uint8_t dev     = p[1];
            const uint8_t ch      = p[2];
            if (trackNo >= kTrackCount) {
                error = "track number " + std::to_string(trackNo) + " is out of range";
                return false;
            }
            if (dev >= kDeviceCount) {
                error = "track " + std::to_string(trackNo) + ": device number " +
                        std::to_string(dev) + " is out of range";
                return false;
            }
            const Device device = static_cast<Device>(dev);
            if (!channelInRange(device, ch)) {
                error = "track " + std::to_string(trackNo) + ": device " + std::to_string(dev) +
                        " has no channel " + std::to_string(ch);
                return false;
            }
            TrackData& t = out.tracks[trackNo];
            t.assigned = true;
            t.device   = device;
            t.channel  = ch;
            t.events.assign(p + kTrackHeadSize, p + len);
        } else if (type == kChunkFmVoice || type == kChunkSccWave) {
            if (len != 1 + kVoiceRecSize) {
                error = "voice chunk length " + std::to_string(len) + " is not 33";
                return false;
            }
            const uint8_t index = p[0];
            if (index >= kVoiceSlots) {
                error = "voice index " + std::to_string(index) + " is out of range";
                return false;
            }
            VoiceRecord& v = out.voices[index];
            v.kind = (type == kChunkFmVoice) ? RecordKind::FmVoice : RecordKind::SccWave;
            std::memcpy(v.data.data(), p + 1, kVoiceRecSize);
        } else if (type == kChunkAdpcm) {
            // 壊れていてもブロックは拒まない ―― 書き手の ROM も読み手の ROM も中身を
            // 検査せず、このチャンクを使わなくても曲は鳴る（bytecode.md「チャンク」）。
            // 値の範囲は pcmfile.md の設定1件と同じ。
            std::string why;
            if (len != kAdpcmChunkSize) {
                why = "length " + std::to_string(len) + " is not 7";
            } else if (p[0] >= kAdpcmFiles) {
                why = "voice file number " + std::to_string(p[0]) + " is out of range";
            } else {
                AdpcmVoiceFile a;
                a.startPage    = readLe16(p + 1);
                a.pages        = readLe16(p + 3);
                a.sampleRateHz = readLe16(p + 5);
                if (a.pages == 0 || static_cast<uint32_t>(a.startPage) + a.pages > kAdpcmPages) {
                    why = "voice file " + std::to_string(p[0]) + " does not fit in the ADPCM memory";
                } else if (a.sampleRateHz < kAdpcmRateMin || a.sampleRateHz > kAdpcmRateMax) {
                    why = "voice file " + std::to_string(p[0]) + ": sampling rate " +
                          std::to_string(a.sampleRateHz) + "Hz is out of range";
                } else {
                    a.present = true;
                    out.adpcm[p[0]] = a;
                }
            }
            if (!why.empty()) out.warnings.push_back("ADPCM voice file entry: " + why);
        } else if (type < kChunkSkippableFirst) {
            error = "unknown chunk type " + hex2(type);
            return false;
        }
        pos = body + len;
    }

    // チャンネルの重複と、リズムモードの推定。
    std::array<uint16_t, kDeviceCount> used{};
    std::array<bool, kDeviceCount> hasMelody68{};
    std::array<bool, kDeviceCount> hasRhythm{};
    for (int i = 0; i < kTrackCount; ++i) {
        const TrackData& t = out.tracks[i];
        if (!t.assigned) continue;
        const int d = static_cast<int>(t.device);
        const uint16_t bit = static_cast<uint16_t>(1u << t.channel);
        if (used[d] & bit) {
            error = "device " + std::to_string(d) + ": channel " +
                    std::to_string(t.channel) + " is held by more than one track";
            return false;
        }
        used[d] |= bit;
        if (isFmDevice(t.device)) {
            if (t.channel >= 6 && t.channel <= 8) hasMelody68[d] = true;
            if (t.channel == kChannelRhythm)      hasRhythm[d]   = true;
        }
    }
    for (int d = 0; d < kDeviceCount; ++d) {
        if (hasMelody68[d] && hasRhythm[d]) {
            error = "device " + std::to_string(d) +
                    " holds both channels 6-8 and the rhythm channel";
            return false;
        }
        out.rhythmMode[d] = hasRhythm[d];
    }
    return true;
}

bool loadBlock(const std::vector<uint8_t>& file, SequenceBlock& out, std::string& error) {
    const BlockLocation loc = locateBlock(file.data(), file.size());
    if (!loc.found) {
        error = "not Y8SQ sequence data";
        return false;
    }
    return parseBlock(file.data() + loc.offset, file.size() - loc.offset, out, error);
}

} // namespace y8960
