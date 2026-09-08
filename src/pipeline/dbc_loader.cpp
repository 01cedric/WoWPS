#include "pipeline/dbc_loader.hpp"
#include "core/logger.hpp"
#include <nlohmann/json.hpp>
#include <cctype>
#include <cmath>
#include <cstring>
#include <set>
#include <sstream>
#include <string>

namespace wowee {
namespace pipeline {

namespace {
std::string trimAscii(std::string s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return s.substr(b, e - b);
}
} // namespace

DBCFile::DBCFile() = default;
DBCFile::~DBCFile() = default;

bool DBCFile::load(const std::vector<uint8_t>& dbcData) {
    if (dbcData.empty()) {
        LOG_ERROR("DBC data is empty");
        return false;
    }

    // Detect CSV format: starts with '#'
    if (dbcData[0] == '#') {
        return loadCSV(dbcData);
    }

    // Detect JSON format: starts with '{'
    if (dbcData[0] == '{' || (dbcData[0] <= ' ' && dbcData.size() > 1)) {
        size_t start = 0;
        while (start < dbcData.size() && dbcData[start] <= ' ') start++;
        if (start < dbcData.size() && dbcData[start] == '{') {
            return loadJSON(dbcData);
        }
    }

    if (dbcData.size() < sizeof(DBCHeader)) {
        LOG_ERROR("DBC data too small for header");
        return false;
    }

    // Read header safely (avoid unaligned reinterpret_cast - UB on strict platforms)
    DBCHeader header;
    std::memcpy(&header, dbcData.data(), sizeof(DBCHeader));

    // Verify magic
    if (std::memcmp(header.magic, "WDBC", 4) != 0) {
        LOG_ERROR("Invalid DBC magic: ", std::string(header.magic, 4));
        return false;
    }

    recordCount = header.recordCount;
    fieldCount = header.fieldCount;
    recordSize = header.recordSize;
    stringBlockSize = header.stringBlockSize;

    // Reject absurd header values up front. Real DBCs cap at ~1M records
    // and 1024 fields; large stringBlockSize is up to ~64MB. Multiplying
    // these without bounds risks uint32 overflow on the totalRecordSize
    // computation below - the resize would be tiny but the memcpy would
    // read TB of memory.
    if (recordCount > 10'000'000 || fieldCount > 1024 ||
        recordSize > 1024 * 4 ||
        stringBlockSize > 256u * 1024 * 1024) {
        LOG_ERROR("DBC header rejected: recordCount=", recordCount,
                  " fieldCount=", fieldCount, " recordSize=", recordSize,
                  " stringBlockSize=", stringBlockSize);
        return false;
    }

    // Validate sizes - use uint64 for the product so the overflow check
    // above is the only path that allows a large recordCount * recordSize.
    uint64_t expectedSize = sizeof(DBCHeader) +
                            static_cast<uint64_t>(recordCount) * recordSize +
                            stringBlockSize;
    if (dbcData.size() < expectedSize) {
        LOG_ERROR("DBC file truncated: expected ", expectedSize, " bytes, got ", dbcData.size());
        return false;
    }

    // DBC fields are fixed-width uint32 (4 bytes each); record size must match.
    // Mismatches indicate a corrupted header or unsupported DBC variant.
    if (recordSize != fieldCount * 4) {
        LOG_WARNING("DBC record size mismatch: recordSize=", recordSize,
                    " but fieldCount*4=", fieldCount * 4);
    }

    LOG_DEBUG("Loading DBC: ", recordCount, " records, ",
              fieldCount, " fields, ", recordSize, " bytes/record, ",
              stringBlockSize, " string bytes");

    // Copy record data. Use size_t for the product so it matches the
    // header-validated 64-bit expectedSize math above.
    const uint8_t* recordStart = dbcData.data() + sizeof(DBCHeader);
    size_t totalRecordSize = static_cast<size_t>(recordCount) * recordSize;
    recordData.resize(totalRecordSize);
    if (totalRecordSize > 0) {
        std::memcpy(recordData.data(), recordStart, totalRecordSize);
    }

    // Copy string block
    const uint8_t* stringStart = recordStart + totalRecordSize;
    stringBlock.resize(stringBlockSize);
    if (stringBlockSize > 0) {
        std::memcpy(stringBlock.data(), stringStart, stringBlockSize);
    }

    loaded = true;
    idCache_ = std::make_shared<IdCache>();

    return true;
}

const uint8_t* DBCFile::getRecord(uint32_t index) const {
    if (!loaded || index >= recordCount) {
        return nullptr;
    }

    return recordData.data() + (index * recordSize);
}

uint32_t DBCFile::getUInt32(uint32_t recordIndex, uint32_t fieldIndex) const {
    if (!loaded || recordIndex >= recordCount || fieldIndex >= fieldCount ||
        uint64_t(fieldIndex) * 4 + 4 > recordSize) {
        return 0;
    }

    const uint8_t* record = getRecord(recordIndex);
    if (!record) {
        return 0;
    }

    uint32_t value;
    std::memcpy(&value, record + (fieldIndex * 4), sizeof(uint32_t));
    return value;
}

int32_t DBCFile::getInt32(uint32_t recordIndex, uint32_t fieldIndex) const {
    return static_cast<int32_t>(getUInt32(recordIndex, fieldIndex));
}

float DBCFile::getFloat(uint32_t recordIndex, uint32_t fieldIndex) const {
    if (!loaded || recordIndex >= recordCount || fieldIndex >= fieldCount ||
        uint64_t(fieldIndex) * 4 + 4 > recordSize) {
        return 0.0f;
    }

    const uint8_t* record = getRecord(recordIndex);
    if (!record) {
        return 0.0f;
    }

    float value;
    std::memcpy(&value, record + (fieldIndex * 4), sizeof(float));
    return value;
}

std::string DBCFile::getString(uint32_t recordIndex, uint32_t fieldIndex) const {
    return std::string(getStringView(recordIndex, fieldIndex));
}

std::string_view DBCFile::getStringView(uint32_t recordIndex, uint32_t fieldIndex) const {
    uint32_t offset = getUInt32(recordIndex, fieldIndex);
    return getStringViewByOffset(offset);
}

std::string DBCFile::getStringByOffset(uint32_t offset) const {
    return std::string(getStringViewByOffset(offset));
}

std::string_view DBCFile::getStringViewByOffset(uint32_t offset) const {
    if (!loaded || offset >= stringBlockSize) {
        return {};
    }

    const char* str = reinterpret_cast<const char*>(stringBlock.data() + offset);
    const char* end = reinterpret_cast<const char*>(stringBlock.data() + stringBlockSize);

    size_t length = 0;
    while (str + length < end && str[length] != '\0') {
        length++;
    }

    return std::string_view(str, length);
}

int32_t DBCFile::findRecordById(uint32_t id) const {
    if (!loaded) {
        return -1;
    }

    // AssetManager shares table objects between terrain/audio/main threads.
    // Publish a complete index once; simultaneous first queries must not
    // clear or mutate the same unordered_map while another thread reads it.
    std::call_once(idCache_->once, [this] { buildIdCache(); });

    auto it = idCache_->indices.find(id);
    if (it != idCache_->indices.end()) {
        return static_cast<int32_t>(it->second);
    }

    return -1;
}

void DBCFile::buildIdCache() const {
    auto& indices = idCache_->indices;
    indices.clear();

    for (uint32_t i = 0; i < recordCount; i++) {
        uint32_t id = getUInt32(i, 0);  // Assume first field is ID
        indices[id] = i;
    }

    LOG_DEBUG("Built DBC ID cache with ", indices.size(), " entries");
}

bool DBCFile::loadCSV(const std::vector<uint8_t>& csvData) {
    std::string text(reinterpret_cast<const char*>(csvData.data()), csvData.size());
    std::istringstream stream(text);
    std::string line;

    // --- Parse metadata line: # fields=N strings=I,J,K ---
    if (!std::getline(stream, line) || line.empty() || line[0] != '#') {
        LOG_ERROR("CSV DBC: missing metadata line");
        return false;
    }

    fieldCount = 0;
    std::set<uint32_t> stringCols;

    // Parse "fields=N"
    auto fieldsPos = line.find("fields=");
    if (fieldsPos != std::string::npos) {
        try {
            fieldCount = static_cast<uint32_t>(std::stoul(line.substr(fieldsPos + 7)));
        } catch (...) {
            fieldCount = 0;
        }
    }
    if (fieldCount == 0) {
        LOG_ERROR("CSV DBC: invalid field count");
        return false;
    }

    // Parse "strings=I,J,K"
    auto stringsPos = line.find("strings=");
    if (stringsPos != std::string::npos) {
        std::istringstream ss(line.substr(stringsPos + 8));
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            tok = trimAscii(tok);
            if (!tok.empty()) {
                try {
                    stringCols.insert(static_cast<uint32_t>(std::stoul(tok)));
                } catch (...) {
                    LOG_WARNING("CSV DBC: invalid string column index token: '", tok, "'");
                }
            }
        }
    }

