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
constexpr uint8_t  kMaxSettings = kAdpcmFiles;
constexpr uint16_t kAdpcmRateMin = 1800;
constexpr uint16_t kAdpcmRateMax = 16000;

uint16_t readLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

} // namespace

bool parsePcmFile(const std::vector<uint8_t>& file, PcmFile& out, std::string& error) {
    out = PcmFile{};

    if (file.size() < kHeaderSize || std::memcmp(file.data(), kSignature, sizeof kSignature) != 0) {
        error = "not a Y8PC ADPCM file";
        return false;
    }
    if (file[4] != kVersion) {
        error = "unsupported file version " + std::to_string(file[4]);
        return false;
    }
    const uint8_t content = file[5];
    // ダンプの位置は 9＋7×件数で決まるので、bit2-7 に何かを足す余地は無い
    // （pcmfile.md「版と、知らないものに出会ったとき」）。立っていれば別の形の
    // ファイルなので読まない。
    if (content & ~(kHasSettings | kHasDump)) {
        error = "unknown content bits in the header";
        return false;
    }
    out.hasSettings = (content & kHasSettings) != 0;
    out.hasDump     = (content & kHasDump) != 0;

    const uint8_t  count = file[6];
    const uint16_t pages = readLe16(file.data() + 7);
    if (count > kMaxSettings) {
        error = "setting count " + std::to_string(count) + " exceeds " + std::to_string(kMaxSettings);
        return false;
    }
    if (pages > kAdpcmPages) {
        error = "dump page count " + std::to_string(pages) + " exceeds " + std::to_string(kAdpcmPages);
        return false;
    }
    if ((!out.hasSettings && count != 0) || (!out.hasDump && pages != 0)) {
        error = "content bits do not match the setting and page counts";
        return false;
    }

    const size_t need = kHeaderSize + kSettingSize * count + kAdpcmPageSize * pages;
    if (file.size() < need) {
        error = "file is shorter than the " + std::to_string(need) + " bytes the header says";
        return false;
    }

    // 全件を確かめてから入れる。1件でも外れていればファイル全体を拒む。
    std::array<AdpcmVoiceFile, kAdpcmFiles> settings{};
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t* p = file.data() + kHeaderSize + kSettingSize * i;
        const uint8_t  no = p[0];
        if (no >= kAdpcmFiles) {
            error = "setting " + std::to_string(i) + ": voice file number " +
                    std::to_string(no) + " is out of range";
            return false;
        }
        AdpcmVoiceFile v;
        v.present      = true;
        v.startPage    = readLe16(p + 1);
        v.pages        = readLe16(p + 3);
        v.sampleRateHz = readLe16(p + 5);
        if (v.pages == 0 || static_cast<uint32_t>(v.startPage) + v.pages > kAdpcmPages) {
            error = "voice file " + std::to_string(no) + " does not fit in the ADPCM memory";
            return false;
        }
        if (v.sampleRateHz < kAdpcmRateMin || v.sampleRateHz > kAdpcmRateMax) {
            error = "voice file " + std::to_string(no) + ": sampling rate " +
                    std::to_string(v.sampleRateHz) + "Hz is out of range";
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
