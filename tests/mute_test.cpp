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
        CHECK(parses("OPLLEX1", s) && s.kind == MuteSpec::Kind::Chip && s.device == Device::OPLLEX1 && !s.except);
        CHECK(parses("opl2ex2", s) && s.kind == MuteSpec::Kind::Chip && s.device == Device::OPL2EX2);
        CHECK(parses("SCC,4", s) && s.kind == MuteSpec::Kind::Channel && s.channel == 4);
        CHECK(parses("OPLLEX2,10", s) && s.kind == MuteSpec::Kind::Channel && s.channel == 10);
        CHECK(parses("T0", s) && s.kind == MuteSpec::Kind::Track && s.track == 0);
        CHECK(parses("T15", s) && s.kind == MuteSpec::Kind::Track && s.track == 15);
        CHECK(parses("A", s) && s.kind == MuteSpec::Kind::Track && s.track == 0);
        CHECK(parses("p", s) && s.kind == MuteSpec::Kind::Track && s.track == 15);
        CHECK(parses("!SSGS", s) && s.except && s.kind == MuteSpec::Kind::Chip);
        CHECK(parses("!DCSG1,3", s) && s.except && s.kind == MuteSpec::Kind::Channel);
        CHECK(parses("!T3", s) && s.except && s.track == 3);
        CHECK(parses("!C", s) && s.except && s.track == 2);
    }
    CHECK(!parses(""));
    CHECK(!parses("!"));
    CHECK(!parses("T16"));
    CHECK(!parses("T"));          // T は A-P の外
    CHECK(!parses("Q"));
    CHECK(!parses("OPLL"));
    CHECK(!parses("SCC,5"));      // SCC は 0-4
    CHECK(!parses("SSGS,6"));
    CHECK(!parses("OPLLEX1,9"));  // ADPCM は OPL2EX だけ
    CHECK(parses("OPL2EX1,9"));
    CHECK(!parses("DCSG1,x"));

    // 解決。トラック 0: OPLLEX1 ch0、1: OPLLEX1 ch3、2: SCC ch1、5: OPL2EX1 ch10
    SequenceBlock b;
    assign(b, 0, Device::OPLLEX1, 0);
    assign(b, 1, Device::OPLLEX1, 3);
    assign(b, 2, Device::SCC, 1);
    assign(b, 5, Device::OPL2EX1, kChannelRhythm);

    CHECK(resolve({"OPLLEX1"}, b) == 0x0003);
    CHECK(resolve({"OPLLEX1,3"}, b) == 0x0002);
    CHECK(resolve({"T2"}, b) == 0x0004);
    CHECK(resolve({"F"}, b) == 0x0020);
    CHECK(resolve({"OPL2EX1,10"}, b) == 0x0020);
    CHECK(resolve({"SCC", "T0"}, b) == 0x0005);                 // 複数は和集合

    // ! は「それ以外」。複数の ! はその和集合を残す
    CHECK(resolve({"!OPLLEX1"}, b) == 0x0024);
    CHECK(resolve({"!OPLLEX1,3"}, b) == 0x0025);
    CHECK(resolve({"!T2", "!A"}, b) == 0x0022);
    // ! で残したうえで、! の無い指定はさらに黙らせる
    CHECK(resolve({"!OPLLEX1", "T1"}, b) == 0x0026);

    // シーケンスが使っていないものの指定は何もしない
    CHECK(resolve({"DCSG2"}, b) == 0);
    CHECK(resolve({"SCC,4"}, b) == 0);
    CHECK(resolve({"T9"}, b) == 0);
    // 使っていないものだけを残すと、全部が黙る
    CHECK(resolve({"!DCSG2"}, b) == 0x0027);

    return check::finish("mute_test");
}
