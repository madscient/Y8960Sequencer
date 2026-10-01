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
constexpr uint8_t kChunkEnvelope = 0x04;
constexpr uint8_t kChunkDeviceFirst = 0x40;   // 40-7F は1つのデバイスに属する
constexpr uint8_t kChunkOpnVoice = 0x40;
constexpr uint8_t kChunkAdpcmA   = 0x41;
constexpr uint8_t kChunkFm4op    = 0x42;
constexpr uint8_t kChunkSkippableFirst = 0x80;
constexpr uint8_t kChunkMeta     = 0x80;

constexpr uint8_t kMetaPitch  = 0x01;
constexpr uint8_t kMetaVolume = 0x02;
constexpr uint8_t kMetaTitle  = 0x03;
constexpr uint8_t kMetaAuthor = 0x04;
constexpr uint16_t kPitchMin  = 4300;
constexpr uint16_t kPitchMax  = 4500;

constexpr size_t kAdpcmChunkSize = 7;
constexpr size_t kEnvelopeChunkSize = 5;
constexpr size_t kAdpcmAChunkSize = 6;
constexpr uint16_t kAdpcmRateMin = 1800;
constexpr uint16_t kAdpcmRateMax = 16000;

constexpr size_t kTrackHeadSize = 3;   // トラック番号、デバイス、チャンネル

bool hasSignature(const uint8_t* p, size_t size) {
    return size >= sizeof kSignature && std::memcmp(p, kSignature, sizeof kSignature) == 0;
}

uint16_t readLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

// リズムチャンネルと排他になる 6-8 を持つデバイス。
bool isOplFamily(Device d) {
    return d == Device::OPLLEX1 || d == Device::OPLLEX2 ||
           d == Device::OPL2EX1 || d == Device::OPL2EX2 || d == Device::OPL3;
}

std::string hex2(unsigned v) {
    static const char digits[] = "0123456789ABCDEF";
    std::string s = "00h";
    s[0] = digits[(v >> 4) & 0xF];
    s[1] = digits[v & 0xF];
    return s;
}

// 4OP のチャンネル 18-23 が組にする 2OP の前側。後ろ側はこれに 3 を足したもの。
uint8_t fourOpFront(uint8_t ch) {
    const uint8_t i = static_cast<uint8_t>(ch - kOpl3FourOpFirst);
    return static_cast<uint8_t>((i < 3) ? i : 9 + (i - 3));
}

// 文字列は 20h-7Eh の ASCII だけで書く決まり。ほかのバイトは '?' にして表示を崩さない。
std::string asciiText(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; ++i) s.push_back((p[i] >= 0x20 && p[i] <= 0x7E) ? static_cast<char>(p[i]) : '?');
    return s;
}

// メタ情報は使わなくても演奏が成り立つので、壊れていてもブロックは拒まない。
// 読めたところまでを使い、残りは警告にする。
void readMeta(const uint8_t* p, size_t len, SequenceBlock& out) {
    size_t pos = 0;
    while (pos < len) {
        if (len - pos < 2 || p[pos + 1] > len - pos - 2) {
            out.warnings.push_back("meta information is cut short");
            return;
        }
        const uint8_t item = p[pos];
        const uint8_t n    = p[pos + 1];
        const uint8_t* v   = p + pos + 2;
        pos += 2 + n;
        if (item == kMetaPitch && n == 2) {
            const uint16_t pitch = readLe16(v);
            if (pitch >= kPitchMin && pitch <= kPitchMax) out.masterPitch = pitch;
            else out.warnings.push_back("master pitch " + std::to_string(pitch) + " is out of range");
        } else if (item == kMetaVolume && n == 1) {
            if (v[0] <= 127) out.masterVolume = v[0];
            else out.warnings.push_back("master volume " + std::to_string(v[0]) + " is out of range");
        } else if (item == kMetaTitle) {
            out.title = asciiText(v, n);
        } else if (item == kMetaAuthor) {
            out.author = asciiText(v, n);
        } else if (item <= kMetaAuthor) {
            out.warnings.push_back("meta item " + std::to_string(item) + " has a wrong length");
        }
        // 知らない項目番号は飛ばす。
    }
}

} // namespace

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
    case Device::OPL3:    return channel <= kOpl3Rhythm;
    case Device::OPM:     return channel < 8;
    case Device::OPNA:
    case Device::OPNB:    return channel <= kOpnAdpcmB;
    }
    return false;
}

