#include "check.h"
#include "pcmfile.h"

#include <initializer_list>
#include <string>
#include <vector>

using namespace y8960;

namespace {

using Bytes = std::vector<uint8_t>;

void appendLe16(Bytes& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>(v >> 8));
}

Bytes setting(uint8_t no, uint16_t start, uint16_t pages, uint16_t rate) {
    Bytes b = {no};
    appendLe16(b, start);
    appendLe16(b, pages);
    appendLe16(b, rate);
    return b;
}

Bytes pcmFile(uint8_t content, std::initializer_list<Bytes> settings, uint16_t dumpPages,
              uint8_t version = 1) {
    Bytes b = {'Y', '8', 'P', 'C', version, content, static_cast<uint8_t>(settings.size())};
    appendLe16(b, dumpPages);
    for (const Bytes& s : settings) b.insert(b.end(), s.begin(), s.end());
    for (uint32_t i = 0; i < dumpPages * kAdpcmPageSize; ++i)
        b.push_back(static_cast<uint8_t>(i & 0xFF));
    return b;
}

bool loads(const Bytes& b) {
    PcmFile f;
    std::string err;
    return parsePcmFile(b, f, err);
}

} // namespace

int main() {
    // 設定とダンプの両方を持つファイル
    {
        const Bytes b = pcmFile(0x03, {setting(1, 0, 2, 8000), setting(63, 2, 1, 16000)}, 3);
        PcmFile f;
        std::string err;
        CHECK(parsePcmFile(b, f, err));
        CHECK(f.hasSettings && f.hasDump);
        CHECK(f.settings[1].present && f.settings[1].pages == 2 && f.settings[1].sampleRateHz == 8000);
        CHECK(f.settings[63].present && f.settings[63].startPage == 2);
        CHECK(!f.settings[0].present);
        CHECK(f.dump.size() == 3 * kAdpcmPageSize);
        CHECK(f.dump[kAdpcmPageSize] == 0);
    }

    // 設定だけ、ダンプだけ
    {
        PcmFile f;
        std::string err;
        CHECK(parsePcmFile(pcmFile(0x01, {setting(0, 0, 1, 1800)}, 0), f, err));
        CHECK(f.hasSettings && !f.hasDump && f.dump.empty());
        CHECK(parsePcmFile(pcmFile(0x02, {}, 1), f, err));
        CHECK(!f.hasSettings && f.hasDump && f.dump.size() == kAdpcmPageSize);
    }

    // 中身のビットが立っていて数が 0 なのは、あってよい
    CHECK(loads(pcmFile(0x03, {}, 0)));

    // 同じ番号が2度あれば後のほうが残る
    {
        const Bytes b = pcmFile(0x01, {setting(4, 0, 1, 8000), setting(4, 8, 2, 4000)}, 0);
        PcmFile f;
        std::string err;
        CHECK(parsePcmFile(b, f, err));
        CHECK(f.settings[4].startPage == 8 && f.settings[4].sampleRateHz == 4000);
    }

    // 拒むもの
    CHECK(!loads(Bytes{'Y', '8', 'S', 'Q', 1, 0, 0, 0, 0}));          // 見出しが違う
    CHECK(!loads(pcmFile(0x01, {setting(0, 0, 1, 8000)}, 0, 2)));      // 版が違う
    CHECK(!loads(pcmFile(0x05, {}, 0)));                               // 知らない中身のビット
    CHECK(!loads(pcmFile(0x02, {setting(0, 0, 1, 8000)}, 0)));         // ビットと件数が合わない
    CHECK(loads(pcmFile(0x01, {setting(63, 0, 1, 8000)}, 0)));         // 番号の上端
    CHECK(!loads(pcmFile(0x01, {setting(64, 0, 1, 8000)}, 0)));        // 番号が範囲外
    CHECK(!loads(pcmFile(0x01, {setting(0, 0, 0, 8000)}, 0)));         // ページ数 0
    CHECK(!loads(pcmFile(0x01, {setting(0, 1023, 2, 8000)}, 0)));      // 1024 ページを超える
    CHECK(!loads(pcmFile(0x01, {setting(0, 0, 1, 1799)}, 0)));         // 周波数が低すぎる
    CHECK(!loads(pcmFile(0x01, {setting(0, 0, 1, 16001)}, 0)));        // 周波数が高すぎる
    {
        Bytes b = pcmFile(0x02, {}, 1);
        b.pop_back();
        CHECK(!loads(b));                                              // ダンプが途中で切れている
    }

    // 1件でも外れていれば、ほかの設定も入らない
    {
        const Bytes b = pcmFile(0x01, {setting(0, 0, 1, 8000), setting(1, 0, 0, 8000)}, 0);
        PcmFile f;
        std::string err;
        CHECK(!parsePcmFile(b, f, err));
        CHECK(!f.settings[0].present);
    }

    // Y8PC の設定があれば、ブロックのチャンク 03 は使わない
    {
        SequenceBlock block;
        block.adpcm[0].present = true;
        block.adpcm[0].pages = 1;
        block.adpcm[0].sampleRateHz = 4000;

        PcmFile f;
        std::string err;
        CHECK(parsePcmFile(pcmFile(0x01, {setting(2, 0, 1, 8000)}, 0), f, err));
        const auto merged = resolveAdpcmDirectory(block, &f);
        CHECK(!merged[0].present);
        CHECK(merged[2].present && merged[2].sampleRateHz == 8000);

        // ダンプだけの Y8PC は設定を持たないので、チャンク 03 が残る
        PcmFile dumpOnly;
        CHECK(parsePcmFile(pcmFile(0x02, {}, 1), dumpOnly, err));
        const auto kept = resolveAdpcmDirectory(block, &dumpOnly);
        CHECK(kept[0].present && kept[0].sampleRateHz == 4000);

        const auto none = resolveAdpcmDirectory(block, nullptr);
        CHECK(none[0].present);
    }

    return check::finish("pcmfile_test");
}
