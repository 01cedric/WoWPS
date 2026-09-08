#include "pipeline/wowee_expansion_names.hpp"
#include "pipeline/wowee_char_features.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'H', 'F'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wchf";

} // namespace

const WoweeCharFeature::Entry*
WoweeCharFeature::findById(uint32_t featureId) const {
    for (const auto& e : entries)
        if (e.featureId == featureId) return &e;
    return nullptr;
}

const char* WoweeCharFeature::featureKindName(uint8_t k) {
    switch (k) {
        case SkinColor:     return "skin-color";
        case FaceVariation: return "face";
        case HairStyle:     return "hair-style";
        case HairColor:     return "hair-color";
        case FacialHair:    return "facial-hair";
        case FacialColor:   return "facial-color";
        case EarStyle:      return "ear-style";
        case Horns:         return "horns";
        case Markings:      return "markings";
        default:            return "unknown";
    }
}

const char* WoweeCharFeature::sexIdName(uint8_t s) {
    switch (s) {
        case Male:   return "male";
        case Female: return "female";
        default:     return "unknown";
    }
}

const char* WoweeCharFeature::expansionGateName(uint8_t e) {
    // The word is the sidecar's, shared with the other two formats
    // that gate on an expansion and with the importers that read them.
    return expansionName(e);
}

bool WoweeCharFeatureLoader::save(const WoweeCharFeature& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeCharFeature::Entry& e) {
        writePOD(os, e.featureId);
        writePOD(os, e.raceId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.texturePath);
        writePOD(os, e.featureKind);
        writePOD(os, e.sexId);
        writePOD(os, e.variationIndex);
        writePOD(os, e.requiresExpansion);
        writePOD(os, e.geosetGroupBits);
        writePOD(os, e.hairColorOverlayId);
                       });
}

WoweeCharFeature WoweeCharFeatureLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeCharFeature>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeCharFeature::Entry& e) {
        if (!readPOD(is, e.featureId) ||
            !readPOD(is, e.raceId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.texturePath)) { return false; }
        if (!readPOD(is, e.featureKind) ||
            !readPOD(is, e.sexId) ||
            !readPOD(is, e.variationIndex) ||
            !readPOD(is, e.requiresExpansion) ||
            !readPOD(is, e.geosetGroupBits) ||
            !readPOD(is, e.hairColorOverlayId)) { return false; }
                                  return true;
                              });
}

bool WoweeCharFeatureLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeCharFeature WoweeCharFeatureLoader::makeStarter(
    const std::string& catalogName) {
    WoweeCharFeature c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint8_t kind, uint8_t variation,
                    const char* name, const char* tex) {
        WoweeCharFeature::Entry e;
        e.featureId = id;
        e.raceId = 1;        // Human raceId
        e.sexId = WoweeCharFeature::Male;
        e.featureKind = kind;
        e.variationIndex = variation;
        e.name = name;
        e.texturePath = tex;
        e.description = std::string("Human Male - ") +
                         WoweeCharFeature::featureKindName(kind) +
                         " variation " + std::to_string(variation);
        c.entries.push_back(e);
    };
    add(1, WoweeCharFeature::SkinColor,    0, "Skin0",
        "textures/character/Human/Male/HumanMaleSkin00.blp");
    add(2, WoweeCharFeature::FaceVariation, 0, "Face0",
        "textures/character/Human/Male/HumanMaleFace00.blp");
    add(3, WoweeCharFeature::HairStyle,    0, "Hair0",
        "textures/character/Human/Male/HumanMaleHair00.blp");
    add(4, WoweeCharFeature::HairStyle,    1, "Hair1",
        "textures/character/Human/Male/HumanMaleHair01.blp");
    add(5, WoweeCharFeature::FacialHair,   0, "Beard0",
        "textures/character/Human/Male/HumanMaleFacialHair00.blp");
    return c;
}

WoweeCharFeature WoweeCharFeatureLoader::makeBloodElfFemale(
    const std::string& catalogName) {
    WoweeCharFeature c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint8_t variation, const char* name,
                    uint32_t geosets) {
        WoweeCharFeature::Entry e;
        e.featureId = id;
        e.raceId = 10;       // Blood Elf raceId
        e.sexId = WoweeCharFeature::Female;
        e.featureKind = WoweeCharFeature::HairStyle;
        e.variationIndex = variation;
        e.name = name;
        e.requiresExpansion = WoweeCharFeature::TBC;
        e.geosetGroupBits = geosets;
        e.texturePath = std::string("textures/character/BloodElf/Female/") +
                         "BloodElfFemaleHair" +
                         (variation < 10
                            ? std::string("0") + std::to_string(variation)
                            : std::to_string(variation)) +
                         ".blp";
        e.description = std::string("Blood Elf Female hair style ") +
                         std::to_string(variation);
        c.entries.push_back(e);
    };
    // 8 iconic Blood Elf Female hairstyles. geosetGroupBits
    // values are placeholder - real M2 geoset masks come from
    // CharHairGeosets.dbc when ported.
    add(100, 0, "LongStraight",   0x0001);
    add(101, 1, "ShortBob",       0x0002);
    add(102, 2, "PonytailHigh",   0x0004);
    add(103, 3, "BraidedTwin",    0x0008);
    add(104, 4, "ShortPixie",     0x0010);
    add(105, 5, "LongWavy",       0x0020);
    add(106, 6, "TopknotMessy",   0x0040);
    add(107, 7, "SideSwept",      0x0080);
    return c;
}

WoweeCharFeature WoweeCharFeatureLoader::makeTauren(
    const std::string& catalogName) {
    WoweeCharFeature c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint8_t kind, uint8_t variation,
                    const char* name) {
        WoweeCharFeature::Entry e;
        e.featureId = id;
        e.raceId = 6;        // Tauren raceId
        e.sexId = WoweeCharFeature::Male;
        e.featureKind = kind;
        e.variationIndex = variation;
        e.name = name;
        e.texturePath = std::string("textures/character/Tauren/Male/Tauren") +
                         WoweeCharFeature::featureKindName(kind) +
                         "_" + std::to_string(variation) + ".blp";
        e.description = std::string("Tauren Male - ") +
                         WoweeCharFeature::featureKindName(kind) +
                         " variant " + std::to_string(variation);
        c.entries.push_back(e);
    };
    // 3 horn variations + 3 facial hair variations.
    add(200, WoweeCharFeature::Horns,      0, "ShortStraight");
    add(201, WoweeCharFeature::Horns,      1, "LongCurled");
    add(202, WoweeCharFeature::Horns,      2, "BrokenLeft");
    add(203, WoweeCharFeature::FacialHair, 0, "GoateeShort");
    add(204, WoweeCharFeature::FacialHair, 1, "FullBeard");
    add(205, WoweeCharFeature::FacialHair, 2, "HandlebarMustache");
    return c;
}

} // namespace pipeline
} // namespace wowee
