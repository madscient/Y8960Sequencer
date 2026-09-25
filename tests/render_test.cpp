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

// 持続する FM 音色のレコード。20h の bit5（EG タイプ）が無いと、アタックの
// 直後にリリースへ入る。
VoiceRecord fmVoice() {
    VoiceRecord v;
    v.kind = RecordKind::FmVoice;
    auto& d = v.data;
    d[8] = 0; d[9] = 0;            // トランスポーズ 0
    d[10] = 0x01;                  // CON = 1（並列）、FB 0
    d[16 + 0] = 0x21; d[16 + 1] = 0x1F; d[16 + 2] = 0xF0; d[16 + 3] = 0x0F; d[16 + 5] = 0;
    d[24 + 0] = 0x21; d[24 + 1] = 0x00; d[24 + 2] = 0xF0; d[24 + 3] = 0x0F; d[24 + 5] = 0;
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
        v.data[16 + 1] = 0x3F;       // モジュレータは鳴らさない
        v.data[24 + 5] = 0x01;       // キャリアは半波サイン
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

    return check::finish("render_test");
}
