#include "check.h"
#include "mute.h"

#include <string>
#include <vector>

using namespace y8960;

namespace {

bool parses(const std::string& text, MuteSpec& spec) {
    std::string err;
    return parseMuteSpec(text, spec, err);
}

bool parses(const std::string& text) {
    MuteSpec spec;
    return parses(text, spec);
}

uint16_t resolve(const std::vector<std::string>& texts, const SequenceBlock& block) {
    std::vector<MuteSpec> specs;
    for (const auto& t : texts) {
        MuteSpec spec;
        std::string err;
        if (parseMuteSpec(t, spec, err)) specs.push_back(spec);
    }
    return resolveMutes(specs, block);
}

void assign(SequenceBlock& b, int track, Device device, uint8_t channel) {
    TrackData& t = b.tracks[static_cast<size_t>(track)];
    t.assigned = true;
    t.device   = device;
    t.channel  = channel;
    t.events   = {0xFF};
}

} // namespace

int main() {
    // 読み取り
    {
        MuteSpec s;
        CHECK(parses("OPLLEX", s) && s.kind == MuteSpec::Kind::Chip && s.device == Device::OPLLEX && !s.except);
        CHECK(parses("opl2ex", s) && s.kind == MuteSpec::Kind::Chip && s.device == Device::OPL2EX);
        CHECK(parses("SCC,4", s) && s.kind == MuteSpec::Kind::Channel && s.channel == 4);
        CHECK(parses("OPLLEX,10", s) && s.kind == MuteSpec::Kind::Channel && s.channel == 10);
        CHECK(parses("T0", s) && s.kind == MuteSpec::Kind::Track && s.track == 0);
        CHECK(parses("T15", s) && s.kind == MuteSpec::Kind::Track && s.track == 15);
        CHECK(parses("A", s) && s.kind == MuteSpec::Kind::Track && s.track == 0);
        CHECK(parses("p", s) && s.kind == MuteSpec::Kind::Track && s.track == 15);
        CHECK(parses("!SSGS", s) && s.except && s.kind == MuteSpec::Kind::Chip);
        CHECK(parses("!SSGS,3", s) && s.except && s.kind == MuteSpec::Kind::Channel);
        CHECK(parses("!T3", s) && s.except && s.track == 3);
        CHECK(parses("!C", s) && s.except && s.track == 2);
        // OPM とデバイス 8・10・11
        CHECK(parses("opl3,24", s) && s.device == Device::OPL3 && s.channel == 24);
        CHECK(parses("OPM", s) && s.kind == MuteSpec::Kind::Chip && s.device == Device::OPM);
        CHECK(parses("OPNA,10", s) && s.device == Device::OPNA && s.channel == 10);
        CHECK(parses("OPNB,9", s) && s.device == Device::OPNB);
    }
    CHECK(!parses(""));
    CHECK(!parses("!"));
    CHECK(!parses("T16"));
    CHECK(!parses("T"));          // T は A-P の外
    CHECK(!parses("Q"));
    CHECK(!parses("OPLL"));
    // カートリッジから無くなったものと、2回路だったころの名前は受け付けない
    CHECK(!parses("DCSG1"));
    CHECK(!parses("OPLLEX1"));
    CHECK(!parses("OPL2EX2,0"));
    CHECK(!parses("SCC,5"));      // SCC は 0-4
    CHECK(!parses("SSGS,6"));
    CHECK(!parses("OPM,8"));      // OPM は 0-7
    CHECK(!parses("OPL3,25"));
    CHECK(!parses("OPNA,11"));
    CHECK(!parses("OPLLEX,9"));   // ADPCM は OPL2EX だけ
    CHECK(parses("OPL2EX,9"));
    CHECK(!parses("SSGS,x"));

    // 解決。トラック 0: OPLLEX ch0、1: OPLLEX ch3、2: SCC ch1、5: OPL2EX ch10
    SequenceBlock b;
    assign(b, 0, Device::OPLLEX, 0);
    assign(b, 1, Device::OPLLEX, 3);
    assign(b, 2, Device::SCC, 1);
    assign(b, 5, Device::OPL2EX, kChannelRhythm);

    CHECK(resolve({"OPLLEX"}, b) == 0x0003);
    CHECK(resolve({"OPLLEX,3"}, b) == 0x0002);
    CHECK(resolve({"T2"}, b) == 0x0004);
    CHECK(resolve({"F"}, b) == 0x0020);
    CHECK(resolve({"OPL2EX,10"}, b) == 0x0020);
    CHECK(resolve({"SCC", "T0"}, b) == 0x0005);                 // 複数は和集合

    // ! は「それ以外」。複数の ! はその和集合を残す
    CHECK(resolve({"!OPLLEX"}, b) == 0x0024);
    CHECK(resolve({"!OPLLEX,3"}, b) == 0x0025);
    CHECK(resolve({"!T2", "!A"}, b) == 0x0022);
    // ! で残したうえで、! の無い指定はさらに黙らせる
    CHECK(resolve({"!OPLLEX", "T1"}, b) == 0x0026);

    // シーケンスが使っていないものの指定は何もしない
    CHECK(resolve({"OPM"}, b) == 0);
    CHECK(resolve({"SCC,4"}, b) == 0);
    CHECK(resolve({"T9"}, b) == 0);
    // 使っていないものだけを残すと、全部が黙る
    CHECK(resolve({"!OPM"}, b) == 0x0027);

    return check::finish("mute_test");
}
