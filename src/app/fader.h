#pragma once
// 出力全体にかける直線のフェードアウト。落ちきったら 0 のまま、reset まで戻らない。

#include <algorithm>
#include <cstdint>

namespace y8960 {

class Fader {
public:
    void reset() {
        gain_ = 1.0f;
        step_ = 0.0f;
    }

    // samples 個のサンプルで 1 から 0 まで落とす。0 なら即座に黙る。
    void start(uint32_t samples) {
        if (samples == 0) {
            gain_ = 0.0f;
            step_ = 0.0f;
            return;
        }
        step_ = gain_ / static_cast<float>(samples);
    }

    bool fading() const { return step_ > 0.0f; }
    bool silent() const { return gain_ <= 0.0f; }

    void apply(float* left, float* right, uint32_t samples) {
        if (gain_ >= 1.0f && step_ == 0.0f) return;
        for (uint32_t i = 0; i < samples; ++i) {
            left[i]  *= gain_;
            right[i] *= gain_;
            if (step_ > 0.0f) {
                gain_ = std::max(0.0f, gain_ - step_);
                if (gain_ == 0.0f) step_ = 0.0f;
            }
        }
    }

private:
    float gain_ = 1.0f;
    float step_ = 0.0f;
};

} // namespace y8960
