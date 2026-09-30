// ブロックを作り、シーケンサとドライバを通して実際のエミュレータで鳴らす。
// エミュレータのライブラリが実行ファイルと同じフォルダに要る。

#include "check.h"
#include "chips.h"
#include "platform.h"
#include "player.h"
#include "sequencer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace y8960;

namespace {

constexpr uint32_t kRate = 48000;

// 持続する FM 音色のレコード（チャンク 01 の 12 バイト）。20h の bit5（EG タイプ）が
// 無いと、アタックの直後にリリースへ入る。
VoiceRecord fmVoice() {
    VoiceRecord v;
    v.kind = RecordKind::FmVoice;
    auto& d = v.data;
    d[0] = 0x01;                   // CON = 1（並列）、FB 0
    d[1] = 0;                      // 移調 0
    d[2] = 0x1F; d[3] = 0xF0; d[4] = 0x0F; d[5] = 0x21; d[6] = 0;     // モジュレータ
    d[7] = 0x00; d[8] = 0xF0; d[9] = 0x0F; d[10] = 0x21; d[11] = 0;   // キャリア
    return v;
}

VoiceRecord sccWave() {
    VoiceRecord v;
    v.kind = RecordKind::SccWave;
    for (int i = 0; i < kVoiceRecSize; ++i) {
        v.data[static_cast<size_t>(i)] = static_cast<uint8_t>(i < 16 ? 0x50 : 0xB0);
    }
    return v;
}

void addTrack(SequenceBlock& b, int index, Device device, uint8_t channel,
              const std::vector<uint8_t>& events) {
    TrackData& t = b.tracks[static_cast<size_t>(index)];
    t.assigned = true;
    t.device   = device;
    t.channel  = channel;
    t.events   = events;
    t.events.push_back(0xFF);
}

struct Stereo {
    std::vector<float> l, r;
};

// 新しく開いたエミュレータでブロックを鳴らす。位相などの内部状態を揃えるため。
Stereo renderFresh(const SequenceBlock& b, uint32_t samples, const std::vector<uint8_t>* adpcm = nullptr) {
    Stereo out;
    out.l.assign(samples, 0.0f);
    out.r.assign(samples, 0.0f);
    Y8960Chips c;
    std::string err;
    if (!c.open(executableDirectory(), kRate, err)) return out;
    if (adpcm) c.loadAdpcmMemory(*adpcm);
    DeviceSet d(c);
    d.resetAll();
    d.setAdpcmDirectory(b.adpcm.data());
    d.setAdpcmASamples(b.adpcmA.data());
    Sequencer s(d, TickRate::Hz200);
    s.load(0, b);
    Player p(c, s, TickRate::Hz200, kRate);
    s.start(0, 1);
    p.render(out.l.data(), out.r.data(), samples);
    return out;
}

double rmsOf(const std::vector<float>& x, size_t from) {
    double sum = 0;
    for (size_t i = from; i < x.size(); ++i) sum += double(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(x.size() - from));
}

// 平均を引いた波形が負から正へ横切る時刻（線形補間）から、周期の平均を出す。
double frequency(const std::vector<float>& x, size_t from) {
    double mean = 0;
    for (size_t i = from; i < x.size(); ++i) mean += x[i];
    mean /= static_cast<double>(x.size() - from);
    double first = -1, last = -1;
    int crossings = 0;
    for (size_t i = from + 1; i < x.size(); ++i) {
        const double a = x[i - 1] - mean, b = x[i] - mean;
        if (a < 0 && b >= 0) {
            const double t = static_cast<double>(i - 1) + a / (a - b);
            if (first < 0) first = t;
            last = t;
            ++crossings;
        }
    }
    if (crossings < 2) return 0;
    return (crossings - 1) * static_cast<double>(kRate) / (last - first);
}

double rms(Player& player, uint32_t samples) {
    std::vector<float> l(samples), r(samples);
    player.render(l.data(), r.data(), samples);
    double sum = 0;
    for (uint32_t i = 0; i < samples; ++i) sum += 0.5 * (double(l[i]) * l[i] + double(r[i]) * r[i]);
    return std::sqrt(sum / samples);
}

} // namespace

