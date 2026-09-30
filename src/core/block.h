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
    OPL3    = 8,
    OPM     = 9,
    OPNA    = 10,
    OPNB    = 11,
};

// このプレイヤーが鳴らすデバイスの数。これ以上の番号のトラックは読み飛ばす。
constexpr int kDeviceCount   = 12;
constexpr int kMaxChannels   = 25;     // OPL3 の 0-24
constexpr int kTrackCount    = 16;
constexpr int kVoiceSetSize  = 32;     // `85` が指せる索引
constexpr int kRhythmVoices  = 3;      // 索引 32-34。OPL2EX と OPL3 のリズムモードが使う
constexpr int kVoiceSlots    = kVoiceSetSize + kRhythmVoices;
constexpr int kVoiceRecSize  = 32;     // レコードの枠。いちばん大きい型に合わせる
constexpr int kFmVoiceSize   = 12;     // チャンク 01
constexpr int kSccWaveSize   = 32;     // チャンク 02
constexpr int kFm4opSize     = 24;     // チャンク 42
constexpr int kOpnVoiceSize  = 32;     // チャンク 40
constexpr int kMaxTrackBytes = 2048;   // 終端の FF を含む
constexpr int kAdpcmFiles    = 64;     // ボイスファイル番号 0-63
constexpr uint16_t kAdpcmPages = 1024; // ADPCM メモリ 256KB を 256 バイトで割った数
constexpr int kAdpcmASamples = 256;    // `D9` のサンプル番号 0-255

constexpr int kEnvelopeCount = 32;     // `B2` の番号 0-31。0 は無しでレコードを持たない

// OPLLEX と OPL2EX のチャンネル番号。ほかのデバイスは番号の意味が違うので、
// デバイスを問わずに比べるときは下の関数を使う。
constexpr uint8_t kChannelAdpcm  = 9;
constexpr uint8_t kChannelRhythm = 10;

constexpr uint8_t kOpl3Rhythm     = 24;
constexpr uint8_t kOpl3FourOpFirst = 18;
constexpr uint8_t kOpnSsgFirst    = 6;
constexpr uint8_t kOpnRhythm      = 9;    // OPNA のリズム、OPNB の ADPCM-A
constexpr uint8_t kOpnAdpcmB      = 10;

// そのデバイスが持つチャンネルか（bytecode.md「デバイス番号とチャンネル番号」）。
bool channelExists(Device device, unsigned channel);
// リズムチャンネル（楽器の集合を叩くチャンネル）の番号。持たなければ -1。
int rhythmChannel(Device device);
bool isRhythmChannel(Device device, uint8_t channel);
// リズムチャンネルが叩く楽器の数（使うビットの数）。
int rhythmInstruments(Device device);
// ボイスファイルを鳴らす ADPCM チャンネル（OPL2EX の ADPCM と OPNA・OPNB の ADPCM-B）。
bool isAdpcmChannel(Device device, uint8_t channel);
// デバイス 9-11 は自分の音色集合を持つ（チャンク 40）。
bool ownsVoiceSet(Device device);

struct TrackData {
    bool                 assigned = false;
    Device               device   = Device::SSGS;
    uint8_t              channel  = 0;
    std::vector<uint8_t> events;          // 終端の FF を含む
};

enum class RecordKind : uint8_t { None = 0, FmVoice = 1, SccWave = 2, Fm4op = 3, OpnVoice = 4 };

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

// チャンク `41`。ADPCM-A のサンプル1つの置き場所。256 バイト単位。
struct AdpcmASample {
    bool     present   = false;
    uint16_t startPage = 0;
    uint16_t pages     = 0;
};

// チャンク `04`。ソフトウェアエンベロープ1つ。値の範囲は検査しない ―― ROM は範囲を
// 超えたレートを 32、レベルを 15 として鳴らす（bytecode.md「チャンク」）。
struct EnvelopeRecord {
    bool    present = false;
    uint8_t ar = 0;
    uint8_t dr = 0;
    uint8_t sl = 0;
    uint8_t rr = 0;
};

struct SequenceBlock {
    uint8_t                                   version = 0;
    std::array<TrackData, kTrackCount>        tracks{};
    // デバイス 0-8 が共有する集合。0-31 が `85` の指す音色、32-34 がリズム音色。
    std::array<VoiceRecord, kVoiceSlots>      voices{};
    // デバイス 9-11（OPM・OPNA・OPNB）がそれぞれ持つ集合。
    std::array<std::array<VoiceRecord, kVoiceSetSize>, 3> deviceVoices{};
    std::array<AdpcmVoiceFile, kAdpcmFiles>   adpcm{};
    std::array<AdpcmASample, kAdpcmASamples>  adpcmA{};   // OPNB のもの
    std::array<EnvelopeRecord, kEnvelopeCount> envelopes{};
    // 捨てたチャンク 03 と、読み飛ばしたトラックの理由。ブロックは拒まれない。
    std::vector<std::string>                  warnings;
    // デバイスごとのリズムモード。ブロックは持たないので、割り当てから推定する。
    std::array<bool, kDeviceCount>            rhythmMode{};

    // `85` が名指す集合の索引 index のレコード。無ければ nullptr。
    const VoiceRecord* seqVoice(Device device, uint8_t index) const;
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
