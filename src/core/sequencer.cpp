#include "sequencer.h"

#include "freqtab.h"

#include <algorithm>

namespace y8960 {

namespace {

constexpr uint16_t kPortaHere = 0x8000;   // いま鳴っている高さから
constexpr uint16_t kPortaTie  = 0x8001;   // & ―― 閉じる距離が無い

constexpr uint8_t kEvRest     = 0x0C;
constexpr uint8_t kEvWait     = 0x0E;
constexpr uint8_t kEvNoteDown = 0x0F;   // c- ―― 1つ下のオクターブの b
constexpr uint8_t kEvNoteUp   = 0x10;   // b+ ―― 1つ上のオクターブの c
constexpr uint8_t kEvOctUp    = 0x40;
constexpr uint8_t kEvOctDown  = 0x41;
constexpr uint8_t kEvLoopTop  = 0x42;
constexpr uint8_t kEvDaCapo   = 0x43;
constexpr uint8_t kEvTie      = 0x45;
constexpr uint8_t kEvOctave   = 0x80;
constexpr uint8_t kEvVolume   = 0x81;
constexpr uint8_t kEvVoice    = 0x82;
constexpr uint8_t kEvQuantize = 0x83;
constexpr uint8_t kEvTempo    = 0x84;
constexpr uint8_t kEvSeqVoice = 0x85;
constexpr uint8_t kEvRhyAccent = 0xA8;
constexpr uint8_t kEvRhyVol    = 0xA9;
constexpr uint8_t kEvRhyAccVol = 0xAA;
constexpr uint8_t kEvSsgShape  = 0xB0;
constexpr uint8_t kEvSsgPan    = 0xB1;
constexpr uint8_t kEvNoteAbs   = 0xC0;
constexpr uint8_t kEvRhythm    = 0xC8;
constexpr uint8_t kEvBend      = 0xD0;
constexpr uint8_t kEvBendRel   = 0xD1;
constexpr uint8_t kEvPorta     = 0xD2;
constexpr uint8_t kEvBlockEnd  = 0xD3;
constexpr uint8_t kEvFine      = 0xD4;
constexpr uint8_t kEvDalSegno  = 0xD5;
constexpr uint8_t kEvSsgPeriod = 0xDC;
constexpr uint8_t kEvRegWrite  = 0xE0;
constexpr uint8_t kEvBlock     = 0xE1;
constexpr uint8_t kEvLoopEnd   = 0xE2;
constexpr uint8_t kEvToCoda    = 0xF0;
constexpr uint8_t kEvEnd       = 0xFF;

constexpr uint8_t kLengthLong = 0x80;   // 音長の1バイト目の bit7

// 1 tick に読む、時間を取らないイベントの上限（bytecode.md「上限」）。
constexpr int kNoTimeLimit = 256;

uint8_t mulVol(uint8_t level, uint8_t scale) {
    return static_cast<uint8_t>((static_cast<uint16_t>(level) * (scale + 1)) >> 7);
}

} // namespace

Sequencer::Sequencer(DeviceSet& devices, TickRate rate) : devices_(devices), rate_(rate) {}

void Sequencer::load(int sequence, const SequenceBlock& block) {
    Sequence& s = sequences_[static_cast<size_t>(sequence)];
    s = Sequence{};
    s.block = &block;
    for (int i = 0; i < kTrackCount; ++i) {
        const TrackData& src = block.tracks[static_cast<size_t>(i)];
        Track& t = s.tracks[static_cast<size_t>(i)];
        t.assigned = src.assigned;
        t.device   = src.device;
        t.channel  = src.channel;
        t.events   = src.assigned ? &src.events : nullptr;
    }
}

void Sequencer::start(int sequence, uint8_t repeat) {
    Sequence& s = sequences_[static_cast<size_t>(sequence)];
    if (!s.block) return;
    const uint16_t mask = buildMask(s);
    if (mask == 0) return;

    s.repeat  = repeat;
    s.active  = mask;
    s.current = s.volume;

    // リズムモードは、演奏を始めるシーケンスが決める（ROM の DEV_RHYSEQ）。
    for (int d = 0; d < kDeviceCount; ++d) {
        bool used = false;
        for (const Track& t : s.tracks) {
            if (t.assigned && static_cast<int>(t.device) == d) used = true;
        }
        if (!used) continue;
        devices_[static_cast<Device>(d)].setRhythmMode(s.block->rhythmMode[static_cast<size_t>(d)],
                                                       &s.block->voices[kVoiceSetSize]);
    }

    // リズムの V と @A は、リズムモードに入るときに既定へ戻る（ROM の RHYDEF）。
    // 繰り返しの頭では戻らない。
    for (Track& t : s.tracks) {
        t.rhythmLevel = 8;
        t.rhythmAccentLevel = 15;
    }
    if (activity_) activity_->allOff();

    markClear(s);
    rewind(s);
    s.state = State::Playing;
    tracks(s);          // tick 0 は次の割り込みではなく、いま
}

void Sequencer::stop(int sequence) {
    Sequence& s = sequences_[static_cast<size_t>(sequence)];
    if (s.state == State::Idle) return;
    s.state = State::Idle;
    seqOff(s);
    if (activity_) activity_->allOff();
}

void Sequencer::setTrackMute(int sequence, int track, bool mute) {
    Sequence& s = sequences_[static_cast<size_t>(sequence)];
    Track& t = s.tracks[static_cast<size_t>(track)];
    if (t.muted == mute) return;
    t.muted = mute;
    if (!t.assigned) return;
    // ROM のミュートと同じく、鳴っている音はキーオフせず音量 0 で消す。
    volumeOut(s, t);
    if (!mute || !activity_) return;
    if (t.channel == kChannelRhythm) {
        for (uint8_t i = 0; i < 5; ++i) activity_->noteOff(t.device, static_cast<uint8_t>(kRhythmSlotFirst + i));
    } else {
        activity_->noteOff(t.device, t.channel);
    }
}

bool Sequencer::finished() const {
    for (const Sequence& s : sequences_) {
        if (s.state != State::Idle) return false;
    }
    return true;
}

void Sequencer::interrupt() {
    // ROM と同じく番号の大きいほうから。
    for (int i = kSequenceCount - 1; i >= 0; --i) step(sequences_[static_cast<size_t>(i)]);
}

void Sequencer::step(Sequence& s) {
    if (s.state != State::Playing) return;

    // accumulator が割り込みを tick に変える。増分は整数部と小数部を持つので、
    // 割り込みより速いテンポでは 1 回の割り込みに複数の tick が出る。
    const uint32_t sum = static_cast<uint32_t>(s.acc) + s.incFrac;
    s.acc = static_cast<uint16_t>(sum & 0xFFFF);
    uint32_t ticks = s.incWhole + (sum >> 16);
    while (ticks-- > 0) {
        tracks(s);
        if (s.state == State::Idle) return;
    }
}

uint16_t Sequencer::buildMask(const Sequence& s) const {
    uint16_t mask = 0;
    for (int i = 0; i < kTrackCount; ++i) {
        const Track& t = s.tracks[static_cast<size_t>(i)];
        if (t.assigned && t.events && !t.events->empty()) mask = static_cast<uint16_t>(mask | (1u << i));
    }
    return mask;
}

// 周が終わったら、次の周は同じ tick のうちに始まる（seq.asm の PASSAGAIN）。
// ただし終わった周がその最初の tick のうちに終わっていた ―― 時間を取らなかった ―― なら
// 次の tick を待つ。そうしないと、時間を取らない周が1つの tick の中で回り続ける。
// SQ_FIRST にあたる s.first は巻き戻しで立ち、周が最初の tick を越えて初めて下りる。
void Sequencer::tracks(Sequence& s) {
    for (;;) {
        if (s.active != 0) {
            uint16_t walking = s.active;
            for (int i = 0; i < kTrackCount && walking != 0; ++i, walking = static_cast<uint16_t>(walking >> 1)) {
                if (!(walking & 1)) continue;
                trackStep(s, s.tracks[static_cast<size_t>(i)], i);
                // (DC) や (FINE) はマスクを空にする。上のトラックも進めない。
                if (s.active == 0) break;
            }
            if (s.active != 0) {
                s.first = false;      // 周が最初の tick を越えた
                return;
            }
        }
        const bool wasFirst = s.first;
        passEnd(s);                   // 次があれば巻き戻し、s.first を立てる
        if (s.state != State::Playing) return;
        if (wasFirst) return;
    }
}

void Sequencer::passEnd(Sequence& s) {
    if (s.repeat != 0) {
        --s.repeat;
        if (s.repeat == 0) {
            s.state = State::Idle;
            seqOff(s);
            return;
        }
    }
    s.active = buildMask(s);
    rewind(s);                 // SQ_FIRST を立てる
}

void Sequencer::rewind(Sequence& s) {
    for (Track& t : s.tracks) {
        if (!t.assigned) continue;
        t.ptr   = 0;
        t.wait  = 0;
        t.gate  = 0;
        t.loopSp = 0;
        t.note  = kNoNote;
        t.bend  = 0;
        t.porta = 0;
        t.glide = 0;
        t.oct   = kDefaultOct;
        t.quant = kQuantMax;
        t.vol   = kDefaultVol;
        t.voice = kDefaultVoice;
        // 既定の音色は番号で選ぶ。レコードで表される音色のドライバはこれを捨て、
        // ブロックが持つレコード（`85`）を待つ（doc/rom-feedback.md の B1）。
        device(t).setVoice(t.channel, kDefaultVoice);
        volumeOut(s, t);
    }
    s.first = true;
    s.acc   = 0;
    const TickIncrement inc = tickIncrement(kDefaultTempo, rate_);
    s.incWhole = inc.whole;
    s.incFrac  = inc.frac;
}

void Sequencer::markClear(Sequence& s) {
    for (Track& t : s.tracks) t.mark.fill(0);
}

void Sequencer::seqOff(Sequence& s) {
    for (Track& t : s.tracks) {
        if (t.assigned) keyOff(t);
    }
}

void Sequencer::trackStep(Sequence& s, Track& t, int index) {
    glideStep(t);

    if (t.gate != 0) {
        if (--t.gate == 0) keyOff(t);
    }
    if (t.wait != 0) {
        if (--t.wait != 0) return;
    }
    fetch(s, t, index);
}

bool Sequencer::fetch(Sequence& s, Track& t, int index) {
    for (int n = 0; n < kNoTimeLimit; ++n) {
        const uint8_t op = readByte(t);
        if (event(s, t, index, op)) return true;
    }
    // 時間を取らないイベントが続きすぎた。割り込みの中で回り続けるのを止める。
    endTrack(s, t, index);
    return true;
}

uint8_t Sequencer::readByte(Track& t) {
    if (!t.events || t.ptr >= t.events->size()) return kEvEnd;
    return (*t.events)[t.ptr++];
}

uint16_t Sequencer::readLength(Track& t) {
    const uint8_t first = readByte(t);
    if (!(first & kLengthLong)) return first;
    const uint8_t low = readByte(t);
    return static_cast<uint16_t>(((first & ~kLengthLong) << 8) | low);
}

uint16_t Sequencer::readWord(Track& t) {
    const uint8_t low  = readByte(t);
    const uint8_t high = readByte(t);
    return static_cast<uint16_t>(low | (high << 8));
}

bool Sequencer::event(Sequence& s, Track& t, int index, uint8_t op) {
    if (op == kEvEnd) {
        endTrack(s, t, index);
        return true;
    }

    if (op < 0x40) {                          // 音長が続く
        t.wait = readLength(t);
        if (op == kEvRest) {
            t.porta = 0;                      // `~` が届くのは音符であって休符ではない
            keyOff(t);
            t.gate = 0;
            return t.wait != 0;
        }
        // c- と b+ はその1音だけオクターブを出る。走行状態のオクターブは動かさない。
        int semitone = op;
        if (op == kEvNoteDown) semitone = -1;
        else if (op == kEvNoteUp) semitone = 12;
        else if (op == kEvWait || op >= 12) { // まだ誰も鳴らさないもの
            t.gate = 0;
            return t.wait != 0;
        }
        uint8_t number = static_cast<uint8_t>(t.oct * 12 + semitone);
        // O0 の c- は FFh になり、鳴っている音が無い印と区別できない。ROM と同じく
        // 0 に丸める（ループの中の < で走行状態が 0 まで下がったときだけ起きる）。
        if (number == kNoNote) number = 0;
        return note(s, t, index, number);
    }

    if (op < 0x80) {                          // 何も続かない
        switch (op) {
        case kEvOctUp:   ++t.oct; break;
        case kEvOctDown: --t.oct; break;
        case kEvTie:     t.porta = kPortaTie; break;
        case kEvLoopTop:
            if (t.loopSp < kLoopDepth) t.loop[t.loopSp++] = 1;
            break;
        case kEvDaCapo:  return daCapo(s, t);
        default: break;                       // コーダは場所であって何もしない
        }
        return false;
    }

    if (op < 0xC0) {                          // 1バイト
        const uint8_t arg = readByte(t);
        switch (op) {
        case kEvOctave:   t.oct = arg; break;
        case kEvVolume:   t.vol = arg; volumeOut(s, t); break;
        case kEvQuantize: t.quant = arg; break;
        case kEvVoice:    t.voice = arg; device(t).setVoice(t.channel, arg); break;
        case kEvSeqVoice: {
            t.voice = arg;
            if (s.block && arg < kVoiceSlots) {
                const VoiceRecord& rec = s.block->voices[arg];
                if (rec.kind != RecordKind::None) device(t).seqVoice(t.channel, arg, rec);
            }
            break;
        }
        case kEvTempo: {
            const TickIncrement inc = tickIncrement(arg, rate_);
            s.incWhole = inc.whole;
            s.incFrac  = inc.frac;
            break;
        }
        case kEvRhyAccent: t.rhythmAccent = arg; break;
        case kEvRhyVol:    t.rhythmLevel = arg;       device(t).rhythmVolume(false, arg); break;
        case kEvRhyAccVol: t.rhythmAccentLevel = arg; device(t).rhythmVolume(true, arg); break;
        case kEvSsgShape:  device(t).ssgEnv(SsgEnv::Shape, t.channel, arg); break;
        case kEvSsgPan:    device(t).ssgEnv(SsgEnv::Pan, t.channel, arg); break;
        default: break;
        }
        return false;
    }

    if (op < 0xD0) {                          // 1バイト、そのあと音長
        if (op == kEvRhythm) {
            const uint8_t instruments = readByte(t);
            t.wait = readLength(t);
            t.gate = 0;
            if (!s.mute && !t.muted) {
                device(t).rhythmStrike(instruments, t.rhythmAccent);
                reportStrike(s, t, instruments);
            }
            return t.wait != 0;
        }
        const uint8_t number = readByte(t);
        t.wait = readLength(t);
        if (op == kEvNoteAbs) return note(s, t, index, number);
        return t.wait != 0;
    }

    if (op < 0xE0) {                          // 2バイト
        const uint16_t arg = readWord(t);
        switch (op) {
        case kEvBend:
        case kEvBendRel: {
            const int32_t steps = centToSteps(static_cast<int16_t>(arg));
            t.bend = clampBend(op == kEvBendRel ? t.bend + steps : steps);
            repitch(t);
            break;
        }
        case kEvPorta:    t.porta = arg; break;
        case kEvBlockEnd: jump(t, static_cast<int16_t>(arg)); break;
        case kEvDalSegno:
            if (arg != 0) {
                t.loopSp = 0;                 // 飛んだ先で入り直すループは1周目から
                jump(t, static_cast<int16_t>(arg));
            }
            break;
        case kEvFine:
            if (markHit(t, static_cast<uint8_t>(arg & 0xFF), static_cast<uint8_t>(arg >> 8))) {
                return daCapo(s, t);
            }
            break;
        case kEvSsgPeriod:
            device(t).ssgEnv(SsgEnv::PeriodLow,  t.channel, static_cast<uint8_t>(arg & 0xFF));
            device(t).ssgEnv(SsgEnv::PeriodHigh, t.channel, static_cast<uint8_t>(arg >> 8));
            break;
        default: break;
        }
        return false;
    }

    if (op < 0xF0) {                          // 3バイト
        const uint8_t  first    = readByte(t);
        const uint16_t distance = readWord(t);
        switch (op) {
        case kEvRegWrite: {
            const uint8_t data = static_cast<uint8_t>(distance & 0xFF);
            const uint8_t mask = static_cast<uint8_t>(distance >> 8);
            uint8_t value = data;
            if (mask != 0) {
                uint8_t current = 0;
                if (!device(t).regRead(first, current)) return false;
                value = static_cast<uint8_t>((current & mask) | data);
            }
            device(t).regWrite(first, value);
            break;
        }
        case kEvBlock: {
            // ループの外のブロックは1周目として扱う。
            const uint8_t round = (t.loopSp != 0) ? t.loop[t.loopSp - 1] : 1;
            if (round != first && distance != 0) jump(t, static_cast<int16_t>(distance));
            break;
        }
        case kEvLoopEnd: {
            if (t.loopSp == 0) break;         // 頭の無い終点
            uint8_t& round = t.loop[t.loopSp - 1];
            if (first != 0 && round >= first) {
                --t.loopSp;
                break;
            }
            if (round != 0xFF) ++round;       // 無限ループは 255 で数え止まる
            jump(t, static_cast<int16_t>(distance));
            break;
        }
        default: break;
        }
        return false;
    }

    // 4バイト
    const uint8_t  ordinal = readByte(t);
    const uint8_t  at      = readByte(t);
    const uint16_t distance = readWord(t);
    if (op == kEvToCoda && distance != 0 && markHit(t, ordinal, at)) {
        t.loopSp = 0;
        jump(t, static_cast<int16_t>(distance));
    }
    return false;
}

bool Sequencer::note(Sequence& s, Track& t, int index, uint8_t number) {
    (void)index;
    setGate(t);
    const bool carry = setGlide(t, number);
    if (!carry) keyOff(t);
    t.note = number;
    repitch(t);
    if (!carry && !s.mute && !t.muted) {
        device(t).keyOn(t.channel);
        if (activity_) activity_->noteOn(t.device, t.channel, t.outVolume);
    }
    return t.wait != 0;
}

// Q8 はゲートを置かない ―― 次のキーオンまで鳴り続ける。
// 直後で次の音符へつながる音符も、Q に関係なくゲートを置かない。`&` や `~` を
// 読むのは音長が尽きてからなので、ゲートがあるとその時には続ける相手が消えている。
void Sequencer::setGate(Track& t) {
    if (t.quant >= kQuantMax || joined(t)) {
        t.gate = 0;
        return;
    }
    uint16_t v = 0;
    for (uint8_t i = 0; i < t.quant; ++i) v = static_cast<uint16_t>(v + t.wait);
    v = static_cast<uint16_t>(v >> 3);
    t.gate = (v == 0) ? 1 : v;   // 0 はゲートが無いことになってしまう
}

// 音符の直後の1イベントだけを見る（ROM の TRKJOINED）。間に別のイベントが
// 挟まれば、Q どおり途中で切れる。数値のある `~` は鳴らし直すので、つながない。
bool Sequencer::joined(const Track& t) const {
    const auto at = [&t](size_t i) -> uint8_t {
        return (t.events && i < t.events->size()) ? (*t.events)[i] : kEvEnd;
    };
    const uint8_t op = at(t.ptr);
    if (op == kEvTie) return true;
    if (op != kEvPorta) return false;
    return (at(t.ptr + 1) | (at(t.ptr + 2) << 8)) == kPortaHere;
}

bool Sequencer::setGlide(Track& t, uint8_t number) {
    const uint16_t porta = t.porta;
    t.porta = 0;

    if (porta == 0) {
        t.glide = 0;
        return false;
    }
    if (porta == kPortaTie) {
        // 前の音符が既にキーオフされていれば、続ける相手がいない。
        t.glide = 0;
        return t.note != kNoNote;
    }
    if (t.wait == 0) {           // 滑る時間の無い音符
        t.glide = 0;
        return false;
    }
    if (porta == kPortaHere) {
        if (t.note == kNoNote) {
            t.glide = 0;
            return false;
        }
        // 残っていた滑りも足す ―― 前の `~` が終わる前に来た `~` は、いま音程が
        // 実際にあるところから始まる。
        const int32_t whole = (t.glide >> 8) + (static_cast<int32_t>(t.note) - number) * kFreqSemitone;
        t.glide = whole << 8;
        computeStep(t);
        return true;
    }
    // ~<cent>: その音符から指定ぶんずれた高さから始め、普通に打鍵する。
    t.glide = static_cast<int32_t>(clampBend(centToSteps(static_cast<int16_t>(porta)))) << 8;
    computeStep(t);
    return false;
}

// 音符の長さで距離を割ったものが1 tick の歩幅。切り上げるのは、足りない歩幅だと
// 音符が終わっても滑りが残るため。
void Sequencer::computeStep(Track& t) {
    const int32_t whole = t.glide >> 8;
    const uint32_t mag = static_cast<uint32_t>(whole < 0 ? -whole : whole);
    const uint32_t num = mag << 8;
    uint32_t q = num / t.wait;
    if (num % t.wait != 0) ++q;
    if (q == 0) q = 1;           // 歩幅 0 は永久に着かない
    t.gstep = (whole < 0) ? -static_cast<int32_t>(q) : static_cast<int32_t>(q);
}

void Sequencer::glideStep(Track& t) {
    if (t.glide == 0) return;
    const int32_t next = t.glide - t.gstep;
    // 符号がひっくり返ったら着いたということ。
    t.glide = ((next ^ t.glide) < 0) ? 0 : next;
    repitch(t);
}

int16_t Sequencer::bendOffset(const Track& t) const {
    return static_cast<int16_t>(t.bend + (t.glide >> 8) + tune_);
}

void Sequencer::repitch(Track& t) {
    if (t.note == kNoNote) return;
    device(t).setPitch(t.channel, t.note, bendOffset(t));
}

void Sequencer::keyOff(Track& t) {
    t.note = kNoNote;            // タイでつなぐ相手も、ベンドの相手も無くなる
    device(t).keyOff(t.channel);
    if (activity_) activity_->noteOff(t.device, t.channel);
}

// リズムの打撃を、楽器ごとの枠に分けて記録する。叩いた楽器だけが立ち上がる。
void Sequencer::reportStrike(Sequence& s, const Track& t, uint8_t instruments) {
    if (!activity_) return;
    constexpr uint8_t kBits[5] = {0x10, 0x08, 0x04, 0x02, 0x01};   // BD SD TOM CYM HH
    for (uint8_t i = 0; i < 5; ++i) {
        if (!(instruments & kBits[i])) continue;
        const uint8_t level15 = (t.rhythmAccent & kBits[i]) ? t.rhythmAccentLevel : t.rhythmLevel;
        // 0-15 を 0-127 に伸ばし、シーケンスの音量を掛ける。
        const uint8_t loud = mulVol(static_cast<uint8_t>(level15 * 127 / 15), (s.mute || t.muted) ? 0 : s.current);
        activity_->noteOn(t.device, static_cast<uint8_t>(kRhythmSlotFirst + i), loud);
    }
}

// トラックの音量にシーケンスの音量を掛ける。リズムチャンネルは打撃ごとに自分の
// レベルを持つので、シーケンスのぶんだけを送る。
void Sequencer::volumeOut(Sequence& s, Track& t) {
    const uint8_t own = (t.channel == kChannelRhythm) ? kMixerMax : t.vol;
    const uint8_t scale = (s.mute || t.muted) ? 0 : s.current;
    t.outVolume = mulVol(own, scale);
    device(t).setVolume(t.channel, t.outVolume);
}

// 距離は、その2バイトを読み終えた位置から数える。
void Sequencer::jump(Track& t, int16_t distance) {
    const int64_t target = static_cast<int64_t>(t.ptr) + distance;
    t.ptr = (target < 0) ? 0 : static_cast<size_t>(target);
}

// 到達するたびに数え、発火する回数と等しければ発火する。発火してもリセットしない。
bool Sequencer::markHit(Track& t, uint8_t ordinal, uint8_t at) {
    if (ordinal >= kMarkCount) return false;
    uint8_t& count = t.mark[ordinal];
    count = static_cast<uint8_t>(count + 1);   // 1バイトで一周する
    return count == at;
}

// ダカーポとフィーネは、そのトラックだけでなくシーケンス全体の周を終わらせる。
// ここではキーオフしない。次の周は同じ tick で始まり、音符も休符も最初に
// キーを切る。`0E` で始まるトラックだけが前の周の最後の音を保つ（bytecode.md）。
// 繰り返しを使い切ったときのキーオフは passEnd が持つ。
bool Sequencer::daCapo(Sequence& s, Track& t) {
    (void)t;
    s.active = 0;
    return true;
}

void Sequencer::endTrack(Sequence& s, Track& t, int index) {
    keyOff(t);
    s.active = static_cast<uint16_t>(s.active & ~(1u << index));
}

} // namespace y8960
