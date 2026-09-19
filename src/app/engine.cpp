#include "engine.h"

#include "platform.h"

#include <SDL3/SDL.h>

namespace y8960 {

namespace {

void SDLCALL feed(void* userdata, SDL_AudioStream* stream, int additional, int /*total*/) {
    if (additional <= 0) return;
    const int frames = additional / static_cast<int>(sizeof(float) * 2);
    if (frames <= 0) return;
    static thread_local std::vector<float> buffer;
    buffer.resize(static_cast<size_t>(frames) * 2);
    static_cast<PlaybackEngine*>(userdata)->render(buffer.data(), frames);
    SDL_PutAudioStreamData(stream, buffer.data(), frames * static_cast<int>(sizeof(float) * 2));
}

} // namespace

PlaybackEngine::PlaybackEngine() = default;

PlaybackEngine::~PlaybackEngine() {
    if (stream_) SDL_DestroyAudioStream(stream_);
}

bool PlaybackEngine::open(uint32_t sampleRate, TickRate rate, std::string& error) {
    sampleRate_ = sampleRate;
    rate_       = rate;
    if (!chips_.open(executableDirectory(), sampleRate_, error)) return false;
    devices_ = std::make_unique<DeviceSet>(chips_);
    devices_->resetAll();
    rebuildPlayer();

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        error = SDL_GetError();
        return false;
    }
    SDL_AudioSpec spec{};
    spec.format   = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq     = static_cast<int>(sampleRate_);
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, this);
    if (!stream_) {
        error = SDL_GetError();
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void PlaybackEngine::rebuildPlayer() {
    sequencer_ = std::make_unique<Sequencer>(*devices_, rate_);
    sequencer_->setActivity(&activity_);
    player_    = std::make_unique<Player>(chips_, *sequencer_, rate_, sampleRate_);
    activity_.allOff();
}

void PlaybackEngine::load(const SequenceBlock& block, const PcmFile* pcm) {
    std::lock_guard<std::mutex> lock(mutex_);
    block_     = block;
    directory_ = resolveAdpcmDirectory(block_, pcm);
    devices_->resetAll();
    devices_->setAdpcmDirectory(directory_.data());
    if (pcm) chips_.loadAdpcmMemory(pcm->dump);
    rebuildPlayer();
    sequencer_->load(0, block_);
    applyMutes();
    loaded_ = true;
}

void PlaybackEngine::play(uint8_t repeat) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_) return;
    sequencer_->stop(0);
    devices_->resetAll();
    sequencer_->load(0, block_);
    applyMutes();
    sequencer_->start(0, repeat);
}

void PlaybackEngine::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_) return;
    sequencer_->stop(0);
}

bool PlaybackEngine::playing() {
    std::lock_guard<std::mutex> lock(mutex_);
    return loaded_ && !sequencer_->finished();
}

void PlaybackEngine::setTickRate(TickRate rate) {
    std::lock_guard<std::mutex> lock(mutex_);
    rate_ = rate;
    if (!devices_) return;          // まだ開いていない
    rebuildPlayer();
    if (loaded_) {
        sequencer_->load(0, block_);
        applyMutes();
    }
}

void PlaybackEngine::setTrackMutes(uint16_t mask) {
    std::lock_guard<std::mutex> lock(mutex_);
    muteMask_ = mask;
    if (sequencer_) applyMutes();
}

void PlaybackEngine::applyMutes() {
    for (int i = 0; i < kTrackCount; ++i) {
        sequencer_->setTrackMute(0, i, (muteMask_ >> i) & 1);
    }
}

void PlaybackEngine::render(float* interleaved, int frames) {
    const size_t n = static_cast<size_t>(frames);
    if (left_.size() < n) {
        left_.resize(n);
        right_.resize(n);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        player_->render(left_.data(), right_.data(), static_cast<uint32_t>(n));
    }
    for (size_t i = 0; i < n; ++i) {
        interleaved[i * 2]     = left_[i];
        interleaved[i * 2 + 1] = right_[i];
    }
}

} // namespace y8960
