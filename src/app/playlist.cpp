#include "playlist.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace y8960 {

Playlist::Playlist() : rng_(std::random_device{}()) {}

void Playlist::clear() {
    items_.clear();
    order_.clear();
    current_ = kNone;
    removedAt_ = kNone;
}

void Playlist::add(const std::string& path) {
    items_.push_back({path, fileNameOf(path), false});
    const size_t index = items_.size() - 1;
    if (!shuffle_) {
        order_.push_back(index);
        return;
    }
    // まだ鳴らしていない側のどこかに混ぜる。
    const int pos = positionOf(current_);
    const size_t first = (pos == kNone) ? 0 : static_cast<size_t>(pos) + 1;
    std::uniform_int_distribution<size_t> pick(first, order_.size());
    order_.insert(order_.begin() + static_cast<std::ptrdiff_t>(pick(rng_)), index);
}

void Playlist::remove(size_t index) {
    if (index >= items_.size()) return;
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
    const auto at = std::find(order_.begin(), order_.end(), index);
    const int pos = static_cast<int>(at - order_.begin());
    order_.erase(at);
    if (current_ == static_cast<int>(index)) removedAt_ = pos;
    else if (current_ == kNone && removedAt_ != kNone && pos < removedAt_) --removedAt_;
    for (size_t& i : order_) {
        if (i > index) --i;
    }
    if (current_ == static_cast<int>(index)) current_ = kNone;
    else if (current_ > static_cast<int>(index)) --current_;
}

void Playlist::move(size_t from, size_t to) {
    if (from >= items_.size() || to >= items_.size() || from == to) return;
    PlaylistItem item = std::move(items_[from]);
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(from));
    items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(to), std::move(item));

    auto remap = [from, to](size_t i) -> size_t {
        if (i == from) return to;
        if (from < to && i > from && i <= to) return i - 1;
        if (to < from && i >= to && i < from) return i + 1;
        return i;
    };
    // シャッフルしていないときの順は、表の並びそのもの。
    if (shuffle_) {
        for (size_t& i : order_) i = remap(i);
    } else {
        for (size_t i = 0; i < order_.size(); ++i) order_[i] = i;
    }
    if (current_ != kNone) current_ = static_cast<int>(remap(static_cast<size_t>(current_)));
}

void Playlist::setCurrent(int index) {
    removedAt_ = kNone;
    current_ = (index >= 0 && static_cast<size_t>(index) < items_.size()) ? index : kNone;
}

void Playlist::setShuffle(bool on) {
    shuffle_ = on;
    removedAt_ = kNone;
    if (on) {
        reshuffle();
    } else {
        for (size_t i = 0; i < order_.size(); ++i) order_[i] = i;
    }
}

void Playlist::reshuffle() {
    for (size_t i = 0; i < order_.size(); ++i) order_[i] = i;
    std::shuffle(order_.begin(), order_.end(), rng_);
    if (current_ != kNone) {
        auto it = std::find(order_.begin(), order_.end(), static_cast<size_t>(current_));
        std::iter_swap(order_.begin(), it);
    }
}

int Playlist::positionOf(int index) const {
    if (index == kNone) return kNone;
    const auto it = std::find(order_.begin(), order_.end(), static_cast<size_t>(index));
    return (it == order_.end()) ? kNone : static_cast<int>(it - order_.begin());
}

int Playlist::next() const {
    if (order_.empty()) return kNone;
    const int pos = positionOf(current_);
    if (pos == kNone) {
        if (removedAt_ == kNone) return static_cast<int>(order_.front());
        if (static_cast<size_t>(removedAt_) < order_.size()) return static_cast<int>(order_[static_cast<size_t>(removedAt_)]);
        return repeat_ ? static_cast<int>(order_.front()) : kNone;
    }
    const size_t after = static_cast<size_t>(pos) + 1;
    if (after < order_.size()) return static_cast<int>(order_[after]);
    return repeat_ ? static_cast<int>(order_.front()) : kNone;
}

int Playlist::previous() const {
    if (order_.empty()) return kNone;
    const int pos = positionOf(current_);
    if (pos == kNone) return static_cast<int>(order_.front());
    if (pos > 0) return static_cast<int>(order_[static_cast<size_t>(pos) - 1]);
    return repeat_ ? static_cast<int>(order_.back()) : kNone;
}

bool naturalLess(const std::string& a, const std::string& b) {
    auto digit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (digit(a[i]) && digit(b[j])) {
            size_t ei = i, ej = j;
            while (ei < a.size() && digit(a[ei])) ++ei;
            while (ej < b.size() && digit(b[ej])) ++ej;
            // 頭の 0 を除いた桁数で比べ、同じなら字の順で比べる。
            size_t zi = i, zj = j;
            while (zi + 1 < ei && a[zi] == '0') ++zi;
            while (zj + 1 < ej && b[zj] == '0') ++zj;
            if (ei - zi != ej - zj) return ei - zi < ej - zj;
            const int c = a.compare(zi, ei - zi, b, zj, ej - zj);
            if (c != 0) return c < 0;
            i = ei;
            j = ej;
            continue;
        }
        const int la = std::tolower(static_cast<unsigned char>(a[i]));
        const int lb = std::tolower(static_cast<unsigned char>(b[j]));
        if (la != lb) return la < lb;
        ++i;
        ++j;
    }
    if (a.size() - i != b.size() - j) return a.size() - i < b.size() - j;
    return a < b;
}

bool isDirectory(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(fs::u8path(path), ec);
}

std::string fileNameOf(const std::string& path) {
    const size_t cut = path.find_last_of("/\\");
    return (cut == std::string::npos) ? path : path.substr(cut + 1);
}

std::vector<std::string> sequenceFilesIn(const std::string& folder) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(folder), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc)) continue;
        std::string ext = it->path().extension().u8string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".sq") out.push_back(it->path().u8string());
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return naturalLess(fileNameOf(a), fileNameOf(b));
    });
    return out;
}

} // namespace y8960
