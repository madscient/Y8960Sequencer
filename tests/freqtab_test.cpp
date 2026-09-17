// 音律表が ROM のものと同じかを見る。
// 環境変数 Y8960_FREQDAT に Y8960BasicExtension の src/freq/freqdat.asm を渡すと
// 2304 項目すべてを突き合わせる。渡さないときは半音の 12 点だけを見る。

#include "check.h"
#include "freqtab.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace y8960;

namespace {

// freqtab.py が「移ってはならない」として持っている 12 点。
const uint16_t kWasOpl[12] = {2759, 2923, 3097, 3281, 3476, 3683, 3902,
                              2067, 2190, 2320, 2458, 2604};
const uint16_t kWasDiv[12] = {54728, 51656, 48757, 46020, 43437, 40999,
                              38698, 36526, 34476, 32541, 30715, 28991};
const uint16_t kWasPcm[12] = {0, 15, 31, 48, 67, 86, 106, 128, 150, 175, 200, 227};

bool readFreqDat(const char* path, std::vector<uint16_t> out[3]) {
    std::ifstream f(path);
    if (!f) return false;
    int family = -1;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("FRQ_OPL", 0) == 0) { family = 0; continue; }
        if (line.rfind("FRQ_DIV", 0) == 0) { family = 1; continue; }
        if (line.rfind("FRQ_PCM", 0) == 0) { family = 2; continue; }
        const size_t dw = line.find("dw");
        if (family < 0 || dw == std::string::npos) continue;
        std::istringstream words(line.substr(dw + 2));
        std::string word;
        while (std::getline(words, word, ',')) {
            out[family].push_back(static_cast<uint16_t>(std::stoul(word)));
        }
    }
    return true;
}

} // namespace

int main() {
    for (int s = 0; s < 12; ++s) {
        const int i = s * kFreqSemitone;
        CHECK((freqEntry(FreqFamily::Opl, i) & 0x0FFF) == kWasOpl[s]);
        CHECK(freqEntry(FreqFamily::Div, i) == kWasDiv[s]);
        CHECK(((freqEntry(FreqFamily::Pcm, i) + 0x80) >> 8) == kWasPcm[s]);
    }
    // C から F+ はブロックが1つ下、G から B はオクターブのブロック。
    CHECK((freqEntry(FreqFamily::Opl, 0) & kFnumAdj) != 0);
    CHECK((freqEntry(FreqFamily::Opl, 7 * kFreqSemitone) & kFnumAdj) == 0);

    // セントから 1/64 半音へ
    CHECK(centToSteps(100) == 64);
    CHECK(centToSteps(1200) == 768);
    CHECK(centToSteps(-1200) == -768);
    CHECK(centToSteps(0) == 0);
    CHECK(clampBend(8 * kFreqSteps + 1) == 8 * kFreqSteps);
    CHECK(clampBend(-8 * kFreqSteps - 1) == -8 * kFreqSteps);

    if (const char* path = std::getenv("Y8960_FREQDAT")) {
        std::vector<uint16_t> rom[3];
        if (!readFreqDat(path, rom)) {
            std::fprintf(stderr, "%s: 読めません\n", path);
            return 1;
        }
        const FreqFamily fam[3] = {FreqFamily::Opl, FreqFamily::Div, FreqFamily::Pcm};
        const char* name[3] = {"FRQ_OPL", "FRQ_DIV", "FRQ_PCM"};
        for (int k = 0; k < 3; ++k) {
            CHECK(rom[k].size() == static_cast<size_t>(kFreqSteps));
            int bad = 0;
            for (size_t i = 0; i < rom[k].size() && i < kFreqSteps; ++i) {
                if (freqEntry(fam[k], static_cast<int>(i)) != rom[k][i]) {
                    if (bad < 5) {
                        std::fprintf(stderr, "%s[%zu]: %u なのに ROM は %u\n", name[k], i,
                                     freqEntry(fam[k], static_cast<int>(i)), rom[k][i]);
                    }
                    ++bad;
                }
            }
            std::printf("%s: %d 件ちがい\n", name[k], bad);
            CHECK(bad == 0);
        }
    } else {
        std::printf("Y8960_FREQDAT が無いので、半音の12点だけを見た\n");
    }

    return check::finish("freqtab_test");
}
