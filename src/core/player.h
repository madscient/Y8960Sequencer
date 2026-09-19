#pragma once
// 割り込みの時刻に合わせてシーケンサを進め、その間のサンプルをチップに作らせる。

#include "chips.h"
#include "timing.h"

#include <cstdint>
#include <vector>

namespace y8960 {

// 割り込み1回ぶんの処理。シーケンサがこれを実装する。
class InterruptHandler {
public:
    virtual ~InterruptHandler() = default;
    virtual void interrupt() = 0;
    // これ以上鳴らすものが無ければ true。
    virtual bool finished() const = 0;
};

class Player {
public:
    Player(Y8960Chips& chips, InterruptHandler& handler, TickRate rate, uint32_t sampleRate);

    // 割り込みの時刻では、その時刻より前のサンプルを作り終えてから割り込みを処理する。
    // レジスタ書き込みはそのあとに作るサンプルから効く。
    void render(float* outL, float* outR, uint32_t samples);

    bool finished() const { return handler_.finished(); }

private:
    Y8960Chips&       chips_;
    InterruptHandler& handler_;
    InterruptClock    clock_;
    // 割り込みの間隔1回ぶんを、呼び出し側が求める量によらず一度に作ってためる。
    // エミュレータは同じ tick の KEY OFF → KEY ON を見せるために、1回の生成の中で
    // 間に数 ms を作る。生成が割り込みの直後で細かく切れると、その分が作れず
    // KEY OFF が消える。リアルタイム再生では求める量が負荷で揺れる。
    std::vector<float> bufL_, bufR_;
    size_t            pos_ = 0;
};

} // namespace y8960
