/**
 * dbc_to_csv - Convert binary WDBC files to CSV text format.
 *
 * Usage: dbc_to_csv <input.dbc> <output.csv>
 *
 * Output format:
 *   Line 1:  # fields=N strings=I,J,K,...    (metadata)
 *   Lines 2+: one record per line, comma-separated fields
 *             String fields are double-quoted with escaped inner quotes.
 *             Numeric fields are plain uint32.
 *
 * String column auto-detection:
 *   A column is marked as "string" when every non-zero value in that column
 *   is a valid offset into the WDBC string block (points to a printable,
 *   null-terminated string and doesn't exceed the block size).
 */

#include "pipeline/dbc_loader.hpp"
#include "core/logger.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>

using wowee::pipeline::DBCFile;

namespace {

// Read entire file into memory.
std::vector<uint8_t> readFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto size = f.tellg();
    if (size <= 0) return {};
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

// Precompute the set of valid string-boundary offsets in the string block.
// An offset is a valid boundary if it is 0 or immediately follows a null byte.
// This prevents small integer values (e.g. RaceID=1, 2, 3) from being falsely
// detected as string offsets just because they land in the middle of a longer
// string that starts at a lower offset.
std::set<uint32_t> computeStringBoundaries(const std::vector<uint8_t>& stringBlock) {
    std::set<uint32_t> boundaries;
    if (stringBlock.empty()) return boundaries;
    boundaries.insert(0); // offset 0 is always a valid start
    for (size_t i = 0; i + 1 < stringBlock.size(); ++i) {
        if (stringBlock[i] == 0) {
            boundaries.insert(static_cast<uint32_t>(i + 1));
        }
    }
    return boundaries;
}

// Check whether offset points to a valid string-boundary position in the block
// and that the string there is printable and null-terminated.
bool isValidStringOffset(const std::vector<uint8_t>& stringBlock,
                         const std::set<uint32_t>& boundaries,
                         uint32_t offset) {
    if (offset >= stringBlock.size()) return false;
    // Must start at a string boundary (offset 0 or right after a null byte).
    if (!boundaries.count(offset)) return false;
    // Must be null-terminated within the block and contain only printable/whitespace bytes.
    for (size_t i = offset; i < stringBlock.size(); ++i) {
        uint8_t c = stringBlock[i];
        if (c == 0) return true;   // found terminator
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') return false;
    }
    return false; // ran off end without terminator
}

// Detect which columns are string columns.
std::set<uint32_t> detectStringColumns(const DBCFile& dbc,
                                        const std::vector<uint8_t>& rawData) {
    // Reconstruct the string block from the raw file.
    // Header is 20 bytes, then recordCount * recordSize bytes of records, then string block.
    uint32_t recordCount = dbc.getRecordCount();
    uint32_t fieldCount  = dbc.getFieldCount();
    uint32_t recordSize  = dbc.getRecordSize();
    uint32_t strBlockSize = dbc.getStringBlockSize();

    size_t strBlockOffset = 20 + static_cast<size_t>(recordCount) * recordSize;
    std::vector<uint8_t> stringBlock;
    if (strBlockSize > 0 && strBlockOffset + strBlockSize <= rawData.size()) {
        stringBlock.assign(rawData.begin() + strBlockOffset,
                           rawData.begin() + strBlockOffset + strBlockSize);
    }

    std::set<uint32_t> stringCols;

    // If no string block (or trivial size), no string columns.
    if (stringBlock.size() <= 1) return stringCols;

    // Precompute valid string-start boundaries to avoid false positives from
    // integer fields whose small values accidentally land inside longer strings.
    auto boundaries = computeStringBoundaries(stringBlock);

    // Field 0 is always the numeric record ID - skip it.
    for (uint32_t col = 1; col < fieldCount; ++col) {
        bool allZeroOrValid = true;
        bool hasNonZero = false;
        std::set<std::string> distinctStrings;

        for (uint32_t row = 0; row < recordCount; ++row) {
            uint32_t val = dbc.getUInt32(row, col);
            if (val == 0) continue;
            hasNonZero = true;
            if (!isValidStringOffset(stringBlock, boundaries, val)) {
                allZeroOrValid = false;
                break;
            }
            // Collect distinct non-empty strings for diversity check.
            const char* s = reinterpret_cast<const char*>(stringBlock.data() + val);
            if (*s != '\0') {
                distinctStrings.insert(std::string(s, strnlen(s, 256)));
            }
        }

        // Require at least 2 distinct non-empty string values.  Columns that
        // only ever point to a single string (e.g. SexID=1 always resolves to
        // the same path fragment at offset 1 in the block) are almost certainly
        // integer fields whose small values accidentally land at a string boundary.
        if (allZeroOrValid && hasNonZero && distinctStrings.size() >= 2) {
            stringCols.insert(col);
        }
    }

    return stringCols;
}

// Escape a string for CSV (double-quote, escape inner quotes).
std::string csvEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        if (c == '"') out += '"'; // double the quote
        out += c;
    }
    out += '"';
    return out;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: dbc_to_csv <input.dbc> <output.csv>\n";
        return 1;
    }

    const std::string inputPath  = argv[1];
    const std::string outputPath = argv[2];

    // Read input file.
    auto rawData = readFileBytes(inputPath);
    if (rawData.empty()) {
        std::cerr << "Error: cannot read " << inputPath << "\n";
        return 1;
    }

    // This tool is for converting binary WDBC (.dbc) files only.
    if (rawData.size() < 4 || std::memcmp(rawData.data(), "WDBC", 4) != 0) {
        std::cerr << "Error: input is not a binary WDBC DBC file: " << inputPath << "\n";
        return 1;
    }

    // Load as WDBC.
    DBCFile dbc;
    if (!dbc.load(rawData)) {
        std::cerr << "Error: failed to parse DBC file " << inputPath << "\n";
        return 1;
    }

    uint32_t recordCount = dbc.getRecordCount();
    uint32_t fieldCount  = dbc.getFieldCount();

    // Detect string columns.
    auto stringCols = detectStringColumns(dbc, rawData);

    // Ensure output directory exists.
    std::filesystem::path outDir = std::filesystem::path(outputPath).parent_path();
    if (!outDir.empty()) {
        std::filesystem::create_directories(outDir);
    }

    // Write CSV.
    std::ofstream out(outputPath);
    if (!out) {
        std::cerr << "Error: cannot write " << outputPath << "\n";
        return 1;
    }

    // Metadata line.
    out << "# fields=" << fieldCount;
    if (!stringCols.empty()) {
        out << " strings=";
        bool first = true;
        for (uint32_t col : stringCols) {
            if (!first) out << ',';
            out << col;
            first = false;
        }
    }
    out << '\n';

    // Data rows.
    for (uint32_t row = 0; row < recordCount; ++row) {
        for (uint32_t col = 0; col < fieldCount; ++col) {
            if (col > 0) out << ',';
            if (stringCols.count(col)) {
                out << csvEscape(dbc.getString(row, col));
            } else {
                out << dbc.getUInt32(row, col);
            }
        }
        out << '\n';
    }

    out.close();

    std::cout << std::filesystem::path(inputPath).filename().string()
              << " -> " << std::filesystem::path(outputPath).filename().string()
              << "  (" << recordCount << " records, " << fieldCount << " fields, "
              << stringCols.size() << " string cols)\n";
    return 0;
}
