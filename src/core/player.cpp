#include "player.h"

#include <algorithm>

namespace y8960 {

Player::Player(Y8960Chips& chips, InterruptHandler& handler, TickRate rate, uint32_t sampleRate)
    : chips_(chips), handler_(handler), clock_(rate, sampleRate) {}

void Player::render(float* outL, float* outR, uint32_t samples) {
    while (samples > 0) {
        if (untilInterrupt_ == 0) {
            handler_.interrupt();
            untilInterrupt_ = clock_.nextInterval();
            continue;
        }
        const uint32_t n = std::min(samples, untilInterrupt_);
        chips_.render(outL, outR, n);
        outL += n;
        outR += n;
        samples -= n;
        untilInterrupt_ -= n;
    }
}

} // namespace y8960
