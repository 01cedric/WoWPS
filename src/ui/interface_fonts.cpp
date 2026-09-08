#include "ui/interface_fonts.hpp"

#include <imgui.h>
#include <cfloat>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string_view>
#include <map>
#include <vector>

namespace wowee::ui {

namespace {

/// The lower-case stem of a path, with any directory and extension removed.
/// "Fonts\\MORPHEUS.ttf" and "morpheus.ttf" both come out as "morpheus", which
/// is what lets a font object written one way find a file named the other.
///
/// Written into the caller's buffer rather than into a string of its own.
///
/// This used to build up to two std::strings per call, one for the stem after
/// the slash and another for the stem before the dot, and it is asked for every
/// font string the renderer draws. Measured rather than assumed, because the
/// obvious reading of that is wrong: every face name in the game is a short
/// filename - FRIZQT__.TTF is twelve characters - so both strings fitted in the
/// small-string buffer and neither ever reached the heap. What they cost was
/// two constructions and a transform, about 8 ns of a 42 ns lookup on a host.
///
/// So this is a fifth off a small number rather than the end of an allocation,
/// and it is worth having for the second reason as much as the first: with a
/// view on the way in, no caller has to build a string to ask, and a face name
/// that is longer than the buffer cannot start allocating again by surprise. A
/// name that long is truncated rather than refused, and truncation can only
/// lose a lookup that had nothing to find.
constexpr size_t kFaceKeyMax = 96;

std::string_view faceKey(std::string_view pathOrName, char (&buf)[kFaceKeyMax]) {
    const size_t slash = pathOrName.find_last_of("\\/");
    std::string_view stem = (slash == std::string_view::npos)
                                ? pathOrName : pathOrName.substr(slash + 1);
    const size_t dot = stem.find_last_of('.');
    if (dot != std::string_view::npos) stem = stem.substr(0, dot);
    const size_t n = (stem.size() < kFaceKeyMax) ? stem.size() : kFaceKeyMax;
    for (size_t i = 0; i < n; ++i) {
        buf[i] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(stem[i])));
    }
    return {buf, n};
}

/// Hashing that takes a view, so a lookup is not obliged to build a string in
/// order to be compared against one. std::equal_to<> supplies the transparent
/// comparison that goes with it; without both, find() on a string_view does not
/// compile and the allocation comes back by the front door.
/// std::map rather than std::unordered_map, and deliberately.
///
/// Heterogeneous lookup - find() taking a string_view against std::string keys,
/// which is the whole point of the normalisation above - reached the unordered
/// containers only in C++20 (P0919). The PS4 toolchain's libc++ predates it, so
/// the hashed version compiled on the host and failed the console build with
/// "no known conversion from string_view to const key_type". The ordered
/// containers have had it since C++14 and every toolchain here implements it.
///
/// Nothing is lost by the change: this map holds the interface's handful of
/// typefaces, and a red-black tree over a dozen short keys is not measurably
/// slower to search than a hash of the same string.
using FaceMap = std::map<std::string, ImFont*, std::less<>>;

FaceMap& faces() {
    static FaceMap map;
    return map;
}

/// A big-endian field out of a font file, or zero past the end.
///
/// Bounds-checked on every read rather than once at the top: a truncated font
/// is a file this client did not write, and the tables it names are at offsets
/// the file itself supplies.
uint32_t beAt(const uint8_t* d, size_t n, size_t at, int bytes) {
    if (at + static_cast<size_t>(bytes) > n) return 0;
    uint32_t v = 0;
    for (int i = 0; i < bytes; ++i) v = (v << 8) | d[at + static_cast<size_t>(i)];
    return v;
}

} // namespace

void registerInterfaceFace(std::string_view pathOrName, ImFont* font) {
    if (!font) return;
    char buf[kFaceKeyMax];
    // The one place a string is built, and it happens a handful of times before
    // the first frame.
    faces()[std::string(faceKey(pathOrName, buf))] = font;
}

