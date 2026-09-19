#pragma once
// ミュートの指定。CLI の --mute が受け取る形と、それをトラックに解決する規則。
//
//   <チップシンボル>            そのチップのトラック全部
//   <チップシンボル>,<CH番号>   そのチャンネルを持つトラック
//   T<トラック番号>             トラック番号 0-15
//   <A-P>                      トラック番号 0-15 を英字で
//
// 頭に ! を付けると「それ以外」を黙らせる。1チャンネルを持つのは1トラックだけ
// なので、どの指定も「どのトラックを黙らせるか」に解決できる。

#include "block.h"

#include <cstdint>
#include <string>
#include <vector>

namespace y8960 {

struct MuteSpec {
    enum class Kind { Chip, Channel, Track };
    Kind    kind    = Kind::Track;
    bool    except  = false;    // ! が付いている
    Device  device  = Device::SSGS;
    uint8_t channel = 0;
    uint8_t track   = 0;
};

// チップシンボル。bytecode.md のデバイス表の名前から空白を除いたもの。
const char* deviceSymbol(Device device);

// 1つの指定を読む。大文字小文字は区別しない。
bool parseMuteSpec(const std::string& text, MuteSpec& out, std::string& error);

// 黙らせるトラックのビット（bit n がトラック n）。
// ! の付いた指定が1つでもあれば、その和集合に入らない割り当て済みのトラックを
// すべて黙らせ、そのうえで ! の無い指定を黙らせる。
// シーケンスが使っていないチップ・チャンネル・トラックの指定は何もしない。
uint16_t resolveMutes(const std::vector<MuteSpec>& specs, const SequenceBlock& block);

} // namespace y8960
