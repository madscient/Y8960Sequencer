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
        error = "Y8SQ ヘッダがありません";
        return false;
    }
    out.version = data[4];
    if (out.version > kReaderVersion) {
        error = "ブロックの版 " + std::to_string(out.version) + " はこのプレイヤーより新しいため読めません";
        return false;
    }
    const size_t blockSize = readLe16(data + 5);
    if (blockSize < kHeaderSize) {
        error = "ヘッダのブロック長 " + std::to_string(blockSize) + " がヘッダより短い";
        return false;
    }
    if (blockSize > size) {
        error = "ヘッダのブロック長 " + std::to_string(blockSize) + " がファイルの残り " +
                std::to_string(size) + " バイトを超えています";
        return false;
    }

    size_t pos = kHeaderSize;
    while (pos < blockSize) {
        if (blockSize - pos < 3) {
            error = "オフセット " + std::to_string(pos) + " のチャンク見出しが途中で切れています";
            return false;
        }
        const uint8_t type = data[pos];
        const size_t  len  = readLe16(data + pos + 1);
        const size_t  body = pos + 3;
        if (len > blockSize - body) {
            error = "オフセット " + std::to_string(pos) + " のチャンクがブロックの終わりを超えています";
            return false;
        }
        const uint8_t* p = data + body;

        if (type == kChunkTrack) {
            if (len < kTrackHeadSize + 1 || len > kTrackHeadSize + kMaxTrackBytes) {
                error = "トラックチャンクの長さ " + std::to_string(len) + " が範囲外です";
                return false;
            }
            const uint8_t trackNo = p[0];
            const uint8_t dev     = p[1];
            const uint8_t ch      = p[2];
            if (trackNo >= kTrackCount) {
                error = "トラック番号 " + std::to_string(trackNo) + " が範囲外です";
                return false;
            }
            if (dev >= kDeviceCount) {
                error = "トラック " + std::to_string(trackNo) + " のデバイス番号 " +
                        std::to_string(dev) + " が範囲外です";
                return false;
            }
            const Device device = static_cast<Device>(dev);
            if (!channelInRange(device, ch)) {
                error = "トラック " + std::to_string(trackNo) + " のチャンネル " +
                        std::to_string(ch) + " はデバイス " + std::to_string(dev) + " にありません";
                return false;
            }
            TrackData& t = out.tracks[trackNo];
            t.assigned = true;
            t.device   = device;
            t.channel  = ch;
            t.events.assign(p + kTrackHeadSize, p + len);
        } else if (type == kChunkFmVoice || type == kChunkSccWave) {
            if (len != 1 + kVoiceRecSize) {
                error = "音色チャンクの長さ " + std::to_string(len) + " が 33 ではありません";
                return false;
            }
            const uint8_t index = p[0];
            if (index >= kVoiceSlots) {
                error = "音色の索引 " + std::to_string(index) + " が範囲外です";
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
                why = "長さが " + std::to_string(len) + " で 7 ではありません";
            } else if (p[0] >= kAdpcmFiles) {
                why = "ボイスファイル番号 " + std::to_string(p[0]) + " が範囲外です";
            } else {
                AdpcmVoiceFile a;
                a.startPage    = readLe16(p + 1);
                a.pages        = readLe16(p + 3);
                a.sampleRateHz = readLe16(p + 5);
                if (a.pages == 0 || static_cast<uint32_t>(a.startPage) + a.pages > kAdpcmPages) {
                    why = "ボイスファイル " + std::to_string(p[0]) + " の範囲が ADPCM メモリに収まりません";
                } else if (a.sampleRateHz < kAdpcmRateMin || a.sampleRateHz > kAdpcmRateMax) {
                    why = "ボイスファイル " + std::to_string(p[0]) + " のサンプリング周波数 " +
                          std::to_string(a.sampleRateHz) + "Hz が範囲外です";
                } else {
                    a.present = true;
                    out.adpcm[p[0]] = a;
                }
            }
            if (!why.empty()) out.warnings.push_back("ADPCM のボイスファイルの控え: " + why);
        } else if (type < kChunkSkippableFirst) {
            error = "知らない種別のチャンク " + hex2(type) + " があるため読めません";
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
            error = "デバイス " + std::to_string(d) + " のチャンネル " +
                    std::to_string(t.channel) + " を複数のトラックが持っています";
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
            error = "デバイス " + std::to_string(d) +
                    " がチャンネル 6-8 とリズムチャンネルの両方を持っています";
            return false;
        }
        out.rhythmMode[d] = hasRhythm[d];
    }
    return true;
}

bool loadBlock(const std::vector<uint8_t>& file, SequenceBlock& out, std::string& error) {
    const BlockLocation loc = locateBlock(file.data(), file.size());
    if (!loc.found) {
        error = "Y8SQ のシーケンスデータではありません";
        return false;
    }
    return parseBlock(file.data() + loc.offset, file.size() - loc.offset, out, error);
}

} // namespace y8960
