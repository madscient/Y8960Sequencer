#include "check.h"
#include "mixer.h"

#include <string>

using namespace y8960;

namespace {

bool parses(const std::string& text, MixerLevels& out) {
    std::string err;
    return parseMixer(text, out, err);
}

bool parses(const std::string& text) {
    MixerLevels m;
    return parses(text, m);
}

} // namespace

int main() {
    {
        MixerLevels m;
        CHECK(parses("100,0,64,128,1", m));
        CHECK(m.master == 100);
        CHECK(m.blocks[0] == 0 && m.blocks[1] == 64 && m.blocks[2] == 128 && m.blocks[3] == 1);
    }
    {
        // 既定はすべて 128。
        const MixerLevels m;
        CHECK(m.master == 128);
        for (uint8_t v : m.blocks) CHECK(v == 128);
    }
    CHECK(!parses(""));
    CHECK(!parses("128,128,128,128"));          // CALL MIXER と同じく5つとも要る
    CHECK(!parses("128,128,128,128,128,128"));
    CHECK(!parses("129,0,0,0,0"));
    CHECK(!parses("0,0,0,0,-1"));
    CHECK(!parses("0,,0,0,0"));
    CHECK(!parses("0,0,0,0,0,"));
    CHECK(!parses("0,0,0,0,1000"));
    CHECK(!parses(" 0,0,0,0,0"));

    // 線形の利得。128 が素通し、0 が無音。
    CHECK(mixerGain(128) == 1.0f);
    CHECK(mixerGain(0) == 0.0f);
    CHECK(mixerGain(64) == 0.5f);
    CHECK(mixerGain(32) == 0.25f);
    CHECK(mixerGain(200) == 1.0f);

    return check::finish("mixer_test");
}
