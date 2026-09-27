#include "check.h"
#include "fader.h"
#include "playlist.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace y8960;
namespace fs = std::filesystem;

namespace {

Playlist three() {
    Playlist p;
    p.add("a/one.sq");
    p.add("a/two.sq");
    p.add("a/three.sq");
    return p;
}

} // namespace

int main() {
    // 順のとおりに進み、端でリピートが無ければ止まる。
    {
        Playlist p = three();
        CHECK(p[1].name == "two.sq");
        CHECK(p.next() == 0);
        p.setCurrent(0);
        CHECK(p.next() == 1);
        CHECK(p.previous() == Playlist::kNone);
        p.setCurrent(2);
        CHECK(p.next() == Playlist::kNone);
        p.setRepeat(true);
        CHECK(p.next() == 0);
        p.setCurrent(0);
        CHECK(p.previous() == 2);
    }

    // シャッフルは全部の曲を1回ずつ回し、いまの曲を頭にする。切れば表の順に戻る。
    {
        Playlist p;
        for (int i = 0; i < 20; ++i) p.add("s" + std::to_string(i) + ".sq");
        p.seed(1);
        p.setCurrent(7);
        p.setShuffle(true);
        std::vector<size_t> order = p.order();
        CHECK(order.front() == 7);
        std::vector<size_t> sorted = order;
        std::sort(sorted.begin(), sorted.end());
        bool all = sorted.size() == 20;
        for (size_t i = 0; all && i < sorted.size(); ++i) all = sorted[i] == i;
        CHECK(all);
        bool mixed = false;
        for (size_t i = 0; i < order.size(); ++i) mixed = mixed || order[i] != i;
        CHECK(mixed);
        CHECK(p.next() == static_cast<int>(order[1]));

        // 足した曲は、まだ鳴らしていない側に入る。
        p.add("late.sq");
        CHECK(p.order().size() == 21);
        CHECK(p.order().front() == 7);

        p.setShuffle(false);
        CHECK(p.next() == 8);
    }

    // 消すと添字が詰まり、いまの曲の添字も付いてくる。
    {
        Playlist p = three();
        p.setCurrent(2);
        p.remove(0);
        CHECK(p.size() == 2);
        CHECK(p.current() == 1);
        CHECK(p[p.current()].name == "three.sq");
        p.remove(1);
        CHECK(p.current() == Playlist::kNone);
        CHECK(p.order().size() == 1 && p.order()[0] == 0);
    }

    // 鳴っている曲を消しても、次はその曲の次にいた曲。末尾なら止まるかリピートで頭。
    {
        Playlist p = three();
        p.setCurrent(1);
        p.remove(1);
        CHECK(p.current() == Playlist::kNone);
        CHECK(p.next() == 1);
        CHECK(p[1].name == "three.sq");
        p.remove(0);          // 前にある曲を消しても、続きの曲は変わらない
        CHECK(p.next() == 0);
        CHECK(p[0].name == "three.sq");

        Playlist q = three();
        q.setCurrent(2);
        q.remove(2);
        CHECK(q.next() == Playlist::kNone);
        q.setRepeat(true);
        CHECK(q.next() == 0);
    }

    // 並べ替えると、いまの曲は同じ曲を指したまま、順は表の並びになる。
    {
        Playlist p = three();
        p.setCurrent(0);
        p.move(0, 2);
        CHECK(p[2].name == "one.sq");
        CHECK(p[0].name == "two.sq");
        CHECK(p.current() == 2);
        CHECK(p.next() == Playlist::kNone);
        p.move(2, 0);
        CHECK(p[0].name == "one.sq");
        CHECK(p.current() == 0);
        CHECK(p.next() == 1);
    }

    // 名前の順は、数字を数として比べ、大文字と小文字を区別しない。
    {
        CHECK(naturalLess("song2.sq", "song10.sq"));
        CHECK(!naturalLess("song10.sq", "song2.sq"));
        CHECK(naturalLess("Alpha.sq", "beta.sq"));
        CHECK(naturalLess("a.sq", "B.sq"));
        CHECK(naturalLess("t02.sq", "t10.sq"));
    }

    // フォルダの直下の .sq だけを、名前の順で拾う。
    {
        const fs::path dir = fs::temp_directory_path() / "y8960_playlist_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "sub");
        for (const char* name : {"b10.SQ", "b2.sq", "a.Sq", "notes.txt", "c.sqx"}) {
            std::ofstream(dir / name) << "x";
        }
        std::ofstream(dir / "sub" / "deep.sq") << "x";
        fs::create_directories(dir / "dir.sq");

        const std::vector<std::string> found = sequenceFilesIn(dir.u8string());
        std::vector<std::string> names;
        for (const std::string& f : found) names.push_back(fileNameOf(f));
        CHECK((names == std::vector<std::string>{"a.Sq", "b2.sq", "b10.SQ"}));
        CHECK(isDirectory(dir.u8string()));
        CHECK(!isDirectory(found.empty() ? std::string() : found[0]));
        CHECK(sequenceFilesIn((dir / "missing").u8string()).empty());
        fs::remove_all(dir, ec);
    }

    // フェードは指定のサンプル数で直線に落ち、落ちきったら 0 のまま。
    {
        Fader f;
        std::vector<float> l(8, 1.0f), r(8, 1.0f);
        f.apply(l.data(), r.data(), 8);
        CHECK(l[7] == 1.0f && !f.fading());
        f.start(4);
        CHECK(f.fading());
        std::fill(l.begin(), l.end(), 1.0f);
        std::fill(r.begin(), r.end(), 1.0f);
        f.apply(l.data(), r.data(), 8);
        CHECK(l[0] == 1.0f);
        CHECK(l[1] == 0.75f && r[1] == 0.75f);
        CHECK(l[3] == 0.25f);
        CHECK(l[4] == 0.0f && l[7] == 0.0f);
        CHECK(f.silent() && !f.fading());
        f.reset();
        CHECK(!f.silent());
        f.start(0);
        CHECK(f.silent());
    }

    return check::finish("playlist_test");
}
