#include "pipeline/wowee_localization.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'L', 'A', 'N'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wlan";

} // namespace

const WoweeLocalization::Entry*
WoweeLocalization::findById(uint32_t stringId) const {
    for (const auto& e : entries)
        if (e.stringId == stringId) return &e;
    return nullptr;
}

const WoweeLocalization::Entry*
WoweeLocalization::findOverride(const std::string& originalKey,
                                  uint8_t languageCode,
                                  uint8_t namespaceKind) const {
    for (const auto& e : entries) {
        if (e.languageCode == languageCode &&
            e.namespace_ == namespaceKind &&
            e.originalKey == originalKey) {
            return &e;
        }
    }
    return nullptr;
}

std::vector<const WoweeLocalization::Entry*>
WoweeLocalization::findByLanguage(uint8_t languageCode) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.languageCode == languageCode) out.push_back(&e);
    return out;
}

bool WoweeLocalizationLoader::save(const WoweeLocalization& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeLocalization::Entry& e) {
        writePOD(os, e.stringId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.languageCode);
        writePOD(os, e.namespace_);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writeStr(os, e.originalKey);
        writeStr(os, e.localizedText);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeLocalization WoweeLocalizationLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeLocalization>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeLocalization::Entry& e) {
        if (!readPOD(is, e.stringId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.languageCode) ||
            !readPOD(is, e.namespace_) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1)) { return false; }
        if (!readStr(is, e.originalKey) ||
            !readStr(is, e.localizedText)) { return false; }
        if (!readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeLocalizationLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeLocalization WoweeLocalizationLoader::makeUIBasics(
    const std::string& catalogName) {
    using L = WoweeLocalization;
    WoweeLocalization c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t lang, const char* key,
                    const char* localized,
                    const char* desc) {
        L::Entry e;
        e.stringId = id; e.name = name; e.description = desc;
        e.languageCode = lang;
        e.namespace_ = L::UI;
        e.originalKey = key;
        e.localizedText = localized;
        e.iconColorRGBA = packRgba(140, 200, 255);   // ui blue
        c.entries.push_back(e);
    };
    // Translations of the "Cancel" button across 5
    // languages - common UI string used in every dialog
    // box.
    add(1, "Cancel_deDE", L::deDE,
        "Cancel", "Abbrechen",
        "German UI: 'Cancel' button.");
    add(2, "Cancel_frFR", L::frFR,
        "Cancel", "Annuler",
        "French UI: 'Cancel' button.");
    add(3, "Cancel_esES", L::esES,
        "Cancel", "Cancelar",
        "Spanish UI: 'Cancel' button.");
    add(4, "Cancel_koKR", L::koKR,
        "Cancel", "취소",
        "Korean UI: 'Cancel' button. Multibyte UTF-8 "
        "round-trip preserved.");
    add(5, "Cancel_zhCN", L::zhCN,
        "Cancel", "取消",
        "Simplified Chinese UI: 'Cancel' button.");
    return c;
}

WoweeLocalization WoweeLocalizationLoader::makeQuestSample(
    const std::string& catalogName) {
    using L = WoweeLocalization;
    WoweeLocalization c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t lang, const char* key,
                    const char* localized,
                    const char* desc) {
        L::Entry e;
        e.stringId = id; e.name = name; e.description = desc;
        e.languageCode = lang;
        e.namespace_ = L::Quest;
        e.originalKey = key;
        e.localizedText = localized;
        e.iconColorRGBA = packRgba(220, 220, 100);   // quest gold
        c.entries.push_back(e);
    };
    // One quest title in 3 languages - illustrates the
    // dotted-key convention "QUEST.123.title".
    add(100, "Quest123Title_deDE", L::deDE,
        "QUEST.123.title",
        "Die Verwüsteten Lande",
        "Quest 123 title in German - placeholder "
        "translation of 'The Blasted Lands'.");
    add(101, "Quest123Title_frFR", L::frFR,
        "QUEST.123.title",
        "Les Terres foudroyees",
        "Quest 123 title in French - note: ASCII-only "
        "approximation of 'foudroyées' to keep this "
        "source file ASCII-clean.");
    add(102, "Quest123Title_koKR", L::koKR,
        "QUEST.123.title",
        "황폐의 땅",
        "Quest 123 title in Korean.");
    return c;
}

WoweeLocalization WoweeLocalizationLoader::makeTooltipSet(
    const std::string& catalogName) {
    using L = WoweeLocalization;
    WoweeLocalization c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t lang, const char* key,
                    const char* localized,
                    const char* desc) {
        L::Entry e;
        e.stringId = id; e.name = name; e.description = desc;
        e.languageCode = lang;
        e.namespace_ = L::Tooltip;
        e.originalKey = key;
        e.localizedText = localized;
        e.iconColorRGBA = packRgba(180, 220, 180);   // tooltip green
        c.entries.push_back(e);
    };
    // Item tooltip strings - high-volume client
    // localization use case. 4 strings × 2 languages
    // = 4 entries (deDE: 2, frFR: 2).
    add(200, "BindOnPickup_deDE", L::deDE,
        "TOOLTIP.BindOnPickup",
        "Bei Aufnahme gebunden",
        "Tooltip line: 'Bind on Pickup' in German. "
        "Common to most epic items.");
    add(201, "BindOnPickup_frFR", L::frFR,
        "TOOLTIP.BindOnPickup",
        "Lie quand ramasse",
        "Tooltip line: 'Bind on Pickup' in French. "
        "ASCII approximation.");
    add(202, "Unique_deDE", L::deDE,
        "TOOLTIP.Unique",
        "Einzigartig",
        "Tooltip line: 'Unique' (cannot equip more "
        "than one) in German.");
    add(203, "Unique_frFR", L::frFR,
        "TOOLTIP.Unique",
        "Unique",
        "Tooltip line: 'Unique' in French (same word, "
        "different pronunciation).");
    return c;
}

} // namespace pipeline
} // namespace wowee
