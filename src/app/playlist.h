#pragma once
// プレイリスト。曲の並び、いま鳴っている曲、次と前の決め方（シャッフル・リピート）を持つ。
// 音には触らない。パスは UTF-8 の文字列で持つ。

#include <cstddef>
#include <random>
#include <string>
#include <vector>

namespace y8960 {

struct PlaylistItem {
    std::string path;
    std::string name;             // 表示用。パスのファイル名の部分
    bool        broken = false;   // 読めなかった
};

class Playlist {
public:
    static constexpr int kNone = -1;

    Playlist();

    size_t size() const { return items_.size(); }
    bool   empty() const { return items_.empty(); }
    const PlaylistItem& operator[](size_t i) const { return items_[i]; }
    PlaylistItem&       operator[](size_t i) { return items_[i]; }

    void clear();
    void add(const std::string& path);
    void remove(size_t index);
    // from の曲を to の位置に置く（間の曲は1つずれる）。
    void move(size_t from, size_t to);

    int  current() const { return current_; }
    void setCurrent(int index);

    // シャッフルを入れると、いまの曲を頭にして残りを混ぜ直す。
    void setShuffle(bool on);
    bool shuffle() const { return shuffle_; }
    void setRepeat(bool on) { repeat_ = on; }
    bool repeat() const { return repeat_; }

    // 再生の順で、いまの曲の次と前。端でリピートが無ければ kNone。
    // いまの曲が無ければ、どちらも順の頭を返す。ただし鳴っていた曲を消したあとの
    // next は、消した曲の次にいた曲を返す。
    int next() const;
    int previous() const;

    // 再生の順（添字の並び）。
    const std::vector<size_t>& order() const { return order_; }

    // シャッフルの並びを決まったものにする。試験のため。
    void seed(unsigned value) { rng_.seed(value); }

private:
    int  positionOf(int index) const;
    void reshuffle();

    std::vector<PlaylistItem> items_;
    std::vector<size_t>       order_;
    int  current_ = kNone;
    // いまの曲を消したとき、順のどこにいたか。next はそこから続ける。
    int  removedAt_ = kNone;
    bool shuffle_ = false;
    bool repeat_  = false;
    std::mt19937 rng_;
};

// フォルダの直下にある .sq のファイル（大文字と小文字を問わない）を、名前の順に返す。
// 読めなければ空。
std::vector<std::string> sequenceFilesIn(const std::string& folder);

bool isDirectory(const std::string& path);
std::string fileNameOf(const std::string& path);
// 名前の順。大文字と小文字を区別せず、数字の並びは数として比べる（"2" が "10" より前）。
bool naturalLess(const std::string& a, const std::string& b);

} // namespace y8960
