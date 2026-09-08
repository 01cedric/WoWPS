#pragma once

/**
 * wowee_binary_io.hpp - the read and write primitives every .w* format uses.
 *
 * All fifty-odd formats share one layout: a four-byte magic, a version, a
 * length-prefixed name, an entry count, then records. Reading and writing that
 * comes down to four operations - a fixed-size value, a length-prefixed string,
 * and the same two backwards - plus the rule that a file's extension is added
 * if the caller left it off.
 *
 * Those four were copied into every format's .cpp, a hundred and forty-one
 * times, byte for byte. They are here now. A fix to any of them used to be a
 * hundred and forty-one edits, which in practice meant it was never a fix at
 * all: the one-megabyte cap on a string length is the only thing standing
 * between a corrupt file and a resize() that asks for four gigabytes, and if
 * that number ever needs to change it should change once.
 */

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace wowee {
namespace pipeline {

/// Write a fixed-size value exactly as it sits in memory.
///
/// These files are not portable across endianness or padding, and never claimed
/// to be - they are written and read by this program on one machine.
template <typename T>
void writePOD(std::ofstream& os, const T& v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

/// Read a fixed-size value. False means the file ended early.
template <typename T>
bool readPOD(std::ifstream& is, T& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(T));
    return is.gcount() == static_cast<std::streamsize>(sizeof(T));
}

/// Write a string as a 32-bit length followed by its bytes. No terminator.
inline void writeStr(std::ofstream& os, const std::string& s) {
    uint32_t n = static_cast<uint32_t>(s.size());
    writePOD(os, n);
    if (n > 0) os.write(s.data(), n);
}

/// Read a length-prefixed string.
///
/// The length is the first thing a corrupt or hostile file gets to choose, so it
/// is capped at a megabyte: no name in any of these formats is remotely that
/// long, and without the cap a garbage length is a resize() of whatever four
/// bytes happened to be there.
inline bool readStr(std::ifstream& is, std::string& s) {
    uint32_t n = 0;
    if (!readPOD(is, n)) return false;
    if (n > (1u << 20)) return false;  // 1 MiB sanity cap
    s.resize(n);
    if (n > 0) {
        is.read(s.data(), n);
        if (is.gcount() != static_cast<std::streamsize>(n)) {
            s.clear();
            return false;
        }
    }
    return true;
}

/// Write a vector of ids as a 32-bit count followed by the values.
inline void writeU32Vec(std::ofstream& os, const std::vector<uint32_t>& v) {
    uint32_t n = static_cast<uint32_t>(v.size());
    writePOD(os, n);
    if (n > 0) {
        os.write(reinterpret_cast<const char*>(v.data()),
                 static_cast<std::streamsize>(n * sizeof(uint32_t)));
    }
}

/// Read a count-prefixed vector of ids.
///
/// Capped for the reason readStr is: the count is the first thing a corrupt
/// file gets to choose, and without a cap a garbage length is a resize() of
/// whatever four bytes happened to be there. 4096 is what all three callers
/// used, checked byte for byte before they were merged.
inline bool readU32Vec(std::ifstream& is, std::vector<uint32_t>& v) {
    uint32_t n = 0;
    if (!readPOD(is, n)) return false;
    if (n > 4096) return false;
    v.resize(n);
    if (n > 0) {
        is.read(reinterpret_cast<char*>(v.data()),
                static_cast<std::streamsize>(n * sizeof(uint32_t)));
        if (is.gcount() !=
            static_cast<std::streamsize>(n * sizeof(uint32_t))) {
            v.clear();
            return false;
        }
    }
    return true;
}

/// Write `bytes` of zero padding.
///
/// The records hold a uint8_t or two followed by something wider, and the
/// padding keeps the wider field where a struct would have put it. Both halves
/// have to agree byte for byte or every field after it reads one place off, so
/// the two are written next to each other here rather than at each of the
/// thirty-two places that needed them.
inline void writePadding(std::ofstream& os, size_t bytes) {
    static const char zeros[8] = {};
    while (bytes > 0) {
        const size_t chunk = bytes < sizeof(zeros) ? bytes : sizeof(zeros);
        os.write(zeros, static_cast<std::streamsize>(chunk));
        bytes -= chunk;
    }
}

/// Step over that padding. False means the file ended inside it.
inline bool skipPadding(std::ifstream& is, size_t bytes) {
    char discard[8];
    while (bytes > 0) {
        const size_t chunk = bytes < sizeof(discard) ? bytes : sizeof(discard);
        is.read(discard, static_cast<std::streamsize>(chunk));
        if (is.gcount() != static_cast<std::streamsize>(chunk)) return false;
        bytes -= chunk;
    }
    return true;
}

/// Read the magic and version a .w* file opens with, and nothing after them.
///
/// For the formats whose header does not continue into a name and a count -
/// world map carries a world type and a grid size there instead - so they get
/// the length-checked magic without the fields they do not have. Reading those
/// with readCatalogHeader would consume four bytes of the next field.
inline bool readMagicAndVersion(std::ifstream& is, const char magic[4], uint32_t version) {
    char fileMagic[4];
    is.read(fileMagic, 4);
    if (is.gcount() != 4) return false;
    if (std::memcmp(fileMagic, magic, 4) != 0) return false;
    uint32_t fileVersion = 0;
    return readPOD(is, fileVersion) && fileVersion == version;
}

/// Write the header every .w* file starts with: magic, version, the catalog's
/// name, and how many entries follow.
inline void writeCatalogHeader(std::ofstream& os, const char magic[4], uint32_t version,
                               const std::string& name, uint32_t entryCount) {
    os.write(magic, 4);
    writePOD(os, version);
    writeStr(os, name);
    writePOD(os, entryCount);
}

/// Read that header back, and say whether this is the file it claims to be.
///
/// False means the magic is wrong - this is some other format, or not one of
/// ours at all - or the version is not the one this build reads, or the file
/// ended inside the header.
///
/// The entry count is capped for the same reason a string length is: it is the
/// next thing a corrupt file gets to choose, and it is about to become a
/// resize(). A million entries is far past anything real and far short of a
/// number that allocates the machine.
inline bool readCatalogHeader(std::ifstream& is, const char magic[4], uint32_t version,
                              std::string& name, uint32_t& entryCount) {
    char fileMagic[4];
    is.read(fileMagic, 4);
    if (is.gcount() != 4) return false;
    if (std::memcmp(fileMagic, magic, 4) != 0) return false;

    uint32_t fileVersion = 0;
    if (!readPOD(is, fileVersion) || fileVersion != version) return false;
    if (!readStr(is, name)) return false;

    entryCount = 0;
    if (!readPOD(is, entryCount)) return false;
    if (entryCount > (1u << 20)) return false;
    return true;
}

/// Add the format's extension unless the path already carries it.
///
/// Every format spelled this out with its own extension baked in, and with the
/// length of that extension written as a literal 5 - which is right for every
/// one of them and would be wrong the first time one is not four letters.
inline std::string normalizePath(std::string base, const std::string& extension) {
    if (base.size() < extension.size() ||
        base.compare(base.size() - extension.size(), extension.size(), extension) != 0) {
        base += extension;
    }
    return base;
}

/// Four colour channels as the single uint32 these formats store.
///
/// Red is the low byte and alpha the high one, which is the order the renderer
/// and ImGui both want and the reverse of how the channels are usually said
/// aloud. That is the whole reason this is a function: written out by hand,
/// the shift that is easy to get wrong is the one you cannot see is wrong -
/// a file whose blue and red are swapped loads, validates, round-trips, and
/// simply looks incorrect.
///
/// It was written out by hand in fifty-nine of these formats, identically. One
/// of them differing would have been one format's colours quietly reversed.
inline uint32_t packRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF) {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(r);
}

/// Make the directory an output file is about to be written into, if it needs
/// making and can be made.
///
/// std::filesystem::create_directories("") throws - "Invalid argument" - and a
/// bare filename has an empty parent path. The savers called it without that
/// guard and without a try, so
///
///     wowee_editor --gen-mesh-altar altar
///
/// terminated on an uncaught exception and dumped core, while the same command
/// with "./altar" wrote the file. Every procedural mesh generator, every
/// building save and every collision save went through one of those three
/// lines.
///
/// The error_code overload deliberately: a directory that cannot be made is
/// something to report when the write fails, which it is about to, rather than
/// a reason to end the process before the message can be printed.
inline void ensureParentDirectory(const std::string& filePath) {
    const std::filesystem::path parent = std::filesystem::path(filePath).parent_path();
    if (parent.empty()) return;  // writing into the working directory
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
}

/// Whether a catalog file is there to be read.
///
/// A hundred and twenty-one formats each wrote these two lines out.
inline bool catalogExists(const std::string& basePath, const std::string& extension) {
    std::ifstream is(normalizePath(basePath, extension), std::ios::binary);
    return is.good();
}

/// Write a catalog: its header, then `writeEntry` once per entry.
///
/// The scaffolding around a format's field list - open the file, count the
/// entries, put the header down, loop, report whether the stream survived - is
/// the same in every one of them, and is the part that has to be right for the
/// file to be readable at all. `writeEntry` writes one entry's fields in order
/// and is the only thing a format actually supplies.
/// The same, for a catalog whose collection is not called `entries`.
///
/// Eight of these formats name theirs for what it holds - classes, gems,
/// keyframes, maps - which reads better in their own code and put them outside
/// the reach of the version above, so they kept writing the scaffolding out.
/// The name and the container are arguments here instead.
template <typename Entries, typename WriteEntry>
bool saveCatalogEntries(const std::string& basePath, const char magic[4], uint32_t version,
                        const std::string& extension, const std::string& name,
                        const Entries& entries, WriteEntry writeEntry) {
    std::ofstream os(normalizePath(basePath, extension), std::ios::binary);
    if (!os) return false;
    writeCatalogHeader(os, magic, version, name, static_cast<uint32_t>(entries.size()));
    for (const auto& e : entries) writeEntry(os, e);
    return os.good();
}

template <typename Catalog, typename WriteEntry>
bool saveCatalog(const Catalog& cat, const std::string& basePath, const char magic[4],
                 uint32_t version, const std::string& extension, WriteEntry writeEntry) {
    return saveCatalogEntries(basePath, magic, version, extension, cat.name, cat.entries,
                              writeEntry);
}

/// Read a catalog back. An empty result means it could not be read.
///
/// `readEntry` fills one entry and answers false the moment a field runs off the
/// end of the file. Every format had to remember to empty the half-filled list
/// on that path and return, at each of the several places a field could fail;
/// forgetting once would have handed back entries built from whatever the reader
/// left in them. That is here now, and once.
template <typename Catalog, typename ReadEntry>
Catalog loadCatalog(const std::string& basePath, const char magic[4], uint32_t version,
                    const std::string& extension, ReadEntry readEntry) {
    Catalog out;
    std::ifstream is(normalizePath(basePath, extension), std::ios::binary);
    if (!is) return out;
    uint32_t entryCount = 0;
    if (!readCatalogHeader(is, magic, version, out.name, entryCount)) return out;
    out.entries.resize(entryCount);
    for (auto& e : out.entries) {
        if (!readEntry(is, e)) {
            out.entries.clear();
            return out;
        }
    }
    return out;
}

}  // namespace pipeline
}  // namespace wowee
