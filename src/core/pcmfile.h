#pragma once
// Y8PC ファイル（Y8960BasicExtension の doc/pcmfile.md）の読み込み。
// `CALL EXPORT PCM` が書き出すもので、ボイスファイルの設定と ADPCM メモリの
// ダンプを運ぶ。どちらか一方だけのファイルもある。

#include "block.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace y8960 {

constexpr uint32_t kAdpcmPageSize = 256;

struct PcmFile {
    bool hasSettings = false;
    bool hasDump     = false;
    std::array<AdpcmVoiceFile, kAdpcmFiles> settings{};
    std::vector<uint8_t> dump;   // ADPCM メモリのページ 0 から
};

bool parsePcmFile(const std::vector<uint8_t>& file, PcmFile& out, std::string& error);

// 演奏に使うボイスファイルの表を決める。Y8PC の設定があればそれがすべてで、
// ブロックのチャンク `03` は使わない（利用者の決定、2026-09-17）。
std::array<AdpcmVoiceFile, kAdpcmFiles> resolveAdpcmDirectory(const SequenceBlock& block,
                                                              const PcmFile* pcm);

} // namespace y8960
