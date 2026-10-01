#include "softenv.h"

namespace y8960 {

namespace {

constexpr uint8_t kLevelMax = 15;
constexpr uint8_t kVMask    = 0x0F;

// チャンネルの並びは ENVMAP と同じ。tick() が書き直させる順もこれになる。
// OPNA・OPNB の SSG は ROM に無く、後ろに足した。
struct ChannelMap {
    Device  device;
    uint8_t channel;
};
constexpr ChannelMap kMap[] = {
    {Device::SSGS, 0},  {Device::SSGS, 1},  {Device::SSGS, 2},
    {Device::SSGS, 3},  {Device::SSGS, 4},  {Device::SSGS, 5},
    {Device::DCSG1, 0}, {Device::DCSG1, 1}, {Device::DCSG1, 2}, {Device::DCSG1, 3},
    {Device::DCSG2, 0}, {Device::DCSG2, 1}, {Device::DCSG2, 2}, {Device::DCSG2, 3},
    {Device::SCC, 0},   {Device::SCC, 1},   {Device::SCC, 2},   {Device::SCC, 3}, {Device::SCC, 4},
    {Device::OPNA, 6},  {Device::OPNA, 7},  {Device::OPNA, 8},
    {Device::OPNB, 6},  {Device::OPNB, 7},  {Device::OPNB, 8},
};

} // namespace

int SoftEnvelope::index(Device device, uint8_t ch) {
    static_assert(sizeof kMap / sizeof kMap[0] == kChannels, "kMap and kChannels disagree");
    switch (device) {
    case Device::SSGS:  return ch < 6 ? ch : -1;
    case Device::DCSG1: return ch < 4 ? 6 + ch : -1;
    case Device::DCSG2: return ch < 4 ? 10 + ch : -1;
    case Device::SCC:   return ch < 5 ? 14 + ch : -1;
    case Device::OPNA:  return (ch >= kOpnSsgFirst && ch < kOpnSsgFirst + 3) ? 19 + ch - kOpnSsgFirst : -1;
    case Device::OPNB:  return (ch >= kOpnSsgFirst && ch < kOpnSsgFirst + 3) ? 22 + ch - kOpnSsgFirst : -1;
    default:            return -1;
    }
}

void SoftEnvelope::attach(Device device, EnvelopeSink* sink) {
    sinks_[static_cast<size_t>(device)] = sink;
}

void SoftEnvelope::reset() {
    for (Channel& c : channels_) c = Channel{};
    moving_ = false;
    acc_ = 0;
}

// ROM は状態の4バイトだけを消し、パラメータは残す。次に選ばれるときに読み直すので
// 残っていても効かない。
void SoftEnvelope::resetDevice(Device device) {
    for (int i = 0; i < kChannels; ++i) {
        if (kMap[i].device != device) continue;
        Channel& c = channels_[static_cast<size_t>(i)];
        c.phase = Phase::None;
        c.level = 0;
        c.count = 0;
        c.vol   = 0;
    }
}

// キーと V は、エンベロープを持たないチャンネルのものも控える。ROM は持つチャンネルが
// 1つも無いあいだ控えを省くが、選ばれるときには必ずドライバの控えから読み直すので、
// 結果は同じ。
bool SoftEnvelope::keyOn(Device device, uint8_t ch) {
    const int i = index(device, ch);
    if (i < 0) return false;
    Channel& c = channels_[static_cast<size_t>(i)];
    c.vol = static_cast<uint8_t>(c.vol | kKey);
    if (c.phase == Phase::None) return false;
    attack(c);
    return true;
}

bool SoftEnvelope::keyOff(Device device, uint8_t ch) {
    const int i = index(device, ch);
    if (i < 0) return false;
    Channel& c = channels_[static_cast<size_t>(i)];
    c.vol = static_cast<uint8_t>(c.vol & ~kKey);
    if (c.phase == Phase::None) return false;
    release(c);
    return true;
}

bool SoftEnvelope::setV(Device device, uint8_t ch, uint8_t v) {
    const int i = index(device, ch);
    if (i < 0) return false;
    Channel& c = channels_[static_cast<size_t>(i)];
    c.vol = static_cast<uint8_t>((c.vol & kKey) | (v & kVMask));
    return c.phase != Phase::None;
}

void SoftEnvelope::none(Device device, uint8_t ch) {
    const int i = index(device, ch);
    if (i < 0) return;
    channels_[static_cast<size_t>(i)].phase = Phase::None;
}

// V は引き算で効く。V15 なら値がそのまま出る。
bool SoftEnvelope::output(Device device, uint8_t ch, uint8_t& level) const {
    const int i = index(device, ch);
    if (i < 0) return false;
    const Channel& c = channels_[static_cast<size_t>(i)];
    if (c.phase == Phase::None) return false;
    const int v = static_cast<int>(c.level) + (c.vol & kVMask) - kLevelMax;
    level = static_cast<uint8_t>(v < 0 ? 0 : v);
    return true;
}

// 無しから選ぶと、キーが押されていればその場でアタック、離れていれば次のキーオンを
// 待つ。別のエンベロープへ替えると、段と値と残りのコマを保って新しい速さで続く。
// 0 はその場で V の音量に戻す。
void SoftEnvelope::select(Device device, uint8_t ch, uint8_t number, const EnvelopeRecord& record) {
    const int i = index(device, ch);
    if (i < 0) return;
    Channel& c = channels_[static_cast<size_t>(i)];
    EnvelopeSink* sink = sinks_[static_cast<size_t>(device)];

    // 0 で外すとき、キーが離れていれば先にドライバのキーオフで消してから V の音量を
    // 書く（ENVSOFF）。逆だと、2つの書き込みのあいだ V の音量で鳴る。
    if (number == 0) {
        if (c.phase == Phase::None) return;
        c.phase = Phase::None;
        if (!(c.vol & kKey) && sink) sink->envReleased(ch);
        refresh(i);
        return;
    }
    if (number >= kEnvelopeCount) return;   // コンパイラは通さない

    // レコードはコマと変化の生のバイトを持つ（チャンク 04）。範囲は形式が書き手に
    // 課すもので、ROM も検査しない。SL だけは、15 を超えるとドライバへ渡すレベルが
    // 4 ビットを越えるので止める。
    c.ar = record.ar;
    c.dr = record.dr;
    c.sl = record.sl > kLevelMax ? kLevelMax : record.sl;
    c.rr = record.rr;

    if (c.phase == Phase::None) {
        if (sink) c.vol = static_cast<uint8_t>(sink->envSync(ch) & (kKey | kVMask));
        if (c.vol & kKey) {
            attack(c);
        } else {
            c.phase = Phase::Stop;
            c.level = 0;
        }
    }
    if (sink) sink->envChosen(ch);
    refresh(i);
}

// 1/60 秒を1コマとし、割り込みの周期によらず 60 コマ／秒で進む。増分は割り込み1回が
// 1コマの何倍かで、1コマ進む割り込みも、2コマや 0 コマの割り込みもある。
// ROM は何かが動いているあいだしかここを呼ばないので、端数もそのあいだしか進まない。
void SoftEnvelope::tick(TickRate rate) {
    if (!moving_) return;
    const TickIncrement inc = envelopeIncrement(rate);
    const uint32_t sum = static_cast<uint32_t>(acc_) + inc.frac;
    acc_ = static_cast<uint16_t>(sum & 0xFFFF);
    uint32_t steps = inc.whole + (sum >> 16);
    while (steps-- > 0) step();
}

uint8_t SoftEnvelope::rate(Channel& c, uint8_t packed) {
    c.count = static_cast<uint8_t>(packed >> 4);
    return static_cast<uint8_t>(packed & 0x0F);
}

void SoftEnvelope::attack(Channel& c) {
    c.phase = Phase::Attack;
    c.level = 0;
    rate(c, c.ar);
    moving_ = true;
    now(c);
}

// SL からではなく、いまの値から。リリース中でも止まっていても、キーオフのたびに
// コマを RR から数え直す（ゲートで切れた音符のあとの休符がそうなる）。
void SoftEnvelope::release(Channel& c) {
    c.phase = Phase::Release;
    rate(c, c.rr);
    moving_ = true;
    now(c);
}

// 勤労5号はキーのフレームで段を用意し、同じフレームで1コマ進める（ENVNOW）。
// チップには書かない ―― 呼ぶのは、このあと書くドライバ。
void SoftEnvelope::now(Channel& c) {
    if (--c.count == 0) stepLevel(c);
}

// 動いているものが無いのが普段の状態。印は1コマごとに下ろし、まだ動いているものが
// 立て直す。
void SoftEnvelope::step() {
    if (!moving_) return;
    moving_ = false;
    for (int i = 0; i < kChannels; ++i) {
        Channel& c = channels_[static_cast<size_t>(i)];
        if (c.phase != Phase::Attack && c.phase != Phase::Decay && c.phase != Phase::Release) continue;
        moving_ = true;
        if (--c.count != 0) continue;
        advance(c, i);
    }
}

void SoftEnvelope::advance(Channel& c, int index) {
    stepLevel(c);
    refresh(index);
}

// 勤労5号の順序。2つの境目も同じにしてある ―― アタックは 15 に着いたところで終わり、
// ディケイは SL と同じ値ならもう1段下げ、SL を下回る段で SL にそろえて終わる。
void SoftEnvelope::stepLevel(Channel& c) {
    if (c.phase == Phase::Attack) {
        const int next = c.level + rate(c, c.ar);
        if (next < kLevelMax) {
            c.level = static_cast<uint8_t>(next);
        } else {
            c.phase = Phase::Decay;
            rate(c, c.dr);
            c.level = kLevelMax;
        }
    } else if (c.phase == Phase::Decay) {
        const int next = c.level - rate(c, c.dr);
        if (next >= 0 && next >= c.sl) {
            c.level = static_cast<uint8_t>(next);
        } else {
            c.phase = Phase::Sustain;
            c.level = c.sl;
        }
    } else {
        const int next = c.level - rate(c, c.rr);
        if (next >= 0) {
            c.level = static_cast<uint8_t>(next);
        } else {
            c.phase = Phase::Stop;
            c.level = 0;
        }
    }
}

void SoftEnvelope::refresh(int index) {
    const ChannelMap& m = kMap[index];
    if (EnvelopeSink* sink = sinks_[static_cast<size_t>(m.device)]) sink->envRefresh(m.channel);
}

} // namespace y8960
