#pragma once

/// Reading the flat "name": number objects the expansion tables are stored in.
///
/// opcodes.json and update_fields.json have the same shape and each had its
/// own scanner. They did not agree: the opcode one understood hex and quoted
/// values, the update-field one understood neither, and the difference was
/// invisible because the update field files happen to be all decimal.
///
/// The failure mode is what makes this worth one function. std::stoul("0x12")
/// in base ten does not throw - it reads the leading zero, stops at the 'x'
/// and answers 0. An opcode 0 is a real opcode and update field 0 is the
/// object GUID, so a table read that way loads, reports success, and is
/// wrong for the rest of the session.

#include <cstdint>
#include <cstdlib>
#include <string>

namespace wowee::game {

/// Parse a table value: plain decimal, or hex with an 0x prefix.
///
/// False unless the whole string, once surrounding whitespace is removed, is
/// a number that fits in 32 bits. `out` is left alone when it fails, so a
/// caller can skip the row rather than store a plausible wrong number.
///
/// The base is chosen from the prefix rather than left to the library: base 0
/// would read a leading zero as octal, and these files are hand-edited.
inline bool parseTableNumber(const std::string& text, uint32_t& out) {
    size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return false;
    size_t end = text.find_last_not_of(" \t\r\n");
    const std::string trimmed = text.substr(begin, end - begin + 1);
    if (trimmed.empty()) return false;

    int base = 10;
    size_t at = 0;
    if (trimmed.size() > 2 && trimmed[0] == '0' &&
        (trimmed[1] == 'x' || trimmed[1] == 'X')) {
        base = 16;
        at = 2;
    }
    // A sign is not a table value, and strtoul would accept one.
    if (trimmed[at] == '-' || trimmed[at] == '+') return false;

    errno = 0;
    char* stop = nullptr;
    const unsigned long value =
        std::strtoul(trimmed.c_str() + at, &stop, base);
    if (errno != 0) return false;
    // Every character has to have been part of the number.
    if (stop == nullptr || *stop != '\0' || stop == trimmed.c_str() + at) {
        return false;
    }
    if (value > 0xFFFFFFFFul) return false;

    out = static_cast<uint32_t>(value);
    return true;
}

/// Call fn(key, valueText) for each "key": value pair in a flat JSON object.
///
/// Deliberately not a JSON parser: these files are a single flat object of
/// name to number, and nesting has never appeared in one. Quotes around a
/// value are stepped over, so both the quoted and bare forms read the same.
template <typename Fn>
void forEachJsonKeyValue(const std::string& json, Fn&& fn) {
    size_t pos = 0;
    while (pos < json.size()) {
        const size_t keyStart = json.find('"', pos);
        if (keyStart == std::string::npos) break;
        const size_t keyEnd = json.find('"', keyStart + 1);
        if (keyEnd == std::string::npos) break;
        std::string key = json.substr(keyStart + 1, keyEnd - keyStart - 1);

        const size_t colon = json.find(':', keyEnd);
        if (colon == std::string::npos) break;

        size_t valStart = colon + 1;
        while (valStart < json.size() &&
               (json[valStart] == ' ' || json[valStart] == '\t' ||
                json[valStart] == '\r' || json[valStart] == '\n' ||
                json[valStart] == '"')) {
            ++valStart;
        }

        size_t valEnd = json.find_first_of(",}\"\r\n", valStart);
        if (valEnd == std::string::npos) valEnd = json.size();

        fn(key, json.substr(valStart, valEnd - valStart));
        pos = valEnd + 1;
    }
}

}  // namespace wowee::game
