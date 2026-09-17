#include "pcmfile.h"

#include <cstring>

namespace y8960 {

namespace {

constexpr uint8_t kSignature[4] = {'Y', '8', 'P', 'C'};
constexpr size_t  kHeaderSize   = 9;
constexpr uint8_t kVersion      = 1;

constexpr uint8_t kHasSettings = 0x01;
constexpr uint8_t kHasDump     = 0x02;

constexpr size_t   kSettingSize = 7;
constexpr uint8_t  kMaxSettings = 32;
constexpr uint16_t kAdpcmRateMin = 1800;
constexpr uint16_t kAdpcmRateMax = 16000;

uint16_t readLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

} // namespace

bool parsePcmFile(const std::vector<uint8_t>& file, PcmFile& out, std::string& error) {
    out = PcmFile{};

    if (file.size() < kHeaderSize || std::memcmp(file.data(), kSignature, sizeof kSignature) != 0) {
        error = "Y8PC の ADPCM ファイルではありません";
        return false;
    }
    if (file[4] != kVersion) {
        error = "ファイルの版 " + std::to_string(file[4]) + " は読めません";
        return false;
    }
    const uint8_t content = file[5];
    // ダンプの位置は 9＋7×件数で決まるので、bit2-7 に何かを足す余地は無い
    // （pcmfile.md「版と、知らないものに出会ったとき」）。立っていれば別の形の
    // ファイルなので読まない。
    if (content & ~(kHasSettings | kHasDump)) {
        error = "ヘッダの中身のビットに知らないものがあります";
        return false;
    }
    out.hasSettings = (content & kHasSettings) != 0;
    out.hasDump     = (content & kHasDump) != 0;

    const uint8_t  count = file[6];
    const uint16_t pages = readLe16(file.data() + 7);
    if (count > kMaxSettings) {
        error = "設定の件数 " + std::to_string(count) + " が 32 を超えています";
        return false;
    }
    if (pages > kAdpcmPages) {
        error = "ダンプのページ数 " + std::to_string(pages) + " が 1024 を超えています";
        return false;
    }
    if ((!out.hasSettings && count != 0) || (!out.hasDump && pages != 0)) {
        error = "ヘッダの中身のビットと、件数・ページ数が合いません";
        return false;
    }

    const size_t need = kHeaderSize + kSettingSize * count + kAdpcmPageSize * pages;
    if (file.size() < need) {
        error = "ファイルがヘッダの言う長さ " + std::to_string(need) + " バイトより短い";
        return false;
    }

    // 全件を確かめてから入れる。1件でも外れていればファイル全体を拒む。
    std::array<AdpcmVoiceFile, kAdpcmFiles> settings{};
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t* p = file.data() + kHeaderSize + kSettingSize * i;
        const uint8_t  no = p[0];
        if (no >= kAdpcmFiles) {
            error = "設定 " + std::to_string(i) + " のボイスファイル番号 " +
                    std::to_string(no) + " が範囲外です";
            return false;
        }
        AdpcmVoiceFile v;
        v.present      = true;
        v.startPage    = readLe16(p + 1);
        v.pages        = readLe16(p + 3);
        v.sampleRateHz = readLe16(p + 5);
        if (v.pages == 0 || static_cast<uint32_t>(v.startPage) + v.pages > kAdpcmPages) {
            error = "ボイスファイル " + std::to_string(no) + " の範囲が ADPCM メモリに収まりません";
            return false;
        }
        if (v.sampleRateHz < kAdpcmRateMin || v.sampleRateHz > kAdpcmRateMax) {
            error = "ボイスファイル " + std::to_string(no) + " のサンプリング周波数 " +
                    std::to_string(v.sampleRateHz) + "Hz が範囲外です";
            return false;
        }
        settings[no] = v;   // 同じ番号が2度あれば後のほうが残る
    }
    out.settings = settings;

    const uint8_t* dump = file.data() + kHeaderSize + kSettingSize * count;
    out.dump.assign(dump, dump + kAdpcmPageSize * pages);
    return true;
}

std::array<AdpcmVoiceFile, kAdpcmFiles> resolveAdpcmDirectory(const SequenceBlock& block,
                                                              const PcmFile* pcm) {
    if (pcm && pcm->hasSettings) return pcm->settings;
    return block.adpcm;
}

} // namespace y8960