    // Field 0 is always the numeric record ID in DBC files - never a string.
    // Some CSV exports incorrectly mark it as a string column; force-remove it.
    if (stringCols.erase(0) > 0) {
        LOG_DEBUG("CSV DBC: removed field 0 from string columns (always numeric ID)");
    }

    recordSize = fieldCount * 4;

    // --- Build string block with initial null byte ---
    stringBlock.clear();
    stringBlock.push_back(0); // offset 0 = empty string

    // --- Parse data rows ---
    struct RowData {
        std::vector<uint32_t> fields;
    };
    std::vector<RowData> rows;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        RowData row;
        row.fields.resize(fieldCount, 0);

        uint32_t col = 0;
        size_t pos = 0;

        while (col < fieldCount && pos < line.size()) {
            if (stringCols.count(col) && pos < line.size() && line[pos] == '"') {
                // Quoted string field
                pos++; // skip opening quote
                std::string str;
                while (pos < line.size()) {
                    if (line[pos] == '"') {
                        if (pos + 1 < line.size() && line[pos + 1] == '"') {
                            str += '"'; // escaped quote
                            pos += 2;
                        } else {
                            pos++; // closing quote
                            break;
                        }
                    } else {
                        str += line[pos++];
                    }
                }
                // Skip comma after closing quote
                if (pos < line.size() && line[pos] == ',') pos++;

                // Store string in string block
                if (str.empty()) {
                    row.fields[col] = 0; // points to empty string at offset 0
                } else {
                    uint32_t offset = static_cast<uint32_t>(stringBlock.size());
                    stringBlock.insert(stringBlock.end(), str.begin(), str.end());
                    stringBlock.push_back(0); // null terminator
                    row.fields[col] = offset;
                }
            } else if (pos < line.size() && line[pos] == '"') {
                // Quoted value in numeric field - skip quotes, try to parse content
                pos++; // skip opening quote
                std::string str;
                while (pos < line.size()) {
                    if (line[pos] == '"') {
                        if (pos + 1 < line.size() && line[pos + 1] == '"') {
                            str += '"';
                            pos += 2;
                        } else {
                            pos++; // closing quote
                            break;
                        }
                    } else {
                        str += line[pos++];
                    }
                }
                if (pos < line.size() && line[pos] == ',') pos++;
                if (!str.empty()) {
                    try {
                        row.fields[col] = static_cast<uint32_t>(std::stoul(str));
                    } catch (...) {
                        row.fields[col] = 0;
                    }
                }
            } else {
                // Numeric field - read until comma or end of line
                size_t end = line.find(',', pos);
                if (end == std::string::npos) end = line.size();
                std::string tok = line.substr(pos, end - pos);
                if (!tok.empty()) {
                    try {
                        row.fields[col] = static_cast<uint32_t>(std::stoul(tok));
                    } catch (...) {
                        row.fields[col] = 0; // non-numeric value in numeric field
                    }
                }
                pos = (end < line.size()) ? end + 1 : line.size();
            }
            col++;
        }

