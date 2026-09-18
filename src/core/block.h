#pragma once
// Y8SQ ブロック（Y8960BasicExtension の doc/bytecode.md「ブロック」）の読み込み。

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace y8960 {

enum class Device : uint8_t {
    SSGS    = 0,
    OPLLEX1 = 1,
    OPLLEX2 = 2,
    OPL2EX1 = 3,
    OPL2EX2 = 4,
    DCSG1   = 5,
    DCSG2   = 6,
    SCC     = 7,
};

constexpr int kDeviceCount   = 8;
constexpr int kTrackCount    = 16;
constexpr int kVoiceSetSize  = 32;     // `85` が指せる索引
constexpr int kRhythmVoices  = 3;      // 索引 32-34。OPL2EX のリズムモードが使う
constexpr int kVoiceSlots    = kVoiceSetSize + kRhythmVoices;
constexpr int kVoiceRecSize  = 32;
constexpr int kMaxTrackBytes = 2048;   // 終端の FF を含む
constexpr int kAdpcmFiles    = 64;     // ボイスファイル番号 0-63
constexpr uint16_t kAdpcmPages = 1024; // ADPCM メモリ 256KB を 256 バイトで割った数

constexpr uint8_t kChannelAdpcm  = 9;
constexpr uint8_t kChannelRhythm = 10;

struct TrackData {
    bool                 assigned = false;
    Device               device   = Device::SSGS;
    uint8_t              channel  = 0;
    std::vector<uint8_t> events;          // 終端の FF を含む
};

enum class RecordKind : uint8_t { None = 0, FmVoice = 1, SccWave = 2 };

struct VoiceRecord {
    RecordKind                           kind = RecordKind::None;
    std::array<uint8_t, kVoiceRecSize>   data{};
};

// チャンク `03`。曲が使うボイスファイルが ADPCM メモリのどこにあるかの控え。
// 中身は pcmfile.md の設定1件と同じ7バイト。
struct AdpcmVoiceFile {
    bool     present       = false;
    uint16_t startPage     = 0;
    uint16_t pages         = 0;
    uint16_t sampleRateHz  = 0;
};

struct SequenceBlock {
    uint8_t                                   version = 0;
    std::array<TrackData, kTrackCount>        tracks{};
    // 0-31 が `85` の指す音色集合、32-34 が OPL2EX のリズム音色。
    std::array<VoiceRecord, kVoiceSlots>      voices{};
    std::array<AdpcmVoiceFile, kAdpcmFiles>   adpcm{};
    // 捨てたチャンク 03 の理由。ブロックは拒まれない（bytecode.md「チャンク」）。
    std::vector<std::string>                  warnings;
    // デバイスごとのリズムモード。ブロックは持たないので、割り当てから推定する。
    std::array<bool, kDeviceCount>            rhythmMode{};
};

struct BlockLocation {
    bool   found  = false;
    size_t offset = 0;   // ヘッダの位置。BSAVE の見出しがあれば 7
};

// ファイルの中身から Y8SQ ヘッダの位置を探す。名前は見ない。
BlockLocation locateBlock(const uint8_t* data, size_t size);

// data はヘッダの先頭。成功すれば true。失敗の理由は error に入る。
bool parseBlock(const uint8_t* data, size_t size, SequenceBlock& out, std::string& error);

// locateBlock と parseBlock をまとめたもの。
bool loadBlock(const std::vector<uint8_t>& file, SequenceBlock& out, std::string& error);

} // namespace y8960
