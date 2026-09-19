#include "player.h"

#include <algorithm>

namespace y8960 {

Player::Player(Y8960Chips& chips, InterruptHandler& handler, TickRate rate, uint32_t sampleRate)
    : chips_(chips), handler_(handler), clock_(rate, sampleRate) {}

void Player::render(float* outL, float* outR, uint32_t samples) {
    while (samples > 0) {
        if (pos_ == bufL_.size()) {
            handler_.interrupt();
            const uint32_t n = clock_.nextInterval();
            bufL_.resize(n);
            bufR_.resize(n);
            chips_.render(bufL_.data(), bufR_.data(), n);
            pos_ = 0;
        }
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(samples, bufL_.size() - pos_));
        std::copy_n(bufL_.data() + pos_, n, outL);
        std::copy_n(bufR_.data() + pos_, n, outR);
        outL += n;
        outR += n;
        samples -= n;
        pos_ += n;
    }
}

} // namespace y8960
