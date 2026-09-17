// 8ブロックそれぞれに、レジスタを直接書いて 440Hz 付近の音を出させ、鳴ることを見る。
// エミュレータのライブラリが実行ファイルと同じフォルダに要る。

#include "chips.h"
#include "check.h"
#include "platform.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace y8960;

namespace {

constexpr uint32_t kRate = 48000;

double rms(Y8960Chips& chips, uint32_t samples) {
    std::vector<float> l(samples), r(samples);
    chips.render(l.data(), r.data(), samples);
    double sum = 0;
    for (uint32_t i = 0; i < samples; ++i) sum += 0.5 * (double(l[i]) * l[i] + double(r[i]) * r[i]);
    return std::sqrt(sum / samples);
}

void opllNote(Y8960Chips& c, Device d, bool on) {
    c.write(d, 0x30, 0x10);                  // 楽器 1、減衰 0
    c.write(d, 0x10, 0x22);                  // F-Number 290、ブロック 4
    c.write(d, 0x20, on ? 0x19 : 0x09);
}

void opl2Note(Y8960Chips& c, Device d, bool on) {
    c.write(d, 0x20, 0x21); c.write(d, 0x23, 0x21);   // 持続型。bit5 が無いと即座にリリースに入る
    c.write(d, 0x40, 0x3F); c.write(d, 0x43, 0x00);
    c.write(d, 0x60, 0xF0); c.write(d, 0x63, 0xF0);
    c.write(d, 0x80, 0x0F); c.write(d, 0x83, 0x0F);
    c.write(d, 0xC0, 0x01);
    c.write(d, 0xA0, 0x44);                  // F-Number 580、ブロック 4
    c.write(d, 0xB0, on ? 0x32 : 0x12);
}

void dcsgNote(Y8960Chips& c, Device d, bool on) {
    c.write(d, 0, 0x8E);                     // 分周値 254
    c.write(d, 0, 0x0F);
    c.write(d, 0, on ? 0x90 : 0x9F);
}

void sccNote(Y8960Chips& c, bool on) {
    for (uint8_t i = 0; i < 32; ++i) c.write(Device::SCC, i, i < 16 ? 0x7F : 0x80);
    c.write(Device::SCC, 0x80, 0xFD);        // N = 253
    c.write(Device::SCC, 0x81, 0x00);
    c.write(Device::SCC, 0x8A, 0x0F);
    c.write(Device::SCC, 0x8F, on ? 0x01 : 0x00);
}

void ssgsNote(Y8960Chips& c, bool on) {
    c.write(Device::SSGS, 0x00, 0xFE);       // TP = 254
    c.write(Device::SSGS, 0x01, 0x00);
    c.write(Device::SSGS, 0x07, 0x3E);
    c.write(Device::SSGS, 0x10, 0x08);
    c.write(Device::SSGS, 0x08, on ? 0x0F : 0x00);
}

} // namespace

int main() {
    Y8960Chips chips;
    std::string err;
    if (!chips.open(executableDirectory(), kRate, err)) {
        std::fprintf(stderr, "chips_test: %s\n", err.c_str());
        return 1;
    }

    const double silent = rms(chips, kRate / 5);
    std::printf("silence  rms=%.5f\n", silent);
    CHECK(silent < 0.001);

    using Note = std::function<void(bool)>;
    const std::vector<std::pair<const char*, Note>> notes = {
        {"SSGS",    [&](bool on) { ssgsNote(chips, on); }},
        {"OPLLEX1", [&](bool on) { opllNote(chips, Device::OPLLEX1, on); }},
        {"OPLLEX2", [&](bool on) { opllNote(chips, Device::OPLLEX2, on); }},
        {"OPL2EX1", [&](bool on) { opl2Note(chips, Device::OPL2EX1, on); }},
        {"OPL2EX2", [&](bool on) { opl2Note(chips, Device::OPL2EX2, on); }},
        {"DCSG1",   [&](bool on) { dcsgNote(chips, Device::DCSG1, on); }},
        {"DCSG2",   [&](bool on) { dcsgNote(chips, Device::DCSG2, on); }},
        {"SCC",     [&](bool on) { sccNote(chips, on); }},
    };
    for (const auto& n : notes) {
        n.second(true);
        const double on = rms(chips, kRate / 5);
        n.second(false);
        rms(chips, kRate / 2);               // 余韻を出し切る
        const double off = rms(chips, kRate / 10);
        std::printf("%-8s on rms=%.5f  off rms=%.5f\n", n.first, on, off);
        CHECK(on > 0.01);
        CHECK(off < on * 0.1);
    }

    return check::finish("chips_test");
}
