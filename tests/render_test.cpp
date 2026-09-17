// ブロックを作り、シーケンサとドライバを通して実際のエミュレータで鳴らす。
// エミュレータのライブラリが実行ファイルと同じフォルダに要る。

#include "check.h"
#include "chips.h"
#include "platform.h"
#include "player.h"
#include "sequencer.h"

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
        const double sample = rms(player2, kRate / 4);
        std::printf("adpcm silence=%.5f playing=%.5f\n", silence, sample);
        CHECK(silence < 0.001);
        CHECK(sample > 0.01);

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
        chips.setGain(Device::OPL2EX2, 0.0f);
        const double muted = rms(player2, kRate / 20);
        CHECK(muted < 0.001);
        CHECK(chips.takeLevel(Device::OPL2EX2) == 0.0f);
        chips.setGain(Device::OPL2EX2, 1.0f);
        const double back = rms(player2, kRate / 20);
        std::printf("muted=%.5f back=%.5f\n", muted, back);
        CHECK(back > 0.01);
    }

    return check::finish("render_test");
}
