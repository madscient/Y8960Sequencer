#include "block.h"
#include "check.h"

#include <initializer_list>
#include <string>
#include <vector>

using namespace y8960;

namespace {

using Bytes = std::vector<uint8_t>;

void appendChunk(Bytes& b, uint8_t type, const Bytes& body) {
    b.push_back(type);
    b.push_back(static_cast<uint8_t>(body.size() & 0xFF));
    b.push_back(static_cast<uint8_t>(body.size() >> 8));
    b.insert(b.end(), body.begin(), body.end());
}

Bytes track(uint8_t no, uint8_t dev, uint8_t ch, std::initializer_list<uint8_t> events = {0xFF}) {
    Bytes body = {no, dev, ch};
    body.insert(body.end(), events.begin(), events.end());
    return body;
}

// 音色チャンク 01（12 バイト）と 02（32 バイト）の中身。
Bytes voice(uint8_t index, uint8_t fill, size_t size = 12) {
    Bytes body(1 + size, fill);
    body[0] = index;
    return body;
}

// デバイスに属するチャンク（種別 40-7F）の中身。先頭がデバイス番号。
Bytes owned(uint8_t device, uint8_t index, size_t size, uint8_t fill = 0) {
    Bytes body(2 + size, fill);
    body[0] = device;
    body[1] = index;
    return body;
}

// チャンクの列をヘッダで包む。長さはヘッダを含めて自動で入れる。
Bytes block(std::initializer_list<std::pair<int, Bytes>> chunks, uint8_t version = 1) {
    Bytes b = {'Y', '8', 'S', 'Q', version, 0, 0};
    for (const auto& c : chunks) appendChunk(b, static_cast<uint8_t>(c.first), c.second);
    b[5] = static_cast<uint8_t>(b.size() & 0xFF);
    b[6] = static_cast<uint8_t>(b.size() >> 8);
    return b;
}

bool load(const Bytes& b, SequenceBlock& out, std::string& err) {
    return loadBlock(b, out, err);
}

bool loads(const Bytes& b) {
    SequenceBlock s;
    std::string err;
    return load(b, s, err);
}

} // namespace