        rows.push_back(std::move(row));
    }

    // --- Build record data (binary layout identical to WDBC) ---
    recordCount = static_cast<uint32_t>(rows.size());
    stringBlockSize = static_cast<uint32_t>(stringBlock.size());

    recordData.resize(static_cast<size_t>(recordCount) * recordSize);
    for (uint32_t i = 0; i < recordCount; ++i) {
        uint8_t* dst = recordData.data() + static_cast<size_t>(i) * recordSize;
        for (uint32_t f = 0; f < fieldCount; ++f) {
            uint32_t val = rows[i].fields[f];
            std::memcpy(dst + f * 4, &val, 4);
        }
    }

    loaded = true;
    idCache_ = std::make_shared<IdCache>();

    LOG_DEBUG("Loaded CSV DBC: ", recordCount, " records, ",
              fieldCount, " fields, ", stringCols.size(), " string cols, ",
              stringBlockSize, " string bytes");
    return true;
}

bool DBCFile::loadJSON(const std::vector<uint8_t>& jsonData) {
    try {
        auto j = nlohmann::json::parse(jsonData.begin(), jsonData.end());

        if (!j.contains("records") || !j["records"].is_array()) {
            LOG_ERROR("JSON DBC: missing 'records' array");
            return false;
        }

        const auto& records = j["records"];
        if (records.empty()) {
            LOG_WARNING("JSON DBC: empty records array");
            return false;
        }

        fieldCount = j.value("fieldCount", 0u);
        if (fieldCount == 0 && !records[0].empty()) {
            fieldCount = static_cast<uint32_t>(records[0].size());
        }
        if (fieldCount == 0) return false;
        // Sanity caps. Real DBCs cap at ~250 fields and a few million
        // records (Spell.dbc is the biggest at ~50K rows). Multi-million
        // products would OOM the recordData allocation below.
        if (fieldCount > 1024) {
            LOG_ERROR("JSON DBC: fieldCount ", fieldCount, " too large");
            return false;
        }

        recordSize = fieldCount * 4;
        recordCount = static_cast<uint32_t>(records.size());
        if (recordCount > 5'000'000 ||
            static_cast<uint64_t>(recordCount) * recordSize > (256ull << 20)) {
            LOG_ERROR("JSON DBC: recordCount ", recordCount, " * recordSize ",
                      recordSize, " exceeds 256MB cap");
            return false;
        }

        stringBlock.clear();
        stringBlock.push_back(0);

        recordData.resize(static_cast<size_t>(recordCount) * recordSize, 0);

        for (uint32_t i = 0; i < recordCount; i++) {
            const auto& row = records[i];
            // Skip non-array rows (object, string, etc.) - row[col] throws
            // on a non-array, which the outer try-catch would treat as a
            // hard load failure for the whole file. Empty record stays
            // zero-initialized from the resize() above.
            if (!row.is_array()) continue;
            uint32_t* fields = reinterpret_cast<uint32_t*>(
                recordData.data() + static_cast<size_t>(i) * recordSize);

            uint32_t cols = std::min(fieldCount, static_cast<uint32_t>(row.size()));
            for (uint32_t col = 0; col < cols; col++) {
                const auto& val = row[col];
                if (val.is_string()) {
                    const std::string& str = val.get_ref<const std::string&>();
                    // Cap individual string at 4KB and total stringBlock at
                    // 64MB to prevent OOM from a malicious JSON DBC stuffing
                    // huge strings into every field.
                    if (str.empty()) {
                        fields[col] = 0;
                    } else if (str.size() > 4096 ||
                               stringBlock.size() + str.size() > 64ull * 1024 * 1024) {
                        fields[col] = 0;
                    } else {
                        fields[col] = static_cast<uint32_t>(stringBlock.size());
                        stringBlock.insert(stringBlock.end(), str.begin(), str.end());
                        stringBlock.push_back(0);
                    }
                } else if (val.is_number_float()) {
                    float f = val.get<float>();
                    if (!std::isfinite(f)) f = 0.0f;
                    std::memcpy(&fields[col], &f, 4);
                } else if (val.is_number_integer()) {
                    // Range-check: nlohmann throws on out-of-range get<uint32_t>
                    // (negative or > UINT32_MAX). Catching at the field level
                    // keeps a single bad cell from killing the whole DBC load.
                    int64_t raw = val.get<int64_t>();
                    if (raw < 0 || raw > 0xFFFFFFFFll) raw = 0;
                    fields[col] = static_cast<uint32_t>(raw);
                }
            }
        }

        stringBlockSize = static_cast<uint32_t>(stringBlock.size());
        loaded = true;
        idCache_ = std::make_shared<IdCache>();

        LOG_INFO("Loaded JSON DBC: ", recordCount, " records, ",
                 fieldCount, " fields, ", stringBlockSize, " string bytes");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("JSON DBC parse error: ", e.what());
        return false;
    }
}

} // namespace pipeline
} // namespace wowee