int rhythmChannel(Device device) {
    switch (device) {
    case Device::OPLLEX1:
    case Device::OPLLEX2:
    case Device::OPL2EX1:
    case Device::OPL2EX2: return kChannelRhythm;
    case Device::OPL3:    return kOpl3Rhythm;
    case Device::OPNA:
    case Device::OPNB:    return kOpnRhythm;
    default:              return -1;
    }
}

bool isRhythmChannel(Device device, uint8_t channel) {
    return rhythmChannel(device) == channel;
}

int rhythmInstruments(Device device) {
    if (device == Device::OPNA || device == Device::OPNB) return 6;
    return rhythmChannel(device) < 0 ? 0 : 5;
}

bool isAdpcmChannel(Device device, uint8_t channel) {
    switch (device) {
    case Device::OPL2EX1:
    case Device::OPL2EX2: return channel == kChannelAdpcm;
    case Device::OPNA:
    case Device::OPNB:    return channel == kOpnAdpcmB;
    default:              return false;
    }
}

bool ownsVoiceSet(Device device) {
    return device == Device::OPM || device == Device::OPNA || device == Device::OPNB;
}

const VoiceRecord* SequenceBlock::seqVoice(Device device, uint8_t index) const {
    const VoiceRecord* rec = nullptr;
    if (ownsVoiceSet(device)) {
        if (index < kVoiceSetSize) {
            rec = &deviceVoices[static_cast<size_t>(device) - static_cast<size_t>(Device::OPM)][index];
        }
    } else if (index < kVoiceSlots) {
        rec = &voices[index];
    }
    return (rec && rec->kind != RecordKind::None) ? rec : nullptr;
}

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
        pos = body + len;

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
            // 鳴らさないデバイスのトラックは割り当てず、残りを鳴らす（bytecode.md
            // 「版と、知らないものに出会ったとき」）。形式がまだ名前を付けていない
            // 番号も同じ扱い。
            if (dev >= kDeviceCount) {
                out.warnings.push_back("track " + std::to_string(trackNo) + ": device " +
                                       std::to_string(dev) + " is not played");
                continue;
            }
            const Device device = static_cast<Device>(dev);
            if (!channelExists(device, ch)) {
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
            const size_t rec = (type == kChunkFmVoice) ? kFmVoiceSize : kSccWaveSize;
            if (len != 1 + rec) {
                error = "voice chunk " + hex2(type) + " length " + std::to_string(len) +
                        " is not " + std::to_string(1 + rec);
                return false;
            }
            const uint8_t index = p[0];
            if (index >= kVoiceSlots) {
                error = "voice index " + std::to_string(index) + " is out of range";
                return false;
            }
            VoiceRecord& v = out.voices[index];
            v = VoiceRecord{};
            v.kind = (type == kChunkFmVoice) ? RecordKind::FmVoice : RecordKind::SccWave;
            std::memcpy(v.data.data(), p + 1, rec);
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
        } else if (type == kChunkEnvelope) {
            // チャンク 03 と違い、壊れていればブロックごと拒む（bytecode.md「チャンク」）。
            if (len != kEnvelopeChunkSize) {
                error = "envelope chunk length " + std::to_string(len) + " is not 5";
                return false;
            }
            if (p[0] == 0 || p[0] >= kEnvelopeCount) {
                error = "envelope number " + std::to_string(p[0]) + " is out of range";
                return false;
            }
            EnvelopeRecord& e = out.envelopes[p[0]];
            e.present = true;
            e.ar = p[1];
            e.dr = p[2];
            e.sl = p[3];
            e.rr = p[4];
        } else if (type >= kChunkDeviceFirst && type < kChunkSkippableFirst) {
            // 中身の先頭がデバイス番号。鳴らさないデバイスのものは読み飛ばす。
            if (len == 0) {
                error = "chunk " + hex2(type) + " has no device number";
                return false;
            }
            const uint8_t dev = p[0];
            if (dev >= kDeviceCount) continue;
            const Device device = static_cast<Device>(dev);
            if (type == kChunkOpnVoice) {
                if (!ownsVoiceSet(device)) {
                    error = "chunk 40h names device " + std::to_string(dev);
                    return false;
                }
                if (len != 2 + kOpnVoiceSize) {
                    error = "voice chunk 40h length " + std::to_string(len) + " is not 34";
                    return false;
                }
                if (p[1] >= kVoiceSetSize) {
                    error = "voice index " + std::to_string(p[1]) + " is out of range";
                    return false;
                }
                VoiceRecord& v = out.deviceVoices[dev - static_cast<size_t>(Device::OPM)][p[1]];
                v.kind = RecordKind::OpnVoice;
                std::memcpy(v.data.data(), p + 2, kOpnVoiceSize);
            } else if (type == kChunkAdpcmA) {
                // ADPCM-A を持つのは OPNB だけ。ほかのデバイス 8-10 のものは鳴るものを
                // 変えないので受け取って捨てる。デバイス 0-7 のものは ROM の読み手と
                // 同じく拒む。
                if (dev < static_cast<uint8_t>(Device::OPL3)) {
                    error = "chunk 41h names device " + std::to_string(dev);
                    return false;
                }
                if (len != kAdpcmAChunkSize) {
                    error = "ADPCM-A chunk length " + std::to_string(len) + " is not 6";
                    return false;
                }
                if (device == Device::OPNB) {
                    AdpcmASample& a = out.adpcmA[p[1]];
                    a.present   = true;
                    a.startPage = readLe16(p + 2);
                    a.pages     = readLe16(p + 4);
                }
            } else if (type == kChunkFm4op) {
                if (device != Device::OPL3) {
                    error = "chunk 42h names device " + std::to_string(dev);
                    return false;
                }
                if (len != 2 + kFm4opSize) {
                    error = "voice chunk 42h length " + std::to_string(len) + " is not 26";
                    return false;
                }
                if (p[1] >= kVoiceSetSize) {
                    error = "voice index " + std::to_string(p[1]) + " is out of range";
                    return false;
                }
                VoiceRecord& v = out.voices[p[1]];
                v = VoiceRecord{};
                v.kind = RecordKind::Fm4op;
                std::memcpy(v.data.data(), p + 2, kFm4opSize);
            } else {
                error = "unknown chunk type " + hex2(type) + " for device " + std::to_string(dev);
                return false;
            }
        } else if (type == kChunkMeta) {
            readMeta(p, len, out);
        } else if (type < kChunkSkippableFirst) {
            error = "unknown chunk type " + hex2(type);
            return false;
        }
    }

    // チャンネルの重複と排他、リズムモードの推定。4OP で鳴らすかは音色が決めるので、
    // ここでは組が1つのトラックのものであることだけを見る。
    std::array<uint32_t, kDeviceCount> used{};
    for (int i = 0; i < kTrackCount; ++i) {
        const TrackData& t = out.tracks[i];
        if (!t.assigned) continue;
        const int d = static_cast<int>(t.device);
        const uint32_t bit = 1u << t.channel;
        if (used[d] & bit) {
            error = "device " + std::to_string(d) + ": channel " +
                    std::to_string(t.channel) + " is held by more than one track";
            return false;
        }
        used[d] |= bit;
    }
    constexpr uint32_t kCh68 = 0x1C0;
    for (int d = 0; d < kDeviceCount; ++d) {
        const Device device = static_cast<Device>(d);
        if (!isOplFamily(device)) continue;
        const int rc = rhythmChannel(device);
        const bool rhythm = (used[d] >> rc) & 1;
        if (rhythm && (used[d] & kCh68)) {
            error = "device " + std::to_string(d) + " holds both channels 6-8 and the rhythm channel";
            return false;
        }
        out.rhythmMode[d] = rhythm;
    }
    const uint32_t opl3 = used[static_cast<size_t>(Device::OPL3)];
    for (uint8_t ch = kOpl3FourOpFirst; ch < kOpl3Rhythm; ++ch) {
        if (!((opl3 >> ch) & 1)) continue;
        const uint8_t front = fourOpFront(ch);
        if (opl3 & ((1u << front) | (1u << (front + 3)))) {
            error = "device 8: channel " + std::to_string(ch) + " and a channel it pairs are both held";
            return false;
        }
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
