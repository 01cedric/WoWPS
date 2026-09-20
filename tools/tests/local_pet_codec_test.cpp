// The real save/LAN pet codec, rank bar and malformed-wire regressions.
#include "../../src/game/local_realm.cpp"
#include "game/local_pet_bar.hpp"
#include <cassert>
#include <iostream>

using namespace wowee::game;

int main() {
    static_assert(SaveVersion == 31 && lan::GameplayVersion == 86);
    LocalRealmPet pet;
    pet.guid = kLocalPetGuidPrefix | 7;
    pet.ownerGuid = 1; pet.entry = 416; pet.displayId = 4449;
    pet.summonSpellId = 688; pet.kind = LocalPetKind::Controlled; pet.level = 40;
    pet.health = pet.maxHealth = 904; pet.resourceType = 0;
    pet.power = pet.maxPower = 1053; pet.name = "Jakyal";
    pet.x = 100; pet.y = 200; pet.z = 5;
    assert(validLocalPet(pet));

    // Both preferences survive every command, stance and attack-order cell.
    unsigned cells = 0;
    for (const bool autocast : {false, true})
        for (const auto command : {LocalPetCommand::Stay, LocalPetCommand::Follow})
            for (const auto react : {LocalPetReact::Passive, LocalPetReact::Defensive, LocalPetReact::Aggressive})
                for (const bool attacking : {false, true}) {
                    auto live = pet;
                    live.fireboltAutocast = autocast; live.command = command;
                    live.react = react; live.commandAttack = attacking;
                    if (attacking) live.targetGuid = 4242;
                    if (command == LocalPetCommand::Stay) { live.stayX = 110; live.stayY = 210; live.stayZ = 6; }
                    Writer w; writePet(w, live);
                    assert(w.bytes.size() == 112 + 1 + live.name.size());
                    assert(w.bytes.size() <= PetWireBytes);
                    Reader r(w.bytes.data(), w.bytes.size());
                    assert(readPet(r) == live && r.done());
                    ++cells;
                }
    assert(cells == 24);

    Writer baseline; writePet(baseline, pet);
    const size_t autocastOffset = 111;
    auto save30 = baseline.bytes;
    save30.erase(save30.begin() + autocastOffset);
    Reader old30(save30.data(), save30.size());
    const auto restored30 = readPet(old30, 30);
    assert(old30.done() && restored30 == pet && !restored30.fireboltAutocast);
    auto save29 = save30;
    save29.erase(save29.begin() + 96, save29.begin() + 111);
    Reader old29(save29.data(), save29.size());
    assert(readPet(old29, 29) == pet && old29.done());

    for (unsigned bad : {2u, 128u, 255u}) {
        auto bytes = baseline.bytes; bytes[autocastOffset] = bad;
        Reader r(bytes.data(), bytes.size()); readPet(r); assert(!r.valid);
    }
    // A truncated pet deck cannot silently default the new preference.
    for (size_t length = 0; length < baseline.bytes.size(); ++length) {
        Reader r(baseline.bytes.data(), length); readPet(r); assert(!r.done());
    }
    // Four maximum-size names still fit the bounded UDP page.
    pet.name.assign(96, 'x'); Writer maximum; writePet(maximum, pet);
    assert(maximum.bytes.size() == PetWireBytes);
    assert(HeaderSize + 19 + PetsPerPage * maximum.bytes.size() <= MaxPacket);

    const uint8_t levels[] = {1, 8, 18, 28, 38, 48, 58, 68, 78};
    const uint32_t spells[] = {3110, 7799, 7800, 7801, 7802, 11762, 11763, 27267, 47964};
    for (unsigned i = 0; i != 9; ++i) {
        for (bool enabled : {false, true}) {
            const auto slot = localPetActionBarSlot(3, 416, levels[i], enabled);
            assert(pet::petActionId(slot) == spells[i]);
            assert(pet::petActionType(slot) == (enabled ? pet::ActionType::Enabled : pet::ActionType::Disabled));
            assert(pet::populatedPetActionSlot(slot));
        }
        if (i) assert(localPetFireboltSpell(416, levels[i] - 1) == spells[i - 1]);
    }
    assert(!localPetFireboltSpell(417, 80));
    assert(!pet::populatedPetActionSlot(localPetActionBarSlot(3, 417, 80, true)));
    assert(pet::populatedPetActionSlot(pet::defaultPetActionSlot(2))); // Stay = 0.
    assert(pet::populatedPetActionSlot(pet::defaultPetActionSlot(9))); // Passive = 0.
    assert(!pet::populatedPetActionSlot(0));
    assert(!pet::populatedPetActionSlot(0xFF000001));
    std::cout << "PASS pet Save31/LAN86: 24 state cells; Save29/30 migration; malformed/truncated data; "
                 "maximum UDP deck; nine rank bars; zero-ID command buttons\n";
}
