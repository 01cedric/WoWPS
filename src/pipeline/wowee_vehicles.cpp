#include "pipeline/wowee_vehicles.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'V', 'H', 'C'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wvhc";

} // namespace

const WoweeVehicle::Entry*
WoweeVehicle::findById(uint32_t vehicleId) const {
    for (const auto& e : entries) if (e.vehicleId == vehicleId) return &e;
    return nullptr;
}

const char* WoweeVehicle::vehicleKindName(uint8_t k) {
    switch (k) {
        case Mount:         return "mount";
        case Chopper:       return "chopper";
        case Tank:          return "tank";
        case Demolisher:    return "demolisher";
        case Gunship:       return "gunship";
        case FlyingMount:   return "flying-mount";
        case TransportRail: return "transport-rail";
        case SiegeWeapon:   return "siege-weapon";
        default:            return "unknown";
    }
}

const char* WoweeVehicle::movementKindName(uint8_t m) {
    switch (m) {
        case Ground:        return "ground";
        case Air:           return "air";
        case Water:         return "water";
        case Submerged:     return "submerged";
        case AmphibiousAW:  return "air+water";
        case AmphibiousGW:  return "ground+water";
        default:            return "unknown";
    }
}

const char* WoweeVehicle::powerTypeName(uint8_t p) {
    switch (p) {
        case Mana:   return "mana";
        case Energy: return "energy";
        case Pyrite: return "pyrite";
        case Heat:   return "heat";
        case None:   return "none";
        default:     return "unknown";
    }
}

bool WoweeVehicleLoader::save(const WoweeVehicle& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeVehicle::Entry& e) {
        writePOD(os, e.vehicleId);
        writePOD(os, e.creatureId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.vehicleKind);
        writePOD(os, e.movementKind);
        uint8_t seatCount = static_cast<uint8_t>(e.seats.size());
        writePOD(os, seatCount);
        writePadding(os, 1);
        writePOD(os, e.turnSpeed);
        writePOD(os, e.pitchSpeed);
        writePOD(os, e.flightCapabilityId);
        writePOD(os, e.powerType);
        writePadding(os, 3);
        writePOD(os, e.maxPower);
        for (const auto& s : e.seats) {
            writePOD(os, s.seatIndex);
            writePOD(os, s.seatFlags);
            writePOD(os, s.attachmentId);
            uint8_t spad = 0;
            writePOD(os, spad);
            writePOD(os, s.controlSpellId);
            writePOD(os, s.exitSpellId);
            writePOD(os, s.passengerYaw);
        }
                       });
}

WoweeVehicle WoweeVehicleLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeVehicle>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeVehicle::Entry& e) {
        if (!readPOD(is, e.vehicleId) ||
            !readPOD(is, e.creatureId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) { return false; }
        uint8_t seatCount = 0;
        if (!readPOD(is, e.vehicleKind) ||
            !readPOD(is, e.movementKind) ||
            !readPOD(is, seatCount)) { return false; }
        if (!skipPadding(is, 1)) { return false; }
        if (!readPOD(is, e.turnSpeed) ||
            !readPOD(is, e.pitchSpeed) ||
            !readPOD(is, e.flightCapabilityId) ||
            !readPOD(is, e.powerType)) { return false; }
        if (!skipPadding(is, 3)) { return false; }
        if (!readPOD(is, e.maxPower)) { return false; }
        if (seatCount > 64) { return false; }
        e.seats.resize(seatCount);
        for (auto& s : e.seats) {
            if (!readPOD(is, s.seatIndex) ||
                !readPOD(is, s.seatFlags) ||
                !readPOD(is, s.attachmentId)) { return false; }
            uint8_t spad = 0;
            if (!readPOD(is, spad)) { return false; }
            if (!readPOD(is, s.controlSpellId) ||
                !readPOD(is, s.exitSpellId) ||
                !readPOD(is, s.passengerYaw)) { return false; }
        }
                                  return true;
                              });
}

