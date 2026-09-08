#include "audio/npc_voice_manager.hpp"
#include "audio/audio_engine.hpp"
#include "audio/sample_load.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/dbc_loader.hpp"
#include "core/logger.hpp"
#include <glm/glm.hpp>

namespace wowee {
namespace audio {

NpcVoiceManager::NpcVoiceManager() : rng_(std::random_device{}()) {}

NpcVoiceManager::~NpcVoiceManager() {
    shutdown();
}

bool NpcVoiceManager::initialize(pipeline::AssetManager* assets) {
    shutdown(); // Rejoining must not append every race bank a second time.
    assetManager_ = assets;
    if (!assetManager_) {
        LOG_WARNING("NPC voice manager: no asset manager");
        return false;
    }

    // Files are .WAV not .OGG in WotLK 3.3.5a!
    LOG_INFO("=== Probing for NPC voice files (.wav format) ===");
    std::vector<std::string> testPaths = {
        "Sound\\Creature\\HumanMaleStandardNPC\\HumanMaleStandardNPCGreetings01.wav",
        "Sound\\Creature\\HumanFemaleStandardNPC\\HumanFemaleStandardNPCGreeting01.wav",
        "Sound\\Creature\\DwarfMaleStandardNPC\\DwarfMaleStandardNPCGreeting01.wav",
        "Sound\\Creature\\OrcMaleStandardNPC\\OrcMaleStandardNPCGreeting01.wav",
    };
    for (const auto& path : testPaths) {
        bool exists = assetManager_->fileExists(path);
        LOG_INFO("  ", path, ": ", (exists ? "EXISTS" : "NOT FOUND"));
    }
    LOG_INFO("===================================");

    loadVoiceSounds();
    loadCreatureAggroSounds();

    int totalSamples = 0;
    for (const auto& [type, samples] : greetingLibrary_) totalSamples += samples.size();
    for (const auto& [type, samples] : farewellLibrary_) totalSamples += samples.size();
    for (const auto& [type, samples] : vendorLibrary_) totalSamples += samples.size();
    for (const auto& [type, samples] : pissedLibrary_) totalSamples += samples.size();
    for (const auto& [type, samples] : aggroLibrary_) totalSamples += samples.size();
    for (const auto& [type, samples] : fleeLibrary_) totalSamples += samples.size();
#ifdef WOWEE_PS4
    LOG_INFO("NPC voice manager initialized (", totalSamples,
             " voice paths; on-demand payloads, retained WAV bytes=0)");
#else
    LOG_INFO("NPC voice manager initialized (", totalSamples, " voice clips)");
#endif
    return true;
}

void NpcVoiceManager::shutdown() {
    greetingLibrary_.clear();
    farewellLibrary_.clear();
    vendorLibrary_.clear();
    pissedLibrary_.clear();
    aggroLibrary_.clear();
    fleeLibrary_.clear();
    creatureAggroSoundByDisplay_.clear();
    creatureAttackSoundByDisplay_.clear();
    creatureWoundSoundByDisplay_.clear();
    creatureDeathSoundByDisplay_.clear();
    resetWorldState();
    assetManager_ = nullptr;
}

void NpcVoiceManager::resetWorldState() {
    missingCombatMappings_.clear();
    lastWoundTime_.clear();
    lastDeathTime_.clear();
    lastPlayTime_.clear();
    lastAggroTime_.clear();
    lastCombatVocalTime_.clear();
    clickCount_.clear();
}

void NpcVoiceManager::loadVoiceSounds() {
    if (!assetManager_) return;

    // Helper to load voice category for a race/gender
    auto loadCategory = [this](
        std::unordered_map<VoiceType, std::vector<VoiceSample>>& library,
        VoiceType type,
        const std::string& npcType,
        const std::string& soundType,
        int count) {

        auto& samples = library[type];
        for (int i = 1; i <= count; ++i) {
            std::string num = (i < 10) ? ("0" + std::to_string(i)) : std::to_string(i);
            std::string path = "Sound\\Creature\\" + npcType + "\\" + npcType + soundType + num + ".wav";
            VoiceSample sample;
            if (loadSound(path, sample)) samples.push_back(std::move(sample));
        }
    };

    // Generic fallback voices (male only)
    auto& genericGreet = greetingLibrary_[VoiceType::GENERIC];
    for (const auto& path : {
        "Sound\\Creature\\DwarfMaleStandardNPC\\DwarfMaleStandardNPCGreeting01.wav",
        "Sound\\Creature\\GnomeMaleStandardNPC\\GnomeMaleStandardNPCGreeting01.wav",
        "Sound\\Creature\\NightElfMaleStandardNPC\\NightElfMaleStandardNPCGreeting01.wav",
        "Sound\\Creature\\OrcMaleStandardNPC\\OrcMaleStandardNPCGreeting01.wav",
    }) {
        VoiceSample sample;
        if (loadSound(path, sample)) genericGreet.push_back(std::move(sample));
    }

    // Load all race/gender combinations
    // Human Male uses "Greetings" (plural), others use "Greeting" (singular)
    loadCategory(greetingLibrary_, VoiceType::HUMAN_MALE, "HumanMaleStandardNPC", "Greetings", 6);
    loadCategory(farewellLibrary_, VoiceType::HUMAN_MALE, "HumanMaleStandardNPC", "Farewell", 5);
    loadCategory(vendorLibrary_, VoiceType::HUMAN_MALE, "HumanMaleStandardNPC", "Vendor", 2);
    loadCategory(pissedLibrary_, VoiceType::HUMAN_MALE, "HumanMaleStandardNPC", "Pissed", 4);

    loadCategory(greetingLibrary_, VoiceType::HUMAN_FEMALE, "HumanFemaleStandardNPC", "Greeting", 5);
    loadCategory(farewellLibrary_, VoiceType::HUMAN_FEMALE, "HumanFemaleStandardNPC", "Farewell", 5);
    loadCategory(vendorLibrary_, VoiceType::HUMAN_FEMALE, "HumanFemaleStandardNPC", "Vendor", 2);
    loadCategory(pissedLibrary_, VoiceType::HUMAN_FEMALE, "HumanFemaleStandardNPC", "Pissed", 4);

    loadCategory(greetingLibrary_, VoiceType::DWARF_MALE, "DwarfMaleStandardNPC", "Greeting", 6);
    loadCategory(farewellLibrary_, VoiceType::DWARF_MALE, "DwarfMaleStandardNPC", "Farewell", 4);
    loadCategory(vendorLibrary_, VoiceType::DWARF_MALE, "DwarfMaleStandardNPC", "Vendor", 2);
    loadCategory(pissedLibrary_, VoiceType::DWARF_MALE, "DwarfMaleStandardNPC", "Pissed", 4);

    loadCategory(greetingLibrary_, VoiceType::GNOME_MALE, "GnomeMaleStandardNPC", "Greeting", 6);
    loadCategory(farewellLibrary_, VoiceType::GNOME_MALE, "GnomeMaleStandardNPC", "Farewell", 5);
    loadCategory(vendorLibrary_, VoiceType::GNOME_MALE, "GnomeMaleStandardNPC", "Vendor", 2);
    loadCategory(pissedLibrary_, VoiceType::GNOME_MALE, "GnomeMaleStandardNPC", "Pissed", 4);

    loadCategory(greetingLibrary_, VoiceType::GNOME_FEMALE, "GnomeFemaleStandardNPC", "Greeting", 6);
    loadCategory(farewellLibrary_, VoiceType::GNOME_FEMALE, "GnomeFemaleStandardNPC", "Farewell", 5);
    loadCategory(vendorLibrary_, VoiceType::GNOME_FEMALE, "GnomeFemaleStandardNPC", "Vendor", 2);
    loadCategory(pissedLibrary_, VoiceType::GNOME_FEMALE, "GnomeFemaleStandardNPC", "Pissed", 4);

    // Goblin Male
    loadCategory(greetingLibrary_, VoiceType::GOBLIN_MALE, "GoblinMaleStandardNPC", "Greeting", 5);
    loadCategory(farewellLibrary_, VoiceType::GOBLIN_MALE, "GoblinMaleStandardNPC", "Farewell", 5);
    loadCategory(vendorLibrary_, VoiceType::GOBLIN_MALE, "GoblinMaleStandardNPC", "Vendor", 2);
    loadCategory(pissedLibrary_, VoiceType::GOBLIN_MALE, "GoblinMaleStandardNPC", "Pissed", 4);

    // Goblin Female
    loadCategory(greetingLibrary_, VoiceType::GOBLIN_FEMALE, "GoblinFemaleStandardNPC", "Greeting", 5);
    loadCategory(farewellLibrary_, VoiceType::GOBLIN_FEMALE, "GoblinFemaleStandardNPC", "Farewell", 5);
    loadCategory(vendorLibrary_, VoiceType::GOBLIN_FEMALE, "GoblinFemaleStandardNPC", "Vendor", 2);
    loadCategory(pissedLibrary_, VoiceType::GOBLIN_FEMALE, "GoblinFemaleStandardNPC", "Pissed", 4);

    loadCategory(greetingLibrary_, VoiceType::NIGHTELF_MALE, "NightElfMaleStandardNPC", "Greeting", 8);
    loadCategory(farewellLibrary_, VoiceType::NIGHTELF_MALE, "NightElfMaleStandardNPC", "Farewell", 7);
    loadCategory(vendorLibrary_, VoiceType::NIGHTELF_MALE, "NightElfMaleStandardNPC", "Vendor", 2);
    loadCategory(pissedLibrary_, VoiceType::NIGHTELF_MALE, "NightElfMaleStandardNPC", "Pissed", 6);

    loadCategory(greetingLibrary_, VoiceType::NIGHTELF_FEMALE, "NightElfFemaleStandardNPC", "Greeting", 6);
    loadCategory(farewellLibrary_, VoiceType::NIGHTELF_FEMALE, "NightElfFemaleStandardNPC", "Farewell", 6);
    loadCategory(vendorLibrary_, VoiceType::NIGHTELF_FEMALE, "NightElfFemaleStandardNPC", "Vendor", 2);
    loadCategory(pissedLibrary_, VoiceType::NIGHTELF_FEMALE, "NightElfFemaleStandardNPC", "Pissed", 6);

    loadCategory(greetingLibrary_, VoiceType::ORC_MALE, "OrcMaleStandardNPC", "Greeting", 5);
    loadCategory(farewellLibrary_, VoiceType::ORC_MALE, "OrcMaleStandardNPC", "Farewell", 5);
    loadCategory(vendorLibrary_, VoiceType::ORC_MALE, "OrcMaleStandardNPC", "Vendor", 2);
    loadCategory(pissedLibrary_, VoiceType::ORC_MALE, "OrcMaleStandardNPC", "Pissed", 4);

    loadCategory(greetingLibrary_, VoiceType::ORC_FEMALE, "OrcFemaleStandardNPC", "Greeting", 6);
    loadCategory(farewellLibrary_, VoiceType::ORC_FEMALE, "OrcFemaleStandardNPC", "Farewell", 6);
    loadCategory(vendorLibrary_, VoiceType::ORC_FEMALE, "OrcFemaleStandardNPC", "Vendor", 2);
    loadCategory(pissedLibrary_, VoiceType::ORC_FEMALE, "OrcFemaleStandardNPC", "Pissed", 6);

    loadCategory(greetingLibrary_, VoiceType::TAUREN_MALE, "TaurenMaleStandardNPC", "Greeting", 5);
    loadCategory(farewellLibrary_, VoiceType::TAUREN_MALE, "TaurenMaleStandardNPC", "Farewell", 5);
    // Tauren Male has no Vendor/Pissed sounds in manifest

    loadCategory(greetingLibrary_, VoiceType::TAUREN_FEMALE, "TaurenFemaleStandardNPC", "Greeting", 5);
    loadCategory(farewellLibrary_, VoiceType::TAUREN_FEMALE, "TaurenFemaleStandardNPC", "Farewell", 5);
    // Tauren Female has no Vendor/Pissed sounds in manifest

    loadCategory(greetingLibrary_, VoiceType::TROLL_MALE, "TrollMaleStandardNPC", "Greeting", 6);
    loadCategory(farewellLibrary_, VoiceType::TROLL_MALE, "TrollMaleStandardNPC", "Farewell", 6);
    loadCategory(vendorLibrary_, VoiceType::TROLL_MALE, "TrollMaleStandardNPC", "Vendor", 2);
    loadCategory(pissedLibrary_, VoiceType::TROLL_MALE, "TrollMaleStandardNPC", "Pissed", 6);

    loadCategory(greetingLibrary_, VoiceType::TROLL_FEMALE, "TrollFemaleStandardNPC", "Greeting", 5);
    loadCategory(farewellLibrary_, VoiceType::TROLL_FEMALE, "TrollFemaleStandardNPC", "Farewell", 6);
    loadCategory(vendorLibrary_, VoiceType::TROLL_FEMALE, "TrollFemaleStandardNPC", "Vendor", 2);
    loadCategory(pissedLibrary_, VoiceType::TROLL_FEMALE, "TrollFemaleStandardNPC", "Pissed", 5);

    loadCategory(greetingLibrary_, VoiceType::UNDEAD_MALE, "UndeadMaleStandardNPC", "Greeting", 6);
    loadCategory(farewellLibrary_, VoiceType::UNDEAD_MALE, "UndeadMaleStandardNPC", "Farewell", 6);
    loadCategory(vendorLibrary_, VoiceType::UNDEAD_MALE, "UndeadMaleStandardNPC", "Vendor", 2);
    loadCategory(pissedLibrary_, VoiceType::UNDEAD_MALE, "UndeadMaleStandardNPC", "Pissed", 6);

    loadCategory(greetingLibrary_, VoiceType::UNDEAD_FEMALE, "UndeadFemaleStandardNPC", "Greeting", 6);
    loadCategory(farewellLibrary_, VoiceType::UNDEAD_FEMALE, "UndeadFemaleStandardNPC", "Farewell", 6);
    loadCategory(vendorLibrary_, VoiceType::UNDEAD_FEMALE, "UndeadFemaleStandardNPC", "Vendor", 2);
    loadCategory(pissedLibrary_, VoiceType::UNDEAD_FEMALE, "UndeadFemaleStandardNPC", "Pissed", 6);

    // Blood Elf Male (TBC+ NPCBloodElfMaleStandard, sparse numbering up to 12)
    loadCategory(greetingLibrary_, VoiceType::BLOODELF_MALE, "NPCBloodElfMaleStandard", "Greeting", 12);
    loadCategory(farewellLibrary_, VoiceType::BLOODELF_MALE, "NPCBloodElfMaleStandard", "Farewell", 12);
    loadCategory(vendorLibrary_, VoiceType::BLOODELF_MALE, "NPCBloodElfMaleStandard", "Vendor", 6);
    loadCategory(pissedLibrary_, VoiceType::BLOODELF_MALE, "NPCBloodElfMaleStandard", "Pissed", 10);

    // Blood Elf Female
    loadCategory(greetingLibrary_, VoiceType::BLOODELF_FEMALE, "NPCBloodElfFemaleStandard", "Greeting", 12);
    loadCategory(farewellLibrary_, VoiceType::BLOODELF_FEMALE, "NPCBloodElfFemaleStandard", "Farewell", 12);
    loadCategory(vendorLibrary_, VoiceType::BLOODELF_FEMALE, "NPCBloodElfFemaleStandard", "Vendor", 6);
    loadCategory(pissedLibrary_, VoiceType::BLOODELF_FEMALE, "NPCBloodElfFemaleStandard", "Pissed", 10);

    // Draenei Male
    loadCategory(greetingLibrary_, VoiceType::DRAENEI_MALE, "NPCDraeneiMaleStandard", "Greeting", 12);
    loadCategory(farewellLibrary_, VoiceType::DRAENEI_MALE, "NPCDraeneiMaleStandard", "Farewell", 12);
    loadCategory(vendorLibrary_, VoiceType::DRAENEI_MALE, "NPCDraeneiMaleStandard", "Vendor", 6);
    loadCategory(pissedLibrary_, VoiceType::DRAENEI_MALE, "NPCDraeneiMaleStandard", "Pissed", 10);

    // Draenei Female
    loadCategory(greetingLibrary_, VoiceType::DRAENEI_FEMALE, "NPCDraeneiFemaleStandard", "Greeting", 12);
    loadCategory(farewellLibrary_, VoiceType::DRAENEI_FEMALE, "NPCDraeneiFemaleStandard", "Farewell", 12);
    loadCategory(vendorLibrary_, VoiceType::DRAENEI_FEMALE, "NPCDraeneiFemaleStandard", "Vendor", 6);
    loadCategory(pissedLibrary_, VoiceType::DRAENEI_FEMALE, "NPCDraeneiFemaleStandard", "Pissed", 10);

    // Load combat sounds from Character vocal files
    // These use a different path structure: Sound\Character\{Race}\{Race}Vocal{Gender}\{Race}{Gender}{Sound}.wav
    auto loadCombatCategory = [this](
        std::unordered_map<VoiceType, std::vector<VoiceSample>>& library,
        VoiceType type,
        const std::string& raceFolder,
        const std::string& raceGender,
        const std::string& soundType,
        int count) {

        auto& samples = library[type];
        for (int i = 1; i <= count; ++i) {
            std::string num = (i < 10) ? ("0" + std::to_string(i)) : std::to_string(i);
            std::string path = "Sound\\Character\\" + raceFolder + "\\" + raceFolder + "Vocal" +
                               (raceGender.find("Male") != std::string::npos ? "Male" : "Female") +
                               "\\" + raceGender + soundType + num + ".wav";
            VoiceSample sample;
            if (loadSound(path, sample)) samples.push_back(std::move(sample));
        }
    };

    // Human combat sounds
    loadCombatCategory(aggroLibrary_, VoiceType::HUMAN_MALE, "Human", "HumanMale", "AttackMyTarget", 2);
    loadCombatCategory(fleeLibrary_, VoiceType::HUMAN_MALE, "Human", "HumanMale", "Flee", 2);

    loadCombatCategory(aggroLibrary_, VoiceType::HUMAN_FEMALE, "Human", "HumanFemale", "AttackMyTarget", 2);
    loadCombatCategory(fleeLibrary_, VoiceType::HUMAN_FEMALE, "Human", "HumanFemale", "Flee", 2);

    // Dwarf combat sounds
    loadCombatCategory(aggroLibrary_, VoiceType::DWARF_MALE, "Dwarf", "DwarfMale", "AttackMyTarget", 3);
    loadCombatCategory(fleeLibrary_, VoiceType::DWARF_MALE, "Dwarf", "DwarfMale", "Flee", 2);

    // Gnome combat sounds
    loadCombatCategory(aggroLibrary_, VoiceType::GNOME_MALE, "Gnome", "GnomeMale", "AttackMyTarget", 2);
    loadCombatCategory(fleeLibrary_, VoiceType::GNOME_MALE, "Gnome", "GnomeMale", "Flee", 2);

    loadCombatCategory(aggroLibrary_, VoiceType::GNOME_FEMALE, "Gnome", "GnomeFemale", "AttackMyTarget", 2);
    loadCombatCategory(fleeLibrary_, VoiceType::GNOME_FEMALE, "Gnome", "GnomeFemale", "Flee", 2);

    // Night Elf combat sounds
    loadCombatCategory(aggroLibrary_, VoiceType::NIGHTELF_MALE, "NightElf", "NightElfMale", "AttackMyTarget", 2);
    loadCombatCategory(fleeLibrary_, VoiceType::NIGHTELF_MALE, "NightElf", "NightElfMale", "Flee", 2);

    loadCombatCategory(aggroLibrary_, VoiceType::NIGHTELF_FEMALE, "NightElf", "NightElfFemale", "AttackMyTarget", 3);
    loadCombatCategory(fleeLibrary_, VoiceType::NIGHTELF_FEMALE, "NightElf", "NightElfFemale", "Flee", 2);

    // Orc combat sounds
    loadCombatCategory(aggroLibrary_, VoiceType::ORC_MALE, "Orc", "OrcMale", "AttackMyTarget", 3);
    loadCombatCategory(fleeLibrary_, VoiceType::ORC_MALE, "Orc", "OrcMale", "Flee", 2);

    loadCombatCategory(aggroLibrary_, VoiceType::ORC_FEMALE, "Orc", "OrcFemale", "AttackMyTarget", 3);
    loadCombatCategory(fleeLibrary_, VoiceType::ORC_FEMALE, "Orc", "OrcFemale", "Flee", 2);

    // Undead combat sounds (Scourge folder)
    loadCombatCategory(aggroLibrary_, VoiceType::UNDEAD_MALE, "Scourge", "UndeadMale", "AttackMyTarget", 2);
    loadCombatCategory(fleeLibrary_, VoiceType::UNDEAD_MALE, "Scourge", "UndeadMale", "Flee", 2);

    loadCombatCategory(aggroLibrary_, VoiceType::UNDEAD_FEMALE, "Scourge", "UndeadFemale", "AttackMyTarget", 2);
    loadCombatCategory(fleeLibrary_, VoiceType::UNDEAD_FEMALE, "Scourge", "UndeadFemale", "Flee", 2);

    // Tauren combat sounds
    loadCombatCategory(aggroLibrary_, VoiceType::TAUREN_MALE, "Tauren", "TaurenMale", "AttackMyTarget", 3);
    loadCombatCategory(fleeLibrary_, VoiceType::TAUREN_MALE, "Tauren", "TaurenMale", "Flee", 2);

    loadCombatCategory(aggroLibrary_, VoiceType::TAUREN_FEMALE, "Tauren", "TaurenFemale", "AttackMyTarget", 3);
    loadCombatCategory(fleeLibrary_, VoiceType::TAUREN_FEMALE, "Tauren", "TaurenFemale", "Flee", 2);

    // Troll combat sounds
    loadCombatCategory(aggroLibrary_, VoiceType::TROLL_MALE, "Troll", "TrollMale", "AttackMyTarget", 3);
    loadCombatCategory(fleeLibrary_, VoiceType::TROLL_MALE, "Troll", "TrollMale", "Flee", 2);

    loadCombatCategory(aggroLibrary_, VoiceType::TROLL_FEMALE, "Troll", "TrollFemale", "AttackMyTarget", 3);
    loadCombatCategory(fleeLibrary_, VoiceType::TROLL_FEMALE, "Troll", "TrollFemale", "Flee", 2);

    // Blood Elf and Draenei combat sounds (flat folder structure, no VocalMale/Female subfolder)
    auto loadCombatFlat = [this](
        std::unordered_map<VoiceType, std::vector<VoiceSample>>& library,
        VoiceType type,
        const std::string& raceFolder,
        const std::string& raceGender,
        const std::string& soundType,
        int count) {

        auto& samples = library[type];
        for (int i = 1; i <= count; ++i) {
            std::string num = (i < 10) ? ("0" + std::to_string(i)) : std::to_string(i);
            std::string path = "Sound\\Character\\" + raceFolder + "\\" + raceGender + soundType + num + ".wav";
            VoiceSample sample;
            if (loadSound(path, sample)) samples.push_back(std::move(sample));
        }
    };

    // Blood Elf combat sounds
    loadCombatFlat(aggroLibrary_, VoiceType::BLOODELF_MALE, "BloodElf", "BloodElfMale", "AttackMyTarget", 3);
    loadCombatFlat(fleeLibrary_, VoiceType::BLOODELF_MALE, "BloodElf", "BloodElfMale", "Flee", 3);

    loadCombatFlat(aggroLibrary_, VoiceType::BLOODELF_FEMALE, "BloodElf", "BloodElfFemale", "AttackMyTarget", 3);
    loadCombatFlat(fleeLibrary_, VoiceType::BLOODELF_FEMALE, "BloodElf", "BloodElfFemale", "Flee", 3);

    // Draenei combat sounds
    loadCombatFlat(aggroLibrary_, VoiceType::DRAENEI_MALE, "Draenei", "DraeneiMale", "AttackMyTarget", 3);
    loadCombatFlat(fleeLibrary_, VoiceType::DRAENEI_MALE, "Draenei", "DraeneiMale", "Flee", 3);

    loadCombatFlat(aggroLibrary_, VoiceType::DRAENEI_FEMALE, "Draenei", "DraeneiFemale", "AttackMyTarget", 3);
    loadCombatFlat(fleeLibrary_, VoiceType::DRAENEI_FEMALE, "Draenei", "DraeneiFemale", "Flee", 3);
}

void NpcVoiceManager::loadCreatureAggroSounds() {
    if (!assetManager_) return;

    auto displayDbc = assetManager_->loadDBC("CreatureDisplayInfo.dbc");
    auto modelDbc = assetManager_->loadDBC("CreatureModelData.dbc");
    auto soundDbc = assetManager_->loadDBC("CreatureSoundData.dbc");
    if (!displayDbc || !displayDbc->isLoaded() ||
        !modelDbc || !modelDbc->isLoaded() ||
        !soundDbc || !soundDbc->isLoaded() || displayDbc->getFieldCount() < 3 ||
        modelDbc->getFieldCount() <= 13 || soundDbc->getFieldCount() <= 10) {
        LOG_WARNING("NPC voice: creature aggro DBC data unavailable");
        return;
    }

    // Fixed-skin creature models (including the Defias HumanThief model) keep
    // their sound set on CreatureModelData, while customizable displays can
    // override it directly in CreatureDisplayInfo. These fields are stable in
    // Classic, TBC, and WotLK.
    std::unordered_map<uint32_t, uint32_t> soundSetByModel;
    for (uint32_t row = 0; row < modelDbc->getRecordCount(); ++row) {
        const uint32_t modelId = modelDbc->getUInt32(row, 0);
        const uint32_t creatureSoundId = modelDbc->getUInt32(row, 13);
        if (modelId != 0 && creatureSoundId != 0)
            soundSetByModel[modelId] = creatureSoundId;
    }

    for (uint32_t row = 0; row < displayDbc->getRecordCount(); ++row) {
        const uint32_t displayId = displayDbc->getUInt32(row, 0);
        const uint32_t modelId = displayDbc->getUInt32(row, 1);
        uint32_t creatureSoundId = displayDbc->getUInt32(row, 2);
        if (creatureSoundId == 0) {
            auto modelSound = soundSetByModel.find(modelId);
            if (modelSound != soundSetByModel.end())
                creatureSoundId = modelSound->second;
        }
        if (displayId == 0 || creatureSoundId == 0) continue;

        const int32_t soundRow = soundDbc->findRecordById(creatureSoundId);
        if (soundRow < 0) continue;
        const uint32_t soundDataRow = static_cast<uint32_t>(soundRow);
        const uint32_t attackSoundId = soundDbc->getUInt32(soundDataRow, 1);
        // CreatureSoundData 3.x: exertion=1, injury=3, death=6, aggro=10.
        const uint32_t woundSoundId = soundDbc->getUInt32(soundDataRow, 3);
        const uint32_t deathSoundId = soundDbc->getUInt32(soundDataRow, 6);
        const uint32_t aggroSoundId = soundDbc->getUInt32(soundDataRow, 10);
        if (woundSoundId != 0) creatureWoundSoundByDisplay_[displayId] = woundSoundId;
        if (deathSoundId != 0) creatureDeathSoundByDisplay_[displayId] = deathSoundId;
        if (attackSoundId != 0)
            creatureAttackSoundByDisplay_[displayId] = attackSoundId;
        if (aggroSoundId != 0)
            creatureAggroSoundByDisplay_[displayId] = aggroSoundId;
    }

    LOG_INFO("NPC voice: loaded ", creatureAggroSoundByDisplay_.size(),
             " model-specific aggro sounds and ",
             creatureAttackSoundByDisplay_.size(), " attack, ",
             creatureWoundSoundByDisplay_.size(), " wound, ",
             creatureDeathSoundByDisplay_.size(), " death vocal banks");
}

bool NpcVoiceManager::playSoundEntry(uint32_t soundId, const glm::vec3& position) {
    if (!assetManager_ || !AudioEngine::instance().isInitialized() || soundId == 0)
        return false;

    auto dbc = assetManager_->loadDBC("SoundEntries.dbc");
    if (!dbc || !dbc->isLoaded()) return false;
    const int32_t rowIndex = dbc->findRecordById(soundId);
    if (rowIndex < 0) return false;

    const uint32_t row = static_cast<uint32_t>(rowIndex);
    const std::string directory = dbc->getString(row, 23);
    std::vector<std::string> paths;
    for (uint32_t field = 3; field <= 12; ++field) {
        const std::string filename = dbc->getString(row, field);
        if (filename.empty()) continue;
        std::string path = directory.empty() ? filename : directory + "\\" + filename;
        if (assetManager_->fileExists(path)) paths.push_back(std::move(path));
    }
    if (paths.empty()) return false;

    std::uniform_int_distribution<size_t> choice(0, paths.size() - 1);
    std::uniform_real_distribution<float> pitch(0.98f, 1.02f);
    return AudioEngine::instance().playSound3D(
        paths[choice(rng_)], position, volumeScale_, pitch(rng_), 60.0f);
}

bool NpcVoiceManager::loadSound(const std::string& path, VoiceSample& sample) {
    if (!assetManager_) return false;

    if (!assetManager_->fileExists(path)) {
        return false;
    }

    sample.path = path;
#ifndef WOWEE_PS4
    sample.data = assetManager_->readFile(path);

    if (sample.data.empty()) {
        LOG_WARNING("NPC voice: Failed to load sound data from ", path);
        return false;
    }
#else
    std::vector<uint8_t>().swap(sample.data);
#endif

    return true;
}

void NpcVoiceManager::playSound(uint64_t npcGuid, VoiceType voiceType, SoundCategory category, const glm::vec3& position) {
    if (!AudioEngine::instance().isInitialized()) {
        return;
    }

    // Check cooldown (except for pissed and combat sounds which override cooldown)
    auto now = std::chrono::steady_clock::now();
    if (category != SoundCategory::PISSED && category != SoundCategory::AGGRO && category != SoundCategory::FLEE) {
        auto it = lastPlayTime_.find(npcGuid);
        if (it != lastPlayTime_.end()) {
            float elapsed = std::chrono::duration<float>(now - it->second).count();
            if (elapsed < GREETING_COOLDOWN) {
                return;
            }
        }
    }

    // Select library based on category
    std::unordered_map<VoiceType, std::vector<VoiceSample>>* library = nullptr;
    switch (category) {
        case SoundCategory::GREETING: library = &greetingLibrary_; break;
        case SoundCategory::FAREWELL: library = &farewellLibrary_; break;
        case SoundCategory::VENDOR: library = &vendorLibrary_; break;
        case SoundCategory::PISSED: library = &pissedLibrary_; break;
        case SoundCategory::AGGRO: library = &aggroLibrary_; break;
        case SoundCategory::FLEE: library = &fleeLibrary_; break;
    }

    // Find voice samples for this type
    auto libIt = library->find(voiceType);
    if (libIt == library->end() || libIt->second.empty()) {
        // Fallback to GENERIC
        libIt = library->find(VoiceType::GENERIC);
        if (libIt == library->end() || libIt->second.empty()) {
            return;
        }
    }

    const auto& samples = libIt->second;
    std::uniform_int_distribution<size_t> dist(0, samples.size() - 1);
    const auto& sample = samples[dist(rng_)];

    // Play sound
    std::uniform_real_distribution<float> pitchDist(0.98f, 1.02f);
    bool success = AudioEngine::instance().playSound3D(
        samplePlaybackSource(sample),
        position,
        1.0f * volumeScale_,
        pitchDist(rng_),
        60.0f,
        // Conversation voices stay intelligible at the normal talk distance.
        // Combat shouts retain their existing spatial attenuation.
        category == SoundCategory::AGGRO || category == SoundCategory::FLEE ? 1.0f : 8.0f
    );

    if (success) {
        lastPlayTime_[npcGuid] = now;
    }
}

void NpcVoiceManager::playGreeting(uint64_t npcGuid, VoiceType voiceType, const glm::vec3& position) {
    // Check if on cooldown - if so, increment pissed counter instead
    auto now = std::chrono::steady_clock::now();
    auto it = lastPlayTime_.find(npcGuid);
    if (it != lastPlayTime_.end()) {
        float elapsed = std::chrono::duration<float>(now - it->second).count();
        if (elapsed < GREETING_COOLDOWN) {
            // On cooldown - increment click count and maybe play pissed sound
            playPissed(npcGuid, voiceType, position);
            return;
        }
    }

    // Reset click count on successful greeting
    clickCount_[npcGuid] = 0;
    playSound(npcGuid, voiceType, SoundCategory::GREETING, position);
}

void NpcVoiceManager::playFarewell(uint64_t npcGuid, VoiceType voiceType, const glm::vec3& position) {
    playSound(npcGuid, voiceType, SoundCategory::FAREWELL, position);
}

void NpcVoiceManager::playVendor(uint64_t npcGuid, VoiceType voiceType, const glm::vec3& position) {
    playSound(npcGuid, voiceType, SoundCategory::VENDOR, position);
}

void NpcVoiceManager::playPissed(uint64_t npcGuid, VoiceType voiceType, const glm::vec3& position) {
    // Increment click count
    clickCount_[npcGuid]++;

    // Only play pissed sound after threshold
    if (clickCount_[npcGuid] >= PISSED_CLICK_THRESHOLD) {
        playSound(npcGuid, voiceType, SoundCategory::PISSED, position);
        clickCount_[npcGuid] = 0;  // Reset after playing
    }
}

void NpcVoiceManager::playAggro(uint64_t npcGuid, uint32_t displayId,
                                VoiceType voiceType, const glm::vec3& position) {
    const auto now = std::chrono::steady_clock::now();
    auto recent = lastAggroTime_.find(npcGuid);
    if (recent != lastAggroTime_.end() &&
        std::chrono::duration<float>(now - recent->second).count() < AGGRO_COOLDOWN) {
        return;
    }

    auto specific = creatureAggroSoundByDisplay_.find(displayId);
    if (specific != creatureAggroSoundByDisplay_.end() &&
        playSoundEntry(specific->second, position)) {
        lastAggroTime_[npcGuid] = now;
        lastPlayTime_[npcGuid] = now;
        return;
    }

    lastAggroTime_[npcGuid] = now;
    playSound(npcGuid, voiceType, SoundCategory::AGGRO, position);
}

void NpcVoiceManager::playCombatAttack(uint64_t npcGuid, uint32_t displayId,
                                       const glm::vec3& position) {
    playCombatEvent(npcGuid, displayId, position, creatureAttackSoundByDisplay_,
                    lastCombatVocalTime_, COMBAT_VOCAL_COOLDOWN, 1, "attack");
}

void NpcVoiceManager::playCombatWound(uint64_t npcGuid, uint32_t displayId,
                                     const glm::vec3& position) {
    playCombatEvent(npcGuid, displayId, position, creatureWoundSoundByDisplay_,
                    lastWoundTime_, 0.6f, 3, "wound");
}

void NpcVoiceManager::playCombatDeath(uint64_t npcGuid, uint32_t displayId,
                                     const glm::vec3& position) {
    playCombatEvent(npcGuid, displayId, position, creatureDeathSoundByDisplay_,
                    lastDeathTime_, 2.0f, 6, "death");
}

void NpcVoiceManager::playCombatEvent(uint64_t npcGuid, uint32_t displayId,
    const glm::vec3& position, const std::unordered_map<uint32_t, uint32_t>& mappings,
    std::unordered_map<uint64_t, std::chrono::steady_clock::time_point>& cooldowns,
    float cooldownSeconds, uint32_t category, const char* label) {
    if (!assetManager_ || !AudioEngine::instance().isInitialized() || volumeScale_ <= 0.0f)
        return;
    const auto now = std::chrono::steady_clock::now();
    const auto recent = cooldowns.find(npcGuid);
    if (recent != cooldowns.end() &&
        std::chrono::duration<float>(now - recent->second).count() < cooldownSeconds) return;
    // Long exploration must not retain one timer per creature ever visited.
    // Pruning only expired entries preserves cooldowns of nearby active fights.
    if (cooldowns.size() >= 4096) {
        for (auto it = cooldowns.begin(); it != cooldowns.end();) {
            if (std::chrono::duration<float>(now - it->second).count() >= cooldownSeconds)
                it = cooldowns.erase(it);
            else ++it;
        }
        if (cooldowns.size() >= 4096) return;
    }
    cooldowns[npcGuid] = now;
    const auto sound = mappings.find(displayId);
    if (sound != mappings.end() && playSoundEntry(sound->second, position)) return;
    const uint64_t key = (uint64_t(displayId) << 32) | category;
    if (missingCombatMappings_.size() < 128 && missingCombatMappings_.insert(key).second)
        LOG_WARNING("NPC voice: unavailable ", label, " for display=", displayId,
                    " soundEntry=", sound == mappings.end() ? 0u : sound->second);
}

} // namespace audio
} // namespace wowee