ImFont* interfaceFace(std::string_view pathOrName) {
    if (pathOrName.empty()) return nullptr;
    char buf[kFaceKeyMax];
    const auto it = faces().find(faceKey(pathOrName, buf));
    return (it == faces().end()) ? nullptr : it->second;
}

ImFont* interfaceFaceOrDefault(std::string_view fontFace) {
    // The order the renderer uses. The interface's own default rather than this
    // client's, because ImGui draws with whatever face was added first and that
    // is deliberately the built-in one.
    if (ImFont* f = interfaceFace(fontFace)) return f;
    if (ImFont* f = interfaceFace("frizqt__")) return f;
    return (ImGui::GetCurrentContext() != nullptr) ? ImGui::GetFont() : nullptr;
}

float interfaceFontSize(float fontHeight) {
    if (fontHeight > 0.0f) return fontHeight;
    // What the renderer falls back to: the current size, not a flat twelve.
    return (ImGui::GetCurrentContext() != nullptr) ? ImGui::GetFontSize() : 12.0f;
}

float interfaceTextWidth(const std::string& text, std::string_view fontFace,
                         float fontHeight) {
    if (text.empty()) return 0.0f;
    const float size = interfaceFontSize(fontHeight);
    if (ImFont* font = interfaceFaceOrDefault(fontFace)) {
        return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
    }
    // No context yet - during the FrameXML load there may be no frame in
    // flight to ask. An estimate is far better than nothing: the alternative
    // is answering zero, and MoneyFrame does SetWidth(GetTextWidth() +
    // iconWidth), which then places three buttons on top of each other.
    return static_cast<float>(text.size()) * size * 0.5f;
}

float fontEmSizeScale(const void* ttfData, size_t byteCount) {
    const auto* d = static_cast<const uint8_t*>(ttfData);
    if (!d || byteCount < 12) return 1.0f;

    // A collection points at its first face; a plain font is already there.
    size_t base = 0;
    if (beAt(d, byteCount, 0, 4) == 0x74746366u /* 'ttcf' */) {
        base = beAt(d, byteCount, 12, 4);
        if (base + 12 > byteCount) return 1.0f;
    }

    const uint32_t tables = beAt(d, byteCount, base + 4, 2);
    size_t head = 0, hhea = 0;
    for (uint32_t i = 0; i < tables; ++i) {
        const size_t rec = base + 12 + static_cast<size_t>(i) * 16;
        const uint32_t tag = beAt(d, byteCount, rec, 4);
        if (tag == 0x68656164u /* 'head' */) head = beAt(d, byteCount, rec + 8, 4);
        else if (tag == 0x68686561u /* 'hhea' */) hhea = beAt(d, byteCount, rec + 8, 4);
    }
    if (head == 0 || hhea == 0) return 1.0f;

    const float upem = static_cast<float>(beAt(d, byteCount, head + 18, 2));
    // Signed, and the descender is negative in every font that has one.
    const auto s16 = [&](size_t at) {
        return static_cast<float>(static_cast<int16_t>(beAt(d, byteCount, at, 2)));
    };
    const float span = s16(hhea + 4) - s16(hhea + 6);
    if (upem <= 0.0f || span <= 0.0f) return 1.0f;

    // A face whose span is wildly different from its em is more likely a
    // misread than a real design, and scaling by it would ruin the text rather
    // than improve it. FRIZQT__ is 1.215 and the other four are near it.
    const float scale = span / upem;
    return (scale >= 0.5f && scale <= 3.0f) ? scale : 1.0f;
}

float fontEmSizeScaleOfFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return 1.0f;
    // The tables this reads are in the first few hundred bytes of every font,
    // but the directory can name them in any order, so the file is read whole
    // rather than guessed at. These are a few hundred kilobytes each.
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    return fontEmSizeScale(bytes.data(), bytes.size());
}

} // namespace wowee::ui