int main() {
    Y8960Chips chips;
    std::string error;
    if (!chips.open(executableDirectory(), kRate, error)) {
        std::fprintf(stderr, "render_test: %s\n", error.c_str());
        return 1;
    }

    SequenceBlock block;
    block.version = 1;
    block.voices[0] = fmVoice();
    block.voices[1] = sccWave();

    // どれも 4分音符 ×2。音色を持つものは先に `85` で選ぶ。
    const std::vector<uint8_t> fmPart  = {0x85, 0x00, 0x00, 48, 0x04, 48};
    const std::vector<uint8_t> sccPart = {0x85, 0x01, 0x00, 48, 0x04, 48};
    const std::vector<uint8_t> plain   = {0x00, 48, 0x04, 48};
    addTrack(block, 0, Device::SSGS,    0, plain);
    addTrack(block, 1, Device::OPLLEX1, 0, fmPart);
    addTrack(block, 2, Device::OPL2EX1, 0, fmPart);
    addTrack(block, 3, Device::DCSG1,   0, plain);
    addTrack(block, 4, Device::SCC,     0, sccPart);

    // ADPCM。ボイスファイル 0 を 1KB ぶん置き、その速さで鳴らす。中身は
    // ADPCM-B として解釈されれば何かの波形になるだけの並び。
    block.adpcm[0].present      = true;
    block.adpcm[0].startPage    = 0;
    block.adpcm[0].pages        = 4;
    block.adpcm[0].sampleRateHz = 8000;
    std::vector<uint8_t> sample(4 * 256);
    for (size_t i = 0; i < sample.size(); ++i) sample[i] = static_cast<uint8_t>((i % 8 < 4) ? 0x33 : 0xCC);
    chips.loadAdpcmMemory(sample);
    addTrack(block, 5, Device::OPL2EX2, kChannelAdpcm, {0x82, 0x00, 0x00, 48, 0x04, 48});

    DeviceSet devices(chips);
    devices.resetAll();
    devices.setAdpcmDirectory(block.adpcm.data());
    Sequencer seq(devices, TickRate::Hz200);
    seq.load(0, block);
    Player player(chips, seq, TickRate::Hz200, kRate);

    const double before = rms(player, kRate / 10);
    seq.start(0, 1);
    const double playing = rms(player, kRate / 2);     // 2音のうち1音目の途中まで
    std::printf("before=%.5f playing=%.5f\n", before, playing);
    CHECK(before < 0.001);
    CHECK(playing > 0.02);

    // 曲は 96 tick で終わる。テンポ 120 なら 1 秒。
    rms(player, kRate);
    CHECK(seq.finished());
    const double after = rms(player, kRate / 10);
    std::printf("after=%.5f\n", after);
    CHECK(after < playing * 0.25);

    // ADPCM チャンネルだけのブロック。ほかの音源が鳴っていないところで見る。
    {
        SequenceBlock only;
        only.version = 1;
        only.adpcm = block.adpcm;
        addTrack(only, 0, Device::OPL2EX2, kChannelAdpcm, {0x82, 0x00, 0x00, 48});

        devices.resetAll();
        devices.setAdpcmDirectory(only.adpcm.data());
        Sequencer seq2(devices, TickRate::Hz200);
        seq2.load(0, only);
        Player player2(chips, seq2, TickRate::Hz200, kRate);
        const double silence = rms(player2, kRate / 10);
        seq2.start(0, 1);
        const double sounding = rms(player2, kRate / 4);
        std::printf("adpcm silence=%.5f playing=%.5f\n", silence, sounding);
        CHECK(silence < 0.001);
        CHECK(sounding > 0.01);

        // レベルメーターの元。鳴っているブロックだけに山が立つ。
        // 「前に読んでから」の山なので、いったん読み捨ててから測る。
        for (int d = 0; d < kDeviceCount; ++d) chips.takeLevel(static_cast<Device>(d));
        rms(player2, kRate / 20);
        const float adpcmLevel = chips.takeLevel(Device::OPL2EX2);
        const float ssgsLevel  = chips.takeLevel(Device::SSGS);
        std::printf("level OPL2EX2=%.4f SSGS=%.4f\n", adpcmLevel, ssgsLevel);
        CHECK(adpcmLevel > 0.01f);
        CHECK(ssgsLevel == 0.0f);
        // 読むと 0 に戻る。
        CHECK(chips.takeLevel(Device::OPL2EX2) == 0.0f);

        // ブロックごとの音量。0 にすればそのブロックは出てこない。
        // Player は割り込みの間隔1回ぶんを先に作ってためるので、その分は前の音量で出る。
        chips.setGain(Device::OPL2EX2, 0.0f);
        rms(player2, kRate / 100);
        chips.takeLevel(Device::OPL2EX2);
        const double muted = rms(player2, kRate / 20);
        CHECK(muted < 0.001);
        CHECK(chips.takeLevel(Device::OPL2EX2) == 0.0f);
        chips.setGain(Device::OPL2EX2, 1.0f);
        const double back = rms(player2, kRate / 20);
        std::printf("muted=%.5f back=%.5f\n", muted, back);
        CHECK(back > 0.01);
    }

    // ADPCM が実際にサンプルメモリを読んでいるか。音が出るだけでは、メモリを読まず
    // に出る音と見分けられないので、中身を変えて出力が変わることを見る。
    {
        auto renderAdpcm = [&](const std::vector<uint8_t>& memory) {
            SequenceBlock only;
            only.version = 1;
            only.adpcm = block.adpcm;
            addTrack(only, 0, Device::OPL2EX2, kChannelAdpcm, {0x82, 0x00, 0x00, 48});
            chips.loadAdpcmMemory(memory);
            devices.resetAll();
            devices.setAdpcmDirectory(only.adpcm.data());
            Sequencer s(devices, TickRate::Hz200);
            s.load(0, only);
            Player p(chips, s, TickRate::Hz200, kRate);
            rms(p, kRate / 20);                          // 前の音を流しきる
            s.start(0, 1);
            std::vector<float> l(kRate / 5), r(kRate / 5);
            p.render(l.data(), r.data(), static_cast<uint32_t>(l.size()));
            return l;
        };
        std::vector<uint8_t> patternA(4 * 256), patternB(4 * 256);
        for (size_t i = 0; i < patternA.size(); ++i) {
            patternA[i] = static_cast<uint8_t>((i % 8 < 4) ? 0x33 : 0xCC);
            patternB[i] = static_cast<uint8_t>((i % 32 < 16) ? 0x77 : 0x99);
        }
        const auto a = renderAdpcm(patternA);
        const auto b = renderAdpcm(patternB);
        double diff = 0;
        for (size_t i = 0; i < a.size(); ++i) diff += std::fabs(double(a[i]) - b[i]);
        diff /= static_cast<double>(a.size());
        std::printf("adpcm memory A vs B: mean |diff| = %.5f\n", diff);
        CHECK(diff > 0.001);
    }

    // 呼び出し側が一度に求めるサンプル数で音が変わらないこと。リアルタイム再生では
    // 音声出力が求める量が負荷で揺れる。同じ tick の KEY OFF → KEY ON は、
    // エミュレータが間に少し音を作って KEY OFF を見せるが、求める量が割り込みの
    // 直後で切れているとその分が作れない。
    {
        SequenceBlock legato;
        legato.version = 1;
        legato.voices[0] = fmVoice();
        // 同じ音を4つ。クオンタイズ 8 なので、音の境目で KEY OFF と KEY ON が同じ tick に来る。
        addTrack(legato, 0, Device::OPLLEX1, 0, {0x85, 0x00, 0x00, 24, 0x00, 24, 0x00, 24, 0x00, 24});
        addTrack(legato, 1, Device::OPL2EX1, 0, {0x85, 0x00, 0x00, 24, 0x00, 24, 0x00, 24, 0x00, 24});
        const uint32_t total = kRate;
        auto renderIn = [&](uint32_t chunk) {
            devices.resetAll();
            Sequencer s(devices, TickRate::Hz200);
            s.load(0, legato);
            Player p(chips, s, TickRate::Hz200, kRate);
            rms(p, kRate / 20);
            s.start(0, 1);
            std::vector<float> l(total), r(total);
            for (uint32_t at = 0; at < total; at += chunk) {
                p.render(l.data() + at, r.data() + at, std::min(chunk, total - at));
            }
            return l;
        };
        // 1ms ごとの RMS が、鳴っているあいだの中央値の 1/10 を切る区間の数。
        // KEY OFF が効けば、3つの音の境目に谷ができる。
        auto dips = [&](const std::vector<float>& l) {
            const uint32_t w = kRate / 1000;
            std::vector<double> env;
            for (uint32_t at = 0; at + w <= total; at += w) {
                double sum = 0;
                for (uint32_t i = 0; i < w; ++i) sum += double(l[at + i]) * l[at + i];
                env.push_back(std::sqrt(sum / w));
            }
            // 最初の音の立ち上がりと最後の音の後を除く
            std::vector<double> body(env.begin() + 20, env.begin() + 980);
            std::vector<double> sorted = body;
            std::sort(sorted.begin(), sorted.end());
            const double floor = sorted[sorted.size() / 2] * 0.1;
            int count = 0;
            bool in = false;
            for (double e : body) {
                if (e < floor && !in) ++count;
                in = e < floor;
            }
            return count;
        };
        for (uint32_t chunk : {total, 1024u, 7u, 1u}) {
            const int n = dips(renderIn(chunk));
            std::printf("chunk %u: %d dips\n", chunk, n);
            CHECK(n == 3);
        }
    }

    // 同じ tick に KEY OFF → KEY ON が重なっても、書き込み順で後ろになるリズムの打撃が
    // 消えないこと。エミュレータは重なりごとに先に数 ms を作るので、重なりが多いと
    // 割り込みの間隔（200Hz で 5ms）に収まらない。
    {
        SequenceBlock crowd;
        crowd.version = 1;
        crowd.voices[0] = fmVoice();
        crowd.rhythmMode[static_cast<size_t>(Device::OPLLEX1)] = true;
        std::vector<uint8_t> melody = {0x85, 0x00};
        std::vector<uint8_t> drums  = {0xA9, 12, 0xAA, 15};
        constexpr int kHits = 8;
        for (int i = 0; i < kHits; ++i) {
            melody.insert(melody.end(), {0x00, 24});    // クオンタイズ 8：境目で KEY OFF と KEY ON が同じ tick
            drums.insert(drums.end(), {0xC8, 0x18, 24}); // BD と SD
        }
        for (int t = 0; t < 5; ++t) addTrack(crowd, t, Device::OPLLEX1, static_cast<uint8_t>(t), melody);
        addTrack(crowd, 5, Device::OPLLEX1, kChannelRhythm, drums);

        const uint32_t total = kRate * 2;
        // エミュレータの内部状態（位相など）を揃えるため、毎回開き直す。
        auto renderWith = [&](const std::vector<int>& muted) {
            Y8960Chips c;
            std::string err;
            std::vector<float> l(total), r(total);
            if (!c.open(executableDirectory(), kRate, err)) return l;
            DeviceSet d(c);
            d.resetAll();
            Sequencer s(d, TickRate::Hz200);
            s.load(0, crowd);
            for (int t : muted) s.setTrackMute(0, t, true);
            Player p(c, s, TickRate::Hz200, kRate);
            s.start(0, 1);
            p.render(l.data(), r.data(), total);
            return l;
        };
        const auto all     = renderWith({});
        const auto noDrums = renderWith({5});
        const auto drumsOn = renderWith({0, 1, 2, 3, 4});
        // 打撃の頭 30ms で、「全部 − リズムだけミュート」が「リズムだけ」の 3 割に届くか。
        const uint32_t hit = kRate / 4, win = kRate * 3 / 100;
        int heard = 0;
        for (int i = 0; i < kHits; ++i) {
            double eo = 0, ed = 0;
            for (uint32_t k = i * hit; k < i * hit + win; ++k) {
                const double dd = double(all[k]) - noDrums[k];
                eo += double(drumsOn[k]) * drumsOn[k];
                ed += dd * dd;
            }
            if (eo > 0 && ed >= eo * 0.09) ++heard;
        }
        std::printf("crowded rhythm hits heard: %d / %d\n", heard, kHits);
        CHECK(heard == kHits);
    }

    // OPL2EX の波形選択が効くこと。チップは YM3812 と同じく WSE（01h の bit5）が
    // 立っていないと E0h-F5h を無視する。キャリアだけを半波サインで鳴らすと、
    // 効いていれば負の側がほとんど出ない。
    {
        SequenceBlock only;
        only.version = 1;
        VoiceRecord v = fmVoice();
        v.data[2] = 0x3F;            // モジュレータは鳴らさない
        v.data[11] = 0x01;           // キャリアは半波サイン
        only.voices[0] = v;
        addTrack(only, 0, Device::OPL2EX1, 0, {0x85, 0x00, 0x00, 96});

        devices.resetAll();
        Sequencer s(devices, TickRate::Hz200);
        s.load(0, only);
        Player p(chips, s, TickRate::Hz200, kRate);
        rms(p, kRate / 20);                              // 前の音を流しきる
        s.start(0, 1);
        std::vector<float> l(kRate / 5), r(kRate / 5);
        p.render(l.data(), r.data(), static_cast<uint32_t>(l.size()));
        const auto [lo, hi] = std::minmax_element(l.begin() + kRate / 50, l.end());
        std::printf("opl2ex half sine: min=%.4f max=%.4f\n", *lo, *hi);
        CHECK(*hi > 0.005f);
        CHECK(*lo > -0.1f * *hi);
    }

    // デバイス 8-11（YMEngine）。O4 A を鳴らし、出てきた音の高さを測る。F-Number・KC・
    // 分周値の計算と、チップのクロックの前提が合っていれば 440Hz になる。
    {
        const auto fmVoice4op = [] {
            VoiceRecord v;
            v.kind = RecordKind::Fm4op;
            v.data[0] = 0x00;                            // FM-FM。キャリアは OP4 だけ
            v.data[3] = 0x01;                            // CS：4OP で鳴らす
            for (int op = 0; op < 4; ++op) {
                uint8_t* o = v.data.data() + 4 + op * 5;
                o[0] = (op == 3) ? 0x00 : 0x3F;
                o[1] = 0xF0; o[2] = 0x0F; o[3] = 0x21; o[4] = 0;
            }
            return v;
        };
        const auto sineOpn = [] {
            VoiceRecord v;
            v.kind = RecordKind::OpnVoice;
            v.data[0] = 0x07;                            // AL 7。C2 だけを鳴らす
            v.data[3] = 0xF0;
            for (int op = 0; op < 4; ++op) {
                uint8_t* o = v.data.data() + 4 + op * 7;
                o[0] = (op == 3) ? 0x00 : 0x7F;
                o[1] = 0x1F; o[4] = 0x0F; o[6] = 0x01;
            }
            return v;
        };
        VoiceRecord sine2op = fmVoice();
        sine2op.data[0] = 0x00;                          // FM。モジュレータは黙らせる
        sine2op.data[2] = 0x3F;

        struct Case {
            const char* name;
            Device      device;
            uint8_t     channel;
        };
        const Case cases[] = {
            {"OPL3 ch0", Device::OPL3, 0},  {"OPL3 ch9", Device::OPL3, 9},  {"OPL3 ch18", Device::OPL3, 18},
            {"OPM ch0", Device::OPM, 0},
            {"OPNA ch0", Device::OPNA, 0},  {"OPNA ch3", Device::OPNA, 3},  {"OPNA ch6", Device::OPNA, 6},
            {"OPNB ch1", Device::OPNB, 1},  {"OPNB ch4", Device::OPNB, 4},  {"OPNB ch6", Device::OPNB, 6},
        };
        for (const Case& c : cases) {
            SequenceBlock b;
            b.version = 1;
            b.voices[0] = (c.channel == 18) ? fmVoice4op() : sine2op;
            b.deviceVoices[static_cast<size_t>(Device::OPNB) - static_cast<size_t>(Device::OPM)][0] = sineOpn();
            b.deviceVoices[static_cast<size_t>(Device::OPNA) - static_cast<size_t>(Device::OPM)][0] = sineOpn();
            b.deviceVoices[0][0] = sineOpn();
            // 左だけに出す。右が黙っていれば `87` がチップの左右に届いている。SSG は定位を持たない。
            addTrack(b, 0, c.device, c.channel, {0x85, 0x00, 0x87, 0x00, 0x80, 4, 0x09, 96});
            const Stereo out = renderFresh(b, kRate / 2);
            const double f = frequency(out.l, kRate / 10);
            const double r = rmsOf(out.r, kRate / 10);
            const double l = rmsOf(out.l, kRate / 10);
            std::printf("%-10s %.2fHz  L rms=%.4f R rms=%.4f\n", c.name, f, l, r);
            CHECK(std::fabs(f - 440.0) < 4.4);
            CHECK(l > 0.005);
            const bool ssg = (c.channel == kOpnSsgFirst);
            if (!ssg) CHECK(r < l * 0.01);
        }
    }

    // OPNA・OPNB の ADPCM-B が、OPL2EX と同じ ADPCM メモリを同じボイスファイルの置き場所で
    // 読むこと。中身を変えて出力が変わることを見る（OPL2EX の ADPCM と同じ観点）。
    for (Device d : {Device::OPNA, Device::OPNB}) {
        SequenceBlock b;
        b.version = 1;
        b.adpcm[0] = AdpcmVoiceFile{true, 4, 4, 8000};
        addTrack(b, 0, d, kOpnAdpcmB, {0x82, 0x00, 0x80, 5, 0x04, 48});
        std::vector<uint8_t> patternA(8 * 256), patternB(8 * 256);
        for (size_t i = 0; i < patternA.size(); ++i) {
            patternA[i] = static_cast<uint8_t>((i % 8 < 4) ? 0x33 : 0xCC);
            patternB[i] = static_cast<uint8_t>((i % 32 < 16) ? 0x77 : 0x99);
        }
        // 置き場所の外（ページ 0-3）だけを変えても、出力は変わらない。
        std::vector<uint8_t> patternC = patternA;
        for (size_t i = 0; i < 4 * 256; ++i) patternC[i] = 0x5A;
        const Stereo a = renderFresh(b, kRate / 5, &patternA);
        const Stereo bb = renderFresh(b, kRate / 5, &patternB);
        const Stereo cc = renderFresh(b, kRate / 5, &patternC);
        double diff = 0, same = 0;
        for (size_t i = 0; i < a.l.size(); ++i) {
            diff += std::fabs(double(a.l[i]) - bb.l[i]);
            same += std::fabs(double(a.l[i]) - cc.l[i]);
        }
        diff /= static_cast<double>(a.l.size());
        same /= static_cast<double>(a.l.size());
        std::printf("%s ADPCM-B: rms=%.4f  A vs B %.5f  A vs C %.5f\n", d == Device::OPNA ? "OPNA" : "OPNB",
                    rmsOf(a.l, 0), diff, same);
        CHECK(rmsOf(a.l, 0) > 0.005);
        CHECK(diff > 0.001);
        CHECK(same < diff * 0.01);
    }

    // OPNA のリズムと OPNB の ADPCM-A は、サンプルの中身をこのプレイヤーが渡せない
    // （OPNA は内蔵 ROM、OPNB はサンプル ROM）。中身が無いと 0 のバイト列を復号した
    // 大きな雑音になるので、叩いても鳴らないこと。
    {
        SequenceBlock b;
        b.version = 1;
        b.adpcmA[0] = AdpcmASample{true, 0, 16};
        addTrack(b, 0, Device::OPNA, kOpnRhythm, {0xA9, 15, 0xC8, 0x3F, 96});
        addTrack(b, 1, Device::OPNB, kOpnRhythm, {0xA9, 15, 0xD9, 0x3F, 0, 0xC8, 0x3F, 96});
        const Stereo out = renderFresh(b, kRate / 2);
        std::printf("ADPCM-A without samples: rms=%.5f\n", rmsOf(out.l, 0));
        CHECK(rmsOf(out.l, 0) < 0.001);
    }

    return check::finish("render_test");
}
