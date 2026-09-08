#include "pipeline/wowee_item_materials.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'M', 'A', 'T'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wmat";

} // namespace

const WoweeItemMaterial::Entry*
WoweeItemMaterial::findById(uint32_t materialId) const {
    for (const auto& e : entries)
        if (e.materialId == materialId) return &e;
    return nullptr;
}

const char* WoweeItemMaterial::materialKindName(uint8_t k) {
    switch (k) {
        case Cloth:    return "cloth";
        case Leather:  return "leather";
        case Mail:     return "mail";
        case Plate:    return "plate";
        case Wood:     return "wood";
        case Stone:    return "stone";
        case Metal:    return "metal";
        case Liquid:   return "liquid";
        case Organic:  return "organic";
        case Crystal:  return "crystal";
        case Ethereal: return "ethereal";
        case Hide:     return "hide";
        default:       return "unknown";
    }
}

const char* WoweeItemMaterial::weightCategoryName(uint8_t w) {
    switch (w) {
        case Light:  return "light";
        case Medium: return "medium";
        case Heavy:  return "heavy";
        default:     return "unknown";
    }
}

bool WoweeItemMaterialLoader::save(const WoweeItemMaterial& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeItemMaterial::Entry& e) {
        writePOD(os, e.materialId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.materialKind);
        writePOD(os, e.weightCategory);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.foleySoundId);
        writePOD(os, e.impactSoundId);
        writePOD(os, e.materialFlags);
        writePOD(os, e.iconColorRGBA);
                       });
}

WoweeItemMaterial WoweeItemMaterialLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeItemMaterial>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeItemMaterial::Entry& e) {
        if (!readPOD(is, e.materialId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        if (!readPOD(is, e.materialKind) ||
            !readPOD(is, e.weightCategory) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.foleySoundId) ||
            !readPOD(is, e.impactSoundId) ||
            !readPOD(is, e.materialFlags) ||
            !readPOD(is, e.iconColorRGBA)) { return false; }
                                  return true;
                              });
}

bool WoweeItemMaterialLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeItemMaterial WoweeItemMaterialLoader::makeArmor(
    const std::string& catalogName) {
    using M = WoweeItemMaterial;
    WoweeItemMaterial c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t weight, uint32_t foley, uint32_t impact,
                    uint32_t flags, uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        M::Entry e;
        e.materialId = id; e.name = name; e.description = desc;
        e.materialKind = kind;
        e.weightCategory = weight;
        e.foleySoundId = foley;
        e.impactSoundId = impact;
        e.materialFlags = flags;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    // foley/impact sound ids reference WSND entries; using
    // 1xx range for foley + 2xx for impact as illustrative
    // anchors that engine projects override.
    add(1, "Cloth",   M::Cloth,   M::Light,  101, 201,
        M::IsFlammable,
        220, 220, 200, "Cloth - light, flammable, no impact sound.");
    add(2, "Leather", M::Leather, M::Light,  102, 202, 0,
        160, 100,  60,
        "Leather - light, supple, dull thud on impact.");
    add(3, "Mail",    M::Mail,    M::Medium, 103, 203,
        M::IsConductive,
        180, 180, 200, "Mail - medium, metallic ring, conducts lightning.");
    add(4, "Plate",   M::Plate,   M::Heavy,  104, 204,
        M::IsConductive,
        220, 220, 230,
        "Plate - heavy, loud clang, conducts lightning.");
    add(5, "Hide",    M::Hide,    M::Medium, 105, 205,
        M::IsFlammable,
        140,  90,  50,
        "Hide - raw furred hide, medium weight, flammable.");
    return c;
}

WoweeItemMaterial WoweeItemMaterialLoader::makeWeapon(
    const std::string& catalogName) {
    using M = WoweeItemMaterial;
    WoweeItemMaterial c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t weight, uint32_t foley, uint32_t impact,
                    uint32_t flags, const char* desc) {
        M::Entry e;
        e.materialId = id; e.name = name; e.description = desc;
        e.materialKind = kind;
        e.weightCategory = weight;
        e.foleySoundId = foley;
        e.impactSoundId = impact;
        e.materialFlags = flags;
        e.iconColorRGBA = packRgba(180, 180, 200);   // weapon-steel grey
        c.entries.push_back(e);
    };
    add(100, "Wood",            M::Wood,  M::Light,  110, 210,
        M::IsBreakable | M::IsFlammable,
        "Wood - staves and bows. Breakable + flammable.");
    add(101, "Steel",           M::Metal, M::Medium, 111, 211,
        M::IsConductive,
        "Steel - vendor-buy weapons. Conducts lightning.");
    add(102, "Mithril",         M::Metal, M::Medium, 112, 212,
        M::IsConductive,
        "Mithril - mid-tier weapons (40-50). Lighter than steel.");
    add(103, "Adamantite",      M::Metal, M::Medium, 113, 213,
        M::IsConductive,
        "Adamantite - endgame raw material (TBC-era). Tough metal.");
    add(104, "EnchantedSteel",  M::Metal, M::Medium, 114, 214,
        M::IsConductive | M::IsMagical,
        "Enchanted steel - magical raid weapons. Glows + conducts.");
    return c;
}

WoweeItemMaterial WoweeItemMaterialLoader::makeMagical(
    const std::string& catalogName) {
    using M = WoweeItemMaterial;
    WoweeItemMaterial c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint32_t flags, uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        M::Entry e;
        e.materialId = id; e.name = name; e.description = desc;
        e.materialKind = kind;
        e.weightCategory = M::Light;     // magical things are weightless
        e.materialFlags = flags;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(200, "Crystal",     M::Crystal,
        M::IsMagical | M::IsBreakable,
        180, 220, 240,
        "Crystal - magical, breakable, refracts light.");
    add(201, "Ethereal",    M::Ethereal,
        M::IsMagical,
        200, 200, 240,
        "Ethereal - ghostly weightless material.");
    add(202, "CursedBone",  M::Organic,
        M::IsCursed,
        100,  60,  60,
        "Cursed bone - applies a debuff to wearer.");
    add(203, "HolyForged",  M::Metal,
        M::IsMagical | M::IsHolyCharged,
        240, 240, 200,
        "Holy-forged steel - damages undead on contact.");
    return c;
}

} // namespace pipeline
} // namespace wowee