int main() {
    // 生のブロック
    {
        const Bytes b = block({{0x00, track(0, 1, 0, {0x00, 48, 0xFF})}});
        SequenceBlock s;
        std::string err;
        CHECK(load(b, s, err));
        CHECK(s.version == 1);
        CHECK(s.tracks[0].assigned);
        CHECK(s.tracks[0].device == Device::OPLLEX1);
        CHECK(s.tracks[0].events == (Bytes{0x00, 48, 0xFF}));
        CHECK(!s.tracks[1].assigned);
    }

    // BSAVE の見出しが付いた形。見出しの中身は読まない
    {
        const Bytes raw = block({{0x00, track(3, 7, 4)}});
        Bytes b = {0xFE, 0x00, 0x80, 0xFF, 0x80, 0x00, 0x00};
        b.insert(b.end(), raw.begin(), raw.end());
        SequenceBlock s;
        std::string err;
        CHECK(load(b, s, err));
        CHECK(s.tracks[3].assigned && s.tracks[3].device == Device::SCC && s.tracks[3].channel == 4);
    }

    // BSAVE の見出しの印が無ければ、7 バイト目の署名は探さない
    {
        const Bytes raw = block({{0x00, track(0, 0, 0)}});
        Bytes b = {0x00, 0, 0, 0, 0, 0, 0};
        b.insert(b.end(), raw.begin(), raw.end());
        CHECK(!loads(b));
    }

    // ブロック長の後ろの余りは無視する（配列に保存すると偶数に切り上がる）
    {
        Bytes b = block({{0x00, track(0, 0, 0)}});
        b.push_back(0x00);
        CHECK(loads(b));
    }

    // 版が新しいものは読まない
    CHECK(!loads(block({{0x00, track(0, 0, 0)}}, 2)));

    // ブロック長がファイルより長い
    {
        Bytes b = block({{0x00, track(0, 0, 0)}});
        b.pop_back();
        CHECK(!loads(b));
    }

    // 知らない種別: 7Fh 以下は拒否、80h 以上は読み飛ばす
    CHECK(!loads(block({{0x04, Bytes{1, 2, 3}}})));
    CHECK(!loads(block({{0x7F, Bytes{}}})));
    {
        const Bytes b = block({{0x80, Bytes{1, 2, 3}}, {0x00, track(5, 0, 2)}});
        SequenceBlock s;
        std::string err;
        CHECK(load(b, s, err));
        CHECK(s.tracks[5].assigned);
    }

    // 音色チャンク。01 は 12 バイト、02 は 32 バイト
    {
        const Bytes b = block({{0x01, voice(31, 0x11)}, {0x02, voice(0, 0x22, 32)}, {0x01, voice(31, 0x33)}});
        SequenceBlock s;
        std::string err;
        CHECK(load(b, s, err));
        CHECK(s.voices[0].kind == RecordKind::SccWave && s.voices[0].data[31] == 0x22);
        CHECK(s.voices[31].kind == RecordKind::FmVoice && s.voices[31].data[11] == 0x33);
        CHECK(s.voices[31].data[12] == 0);
    }
    CHECK(!loads(block({{0x01, voice(0, 0, 32)}})));    // 01 の前の形（32 バイト）
    CHECK(!loads(block({{0x02, voice(0, 0, 12)}})));

    // 索引 32-34 は OPL2EX のリズム音色、35 以上は拒む
    {
        const Bytes b = block({{0x01, voice(32, 0x44)}, {0x01, voice(34, 0x55)}});
        SequenceBlock s;
        std::string err;
        CHECK(load(b, s, err));
        CHECK(s.voices[32].kind == RecordKind::FmVoice && s.voices[32].data[1] == 0x44);
        CHECK(s.voices[34].kind == RecordKind::FmVoice && s.voices[34].data[1] == 0x55);
        CHECK(s.voices[33].kind == RecordKind::None);
    }
    CHECK(!loads(block({{0x01, voice(35, 0)}})));

    // ADPCM のボイスファイルの控え（チャンク 03）
    {
        const Bytes b = block({{0x03, Bytes{5, 0x10, 0x00, 0x04, 0x00, 0x40, 0x1F}},
                               {0x03, Bytes{63, 0x00, 0x00, 0x01, 0x00, 0x40, 0x1F}}});
        SequenceBlock s;
        std::string err;
        CHECK(load(b, s, err));
        CHECK(s.adpcm[5].present);
        CHECK(s.adpcm[5].startPage == 16 && s.adpcm[5].pages == 4 && s.adpcm[5].sampleRateHz == 8000);
        CHECK(s.adpcm[63].present);           // 番号は 0-63
        CHECK(!s.adpcm[0].present);
    }

    // 壊れたチャンク 03 は、その項目だけ捨ててブロックは読む
    {
        const std::initializer_list<Bytes> bad = {
            Bytes{64, 0, 0, 1, 0, 0x40, 0x1F},           // 番号が範囲外
            Bytes{0, 0, 0, 0, 0, 0x40, 0x1F},            // ページ数 0
            Bytes{0, 0x00, 0x04, 0x01, 0x00, 0x40, 0x1F},// 1024 ページを超える
            Bytes{0, 0, 0, 1, 0, 0x07, 0x07},            // 1800Hz 未満
            Bytes{0, 0, 0, 1, 0, 0x40},                  // 長さが 7 でない
        };
        for (const Bytes& body : bad) {
            SequenceBlock s;
            std::string err;
            CHECK(load(block({{0x03, body}, {0x00, track(0, 0, 0)}}), s, err));
            CHECK(s.tracks[0].assigned);
            CHECK(s.warnings.size() == 1);
            for (const auto& a : s.adpcm) CHECK(!a.present);
        }
    }
    {
        SequenceBlock s;
        std::string err;
        CHECK(load(block({{0x03, Bytes{5, 0x10, 0x00, 0x04, 0x00, 0x40, 0x1F}}}), s, err));
        CHECK(s.warnings.empty());
    }

    {
        Bytes shortVoice = voice(0, 0);
        shortVoice.pop_back();
        CHECK(!loads(block({{0x01, shortVoice}})));
    }

    // トラックの範囲
    CHECK(!loads(block({{0x00, track(16, 0, 0)}})));
    CHECK(loads(block({{0x00, track(0, 8, 24)}})));     // OPL3 は 0-24
    CHECK(!loads(block({{0x00, track(0, 8, 25)}})));
    CHECK(loads(block({{0x00, track(0, 9, 7)}})));      // OPM は 0-7
    CHECK(!loads(block({{0x00, track(0, 9, 8)}})));
    CHECK(loads(block({{0x00, track(0, 10, 10)}})));    // OPNA と OPNB は 0-10
    CHECK(!loads(block({{0x00, track(0, 11, 11)}})));
    CHECK(!loads(block({{0x00, track(0, 0, 6)}})));     // SSGS は 0-5
    CHECK(!loads(block({{0x00, track(0, 7, 5)}})));     // SCC は 0-4
    CHECK(!loads(block({{0x00, track(0, 5, 4)}})));     // DCSG は 0-3
    CHECK(!loads(block({{0x00, track(0, 1, 9)}})));     // OPLLEX に ADPCM は無い
    CHECK(loads(block({{0x00, track(0, 3, 9)}})));      // OPL2EX の 9 はモードに依らない
    CHECK(!loads(block({{0x00, Bytes{0, 0, 0}}})));     // イベント列が空

    // 同じチャンネルを2本のトラックが持つ
    CHECK(!loads(block({{0x00, track(0, 5, 1)}, {0x00, track(1, 5, 1)}})));
    CHECK(loads(block({{0x00, track(0, 5, 1)}, {0x00, track(1, 6, 1)}})));

    // 鳴らさないデバイス（12 以上）のトラックは割り当てず、残りを読む
    {
        SequenceBlock s;
        std::string err;
        CHECK(load(block({{0x00, track(0, 12, 99)}, {0x00, track(1, 255, 0)}, {0x00, track(2, 0, 0)}}), s, err));
        CHECK(!s.tracks[0].assigned && !s.tracks[1].assigned);
        CHECK(s.tracks[2].assigned);
        CHECK(s.warnings.size() == 2);
    }

    // 排他のチャンネル。OPL3 のリズムと 6-8、4OP と組の 2OP
    {
        SequenceBlock s;
        std::string err;
        CHECK(load(block({{0x00, track(0, 8, 24)}, {0x00, track(1, 8, 5)}}), s, err));
        CHECK(s.rhythmMode[8]);
    }
    CHECK(!loads(block({{0x00, track(0, 8, 24)}, {0x00, track(1, 8, 6)}})));
    CHECK(!loads(block({{0x00, track(0, 8, 18)}, {0x00, track(1, 8, 0)}})));
    CHECK(!loads(block({{0x00, track(0, 8, 18)}, {0x00, track(1, 8, 3)}})));
    CHECK(loads(block({{0x00, track(0, 8, 18)}, {0x00, track(1, 8, 1)}})));
    CHECK(!loads(block({{0x00, track(0, 8, 21)}, {0x00, track(1, 8, 9)}})));
    CHECK(!loads(block({{0x00, track(0, 8, 23)}, {0x00, track(1, 8, 14)}})));
    CHECK(loads(block({{0x00, track(0, 8, 23)}, {0x00, track(1, 8, 13)}})));
    // OPNA のリズムに排他は無く、リズムモードも持たない
    {
        SequenceBlock s;
        std::string err;
        CHECK(load(block({{0x00, track(0, 10, 9)}, {0x00, track(1, 10, 6)}}), s, err));
        CHECK(!s.rhythmMode[10]);
    }

    // チャンク 40（OPM・OPNA・OPNB の音色）はデバイスごとの集合に入る
    {
        const Bytes b = block({{0x40, owned(9, 0, 32, 0x11)}, {0x40, owned(11, 31, 32, 0x22)},
                               {0x01, voice(0, 0x33)}, {0x40, owned(12, 0, 5)}});
        SequenceBlock s;
        std::string err;
        CHECK(load(b, s, err));
        CHECK(s.deviceVoices[0][0].kind == RecordKind::OpnVoice && s.deviceVoices[0][0].data[31] == 0x11);
        CHECK(s.deviceVoices[2][31].kind == RecordKind::OpnVoice);
        CHECK(s.seqVoice(Device::OPM, 0) == &s.deviceVoices[0][0]);
        CHECK(s.seqVoice(Device::OPNA, 0) == nullptr);
        CHECK(s.seqVoice(Device::OPNB, 31) == &s.deviceVoices[2][31]);
        CHECK(s.seqVoice(Device::OPLLEX1, 0) == &s.voices[0]);
        CHECK(s.seqVoice(Device::OPL3, 0) == &s.voices[0]);
        CHECK(s.seqVoice(Device::SCC, 1) == nullptr);
    }
    CHECK(!loads(block({{0x40, owned(3, 0, 32)}})));    // デバイス 0-8 は名指せない
    CHECK(!loads(block({{0x40, owned(8, 0, 32)}})));
    CHECK(!loads(block({{0x40, owned(9, 32, 32)}})));   // 索引は 0-31
    CHECK(!loads(block({{0x40, owned(9, 0, 31)}})));
    CHECK(!loads(block({{0x40, Bytes{}}})));            // デバイス番号が無い

    // チャンク 41（ADPCM-A のサンプル）
    {
        const Bytes b = block({{0x41, Bytes{11, 255, 0x02, 0x01, 0x03, 0x00}},
                               {0x41, Bytes{10, 1, 0, 0, 1, 0}}});
        SequenceBlock s;
        std::string err;
        CHECK(load(b, s, err));
        CHECK(s.adpcmA[255].present && s.adpcmA[255].startPage == 0x0102 && s.adpcmA[255].pages == 3);
        CHECK(!s.adpcmA[1].present);                    // OPNB のものだけを持つ
    }
    CHECK(!loads(block({{0x41, Bytes{5, 0, 0, 0, 1, 0}}})));
    CHECK(!loads(block({{0x41, Bytes{11, 0, 0, 0, 1}}})));
    CHECK(loads(block({{0x41, Bytes{12, 0, 0, 0}}})));  // 鳴らさないデバイスのものは中身を見ない

    // チャンク 42（OPL3 の 4OP の音色）は共有の集合に入る
    {
        SequenceBlock s;
        std::string err;
        CHECK(load(block({{0x42, owned(8, 5, 24, 0x44)}}), s, err));
        CHECK(s.voices[5].kind == RecordKind::Fm4op && s.voices[5].data[23] == 0x44);
    }
    CHECK(!loads(block({{0x42, owned(9, 5, 24)}})));
    CHECK(!loads(block({{0x42, owned(8, 32, 24)}})));
    CHECK(!loads(block({{0x42, owned(8, 5, 23)}})));

    // 鳴らすデバイスについて知らない種別は拒み、鳴らさないデバイスのものは読み飛ばす
    CHECK(!loads(block({{0x43, Bytes{9, 0}}})));
    CHECK(loads(block({{0x43, Bytes{12, 0}}})));

    // チャンク 80（メタ情報）。知らない項目は飛ばし、壊れていても拒まない
    {
        const Bytes meta = {0x01, 2, 0x98, 0x11,              // 4504 は範囲の外
                            0x02, 1, 100,
                            0x09, 3, 1, 2, 3,                 // 知らない項目
                            0x03, 5, 'T', 'u', 'n', 'e', 0x81,
                            0x04, 2, 'M', 'e'};
        SequenceBlock s;
        std::string err;
        CHECK(load(block({{0x80, meta}, {0x00, track(0, 0, 0)}}), s, err));
        CHECK(s.masterPitch == 4400);
        CHECK(s.masterVolume == 100);
        CHECK(s.title == "Tune?");
        CHECK(s.author == "Me");
        CHECK(s.warnings.size() == 1);
    }
    {
        SequenceBlock s;
        std::string err;
        CHECK(load(block({{0x80, Bytes{0x01, 2, 0x34, 0x11, 0x03, 9, 'x'}}}), s, err));
        CHECK(s.masterPitch == 4404);
        CHECK(s.warnings.size() == 1);                 // 切れている
    }

    // リズムモードの推定
    {
        const Bytes b = block({{0x00, track(0, 2, 10)}, {0x00, track(1, 4, 7)}});
        SequenceBlock s;
        std::string err;
        CHECK(load(b, s, err));
        CHECK(s.rhythmMode[2]);
        CHECK(!s.rhythmMode[4]);
        CHECK(!s.rhythmMode[1]);
    }
    CHECK(!loads(block({{0x00, track(0, 1, 10)}, {0x00, track(1, 1, 6)}})));

    // チャンク 04（ソフトウェアエンベロープ）。値の範囲は見ず、番号と長さだけを見る。
    {
        const Bytes b = block({{0x00, track(0, 0, 0)}, {0x04, Bytes{5, 16, 20, 8, 10}},
                               {0x04, Bytes{31, 40, 0, 99, 32}}});
        SequenceBlock s;
        std::string err;
        CHECK(load(b, s, err));
        CHECK(s.envelopes[5].present);
        CHECK(s.envelopes[5].ar == 16 && s.envelopes[5].dr == 20);
        CHECK(s.envelopes[5].sl == 8 && s.envelopes[5].rr == 10);
        CHECK(s.envelopes[31].present && s.envelopes[31].ar == 40 && s.envelopes[31].sl == 99);
        CHECK(!s.envelopes[1].present);
    }
    CHECK(!loads(block({{0x00, track(0, 0, 0)}, {0x04, Bytes{0, 1, 1, 1, 1}}})));
    CHECK(!loads(block({{0x00, track(0, 0, 0)}, {0x04, Bytes{32, 1, 1, 1, 1}}})));
    CHECK(!loads(block({{0x00, track(0, 0, 0)}, {0x04, Bytes{1, 1, 1, 1}}})));
    CHECK(!loads(block({{0x00, track(0, 0, 0)}, {0x04, Bytes{1, 1, 1, 1, 1, 1}}})));

    // チャンク見出しの途中で切れている
    {
        Bytes b = block({{0x00, track(0, 0, 0)}});
        b.push_back(0x80);
        b.push_back(0x00);
        b[5] = static_cast<uint8_t>(b.size());
        CHECK(!loads(b));
    }

    return check::finish("block_test");
}
