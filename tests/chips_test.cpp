// 12 のデバイスそれぞれに、レジスタを直接書いて 440Hz 付近の音を出させ、鳴ることを見る。
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

// OPL3。2組目のチャンネルと 4OP は NEW（105h）が無いと効かない。
void opl3Note(Y8960Chips& c, uint8_t port, bool fourOp, bool on) {
    const Device d = Device::OPL3;
    c.write(d, 0x05, 0x01, 1);
    c.write(d, 0x04, fourOp ? 0x01 : 0x00, 1);
    const uint8_t slots[] = {0x00, 0x03, 0x08, 0x0B};   // ch0 と ch3 のオペレータ
    for (int i = 0; i < (fourOp ? 4 : 2); ++i) {
        const uint8_t s = slots[i];
        c.write(d, static_cast<uint8_t>(0x20 + s), 0x21, port);
        c.write(d, static_cast<uint8_t>(0x40 + s), (i == (fourOp ? 3 : 1)) ? 0x00 : 0x3F, port);
        c.write(d, static_cast<uint8_t>(0x60 + s), 0xF0, port);
        c.write(d, static_cast<uint8_t>(0x80 + s), 0x0F, port);
    }
    c.write(d, 0xC0, 0x30, port);
    c.write(d, 0xC3, 0x30, port);
    c.write(d, 0xA0, 0x44, port);            // F-Number 580、ブロック 4
    c.write(d, 0xB0, on ? 0x32 : 0x12, port);
    // ymfm は 104h を次のクロックで組に反映するので、後ろ側もキーオンしないと
    // OP3・OP4 がキーオンされない（dev_opl3.cpp の key）。
    if (fourOp) c.write(d, 0xB3, on ? 0x20 : 0x00, port);
}

// OPM。AL 7 で C2 だけを鳴らす。
void opmNote(Y8960Chips& c, bool on) {
    const Device d = Device::OPM;
    c.write(d, 0x20, 0xC7);
    for (uint8_t op = 0; op < 4; ++op) {
        const uint8_t o = static_cast<uint8_t>(op * 8);
        c.write(d, static_cast<uint8_t>(0x40 + o), 0x01);
        c.write(d, static_cast<uint8_t>(0x60 + o), op == 3 ? 0x00 : 0x7F);
        c.write(d, static_cast<uint8_t>(0x80 + o), 0x1F);
        c.write(d, static_cast<uint8_t>(0xE0 + o), 0x0F);
    }
    c.write(d, 0x28, 0x4A);                  // O4 A
    c.write(d, 0x08, on ? 0x78 : 0x00);
}

// OPNA と OPNB の FM。port 1 は 28h の下位3ビットが 4 から。
void opnNote(Y8960Chips& c, Device d, uint8_t port, bool on) {
    if (d == Device::OPNA) c.write(d, 0x29, 0x80);
    const uint8_t ch = 1;
    c.write(d, static_cast<uint8_t>(0xB0 + ch), 0x07, port);
    c.write(d, static_cast<uint8_t>(0xB4 + ch), 0xC0, port);
    for (uint8_t op = 0; op < 4; ++op) {
        const uint8_t o = static_cast<uint8_t>(op * 4 + ch);
        c.write(d, static_cast<uint8_t>(0x30 + o), 0x01, port);
        c.write(d, static_cast<uint8_t>(0x40 + o), op == 3 ? 0x00 : 0x7F, port);
        c.write(d, static_cast<uint8_t>(0x50 + o), 0x1F, port);
        c.write(d, static_cast<uint8_t>(0x80 + o), 0x0F, port);
    }
    c.write(d, static_cast<uint8_t>(0xA4 + ch), 0x24, port);   // ブロック 4
    c.write(d, static_cast<uint8_t>(0xA0 + ch), 0x10, port);
    c.write(d, 0x28, static_cast<uint8_t>((on ? 0xF0 : 0x00) | (port << 2) | ch));
}

// OPNA と OPNB の SSG。
void opnSsgNote(Y8960Chips& c, Device d, bool on) {
    c.write(d, 0x00, 0x1C);
    c.write(d, 0x01, 0x01);
    c.write(d, 0x07, 0x3E);
    c.write(d, 0x08, on ? 0x0F : 0x00);
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
        {"OPL3 p0", [&](bool on) { opl3Note(chips, 0, false, on); }},
        {"OPL3 p1", [&](bool on) { opl3Note(chips, 1, false, on); }},
        {"OPL3 4op", [&](bool on) { opl3Note(chips, 0, true, on); }},
        {"OPM",     [&](bool on) { opmNote(chips, on); }},
        {"OPNA p0", [&](bool on) { opnNote(chips, Device::OPNA, 0, on); }},
        {"OPNA p1", [&](bool on) { opnNote(chips, Device::OPNA, 1, on); }},
        {"OPNA SSG", [&](bool on) { opnSsgNote(chips, Device::OPNA, on); }},
        {"OPNB p0", [&](bool on) { opnNote(chips, Device::OPNB, 0, on); }},
        {"OPNB p1", [&](bool on) { opnNote(chips, Device::OPNB, 1, on); }},
        {"OPNB SSG", [&](bool on) { opnSsgNote(chips, Device::OPNB, on); }},
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