bool WoweeVehicleLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeVehicle WoweeVehicleLoader::makeStarter(const std::string& catalogName) {
    WoweeVehicle c;
    c.name = catalogName;
    {
        // Mechano-Hog: 2-seat ground chopper.
        WoweeVehicle::Entry e;
        e.vehicleId = 1; e.creatureId = 28829;
        e.name = "Mechano-Hog";
        e.description = "Engineering chopper - driver + 1 passenger.";
        e.vehicleKind = WoweeVehicle::Chopper;
        e.movementKind = WoweeVehicle::Ground;
        e.powerType = WoweeVehicle::Energy;
        e.maxPower = 100;
        WoweeVehicle::Seat driver;
        driver.seatIndex = 0; driver.seatFlags = WoweeVehicle::kSeatDriver;
        driver.attachmentId = 0; driver.controlSpellId = 0;
        e.seats.push_back(driver);
        WoweeVehicle::Seat passenger;
        passenger.seatIndex = 1;
        passenger.seatFlags = WoweeVehicle::kSeatPassenger;
        passenger.attachmentId = 1; passenger.controlSpellId = 0;
        e.seats.push_back(passenger);
        c.entries.push_back(e);
    }
    {
        // Wind Rider: 1-seat flying mount with WMNT cross-ref.
        WoweeVehicle::Entry e;
        e.vehicleId = 2; e.creatureId = 1908;
        e.name = "Wind Rider";
        e.description = "Horde flying mount - single rider.";
        e.vehicleKind = WoweeVehicle::FlyingMount;
        e.movementKind = WoweeVehicle::Air;
        // flightCapabilityId 1 matches WMNT.makeStarter mountId.
        e.flightCapabilityId = 1;
        e.powerType = WoweeVehicle::None;
        e.maxPower = 0;
        e.turnSpeed = 4.0f; e.pitchSpeed = 1.5f;
        WoweeVehicle::Seat driver;
        driver.seatIndex = 0;
        driver.seatFlags = WoweeVehicle::kSeatDriver |
                            WoweeVehicle::kSeatHidesPlayer;
        e.seats.push_back(driver);
        c.entries.push_back(e);
    }
    {
        // Salvaged Tank: 1-seat siege ground tank.
        WoweeVehicle::Entry e;
        e.vehicleId = 3; e.creatureId = 33060;
        e.name = "Salvaged Tank";
        e.description = "Ulduar-style siege tank.";
        e.vehicleKind = WoweeVehicle::Tank;
        e.movementKind = WoweeVehicle::Ground;
        e.powerType = WoweeVehicle::Heat;
        e.maxPower = 100;
        WoweeVehicle::Seat driver;
        driver.seatIndex = 0;
        driver.seatFlags = WoweeVehicle::kSeatDriver |
                            WoweeVehicle::kSeatNoEjectByCC;
        driver.controlSpellId = 62286;   // ram ability
        e.seats.push_back(driver);
        c.entries.push_back(e);
    }
    return c;
}

WoweeVehicle WoweeVehicleLoader::makeSiege(const std::string& catalogName) {
    WoweeVehicle c;
    c.name = catalogName;
    auto add = [&](uint32_t vid, uint32_t cid, const char* name,
                    uint8_t kind, uint8_t power,
                    uint32_t controlSp, uint32_t gunnerSp,
                    const char* desc) {
        WoweeVehicle::Entry e;
        e.vehicleId = vid; e.creatureId = cid;
        e.name = name; e.description = desc;
        e.vehicleKind = kind;
        e.movementKind = WoweeVehicle::Ground;
        e.powerType = power;
        e.maxPower = (power == WoweeVehicle::Heat) ? 100 : 50;
        WoweeVehicle::Seat driver;
        driver.seatIndex = 0;
        driver.seatFlags = WoweeVehicle::kSeatDriver;
        driver.controlSpellId = controlSp;
        e.seats.push_back(driver);
        if (gunnerSp != 0) {
            WoweeVehicle::Seat gunner;
            gunner.seatIndex = 1;
            gunner.seatFlags = WoweeVehicle::kSeatGunner;
            gunner.attachmentId = 1;
            gunner.controlSpellId = gunnerSp;
            e.seats.push_back(gunner);
        }
        c.entries.push_back(e);
    };
    add(100, 28593, "Demolisher",
        WoweeVehicle::Demolisher, WoweeVehicle::Pyrite,
        50990, 50652, "2-seat catapult - driver steers, "
                       "gunner launches boulders.");
    add(101, 28781, "Glaive Thrower",
        WoweeVehicle::SiegeWeapon, WoweeVehicle::Pyrite,
        53908, 0,    "Single-seat ballista - fires armor-piercing "
                       "glaives.");
    add(102, 33113, "Salvaged Cannon",
        WoweeVehicle::SiegeWeapon, WoweeVehicle::Heat,
        62307, 0,    "Stationary cannon - overheats on rapid fire.");
    return c;
}

WoweeVehicle WoweeVehicleLoader::makeFlying(const std::string& catalogName) {
    WoweeVehicle c;
    c.name = catalogName;
    auto add = [&](uint32_t vid, uint32_t cid, const char* name,
                    uint32_t flightCap, uint8_t passengerSeats,
                    const char* desc) {
        WoweeVehicle::Entry e;
        e.vehicleId = vid; e.creatureId = cid;
        e.name = name; e.description = desc;
        e.vehicleKind = WoweeVehicle::FlyingMount;
        e.movementKind = WoweeVehicle::Air;
        e.flightCapabilityId = flightCap;
        e.powerType = WoweeVehicle::None;
        e.turnSpeed = 4.0f; e.pitchSpeed = 1.5f;
        WoweeVehicle::Seat driver;
        driver.seatIndex = 0;
        driver.seatFlags = WoweeVehicle::kSeatDriver |
                            WoweeVehicle::kSeatHidesPlayer;
        e.seats.push_back(driver);
        for (uint8_t k = 0; k < passengerSeats; ++k) {
            WoweeVehicle::Seat p;
            p.seatIndex = static_cast<uint8_t>(k + 1);
            p.seatFlags = WoweeVehicle::kSeatPassenger;
            p.attachmentId = static_cast<uint8_t>(k + 1);
            e.seats.push_back(p);
        }
        c.entries.push_back(e);
    };
    // flightCapabilityIds 1 / 2 / 3 match WMNT.makeStarter
    // mountIds (Wind Rider / Gryphon / Drake).
    add(200, 1908,  "Wind Rider",     1, 0,
        "Single-seat horde flying mount.");
    add(201, 478,   "Storm Gryphon",  2, 0,
        "Single-seat alliance flying mount.");
    add(202, 30414, "Twilight Drake", 3, 1,
        "2-seat drake - driver + 1 passenger.");
    return c;
}

} // namespace pipeline
} // namespace wowee
