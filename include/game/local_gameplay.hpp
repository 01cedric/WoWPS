#pragma once
#include "game/local_collision.hpp"
#include "game/local_combat_events.hpp"
#include "game/local_runes.hpp"
#include "game/local_equipment.hpp"
#include "game/local_travel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace wowee::game {
/// P05 line of sight . Defined in game/local_line_of_sight.hpp, which the
/// authority's own translation unit includes; every other consumer of this
/// header only ever sees the pointer.
class LocalCollisionData;
class LocalWorldCatalog;
class LocalVendorInventory;
enum class LocalResourceType : uint8_t { Mana = 0, Rage = 1, Energy = 3, RunicPower = 6 };
struct LocalFactionTemplate {
    uint32_t id = 0, faction = 0, flags = 0, factionGroup = 0, friendGroup = 0, enemyGroup = 0;
    std::array<uint32_t, 4> enemies{}, friends{};
};
struct LocalAreaTriggerVolume {
    uint32_t id = 0, mapId = 0;
    // Raw/server coordinates from AreaTrigger.dbc, not render-space coordinates.
    float x = 0, y = 0, z = 0, radius = 0;
    float boxLength = 0, boxWidth = 0, boxHeight = 0, boxYaw = 0;
};
/// One SkillLine.dbc row, as the local realm needs it. Category 9 is a
/// secondary skill and 11 a primary profession; nothing else is trainable here.
struct LocalSkillLine {
    uint32_t id = 0, category = 0;
    std::string name;
};
inline constexpr uint32_t kLocalSkillCategorySecondary = 9;
inline constexpr uint32_t kLocalSkillCategoryProfession = 11;
struct LocalRealmPortal {
    uint32_t id = 0, sourceMapId = 0, targetMapId = 0;
    std::string name;
    bool instanceMap = false;
    // Map.dbc InstanceType: 0 world, 1 party dungeon, 2 raid, 3 battleground,
    // 4 arena. Zero when the client has no row for the target map, in which
    // case instanceMap rests on the catalog alone as it always did.
    uint32_t instanceType = 0, maxPlayers = 0;
};
/// One row of the client's Map.dbc, as the local realm needs it. This is what
/// makes a dungeon or raid entrance usable on the client's own authority rather
/// than only when the world catalog happens to carry the map.
struct LocalMapDefinition {
    uint32_t id = 0, instanceType = 0, maxPlayers = 0, expansion = 0;
    std::string name;
    bool instance() const { return instanceType == 1 || instanceType == 2; }
};
struct LocalInstanceState {
    uint32_t id = 0, mapId = 0;
    // 0: legacy shared; human GUID: solo private; high-bit key: a session
    // party's durable instance owner. This is not the transient party ID.
    uint64_t groupId = 0;
};

struct LocalItemStack { uint32_t itemId = 0; uint16_t count = 0; uint8_t bagSlot=255; bool operator==(const LocalItemStack&) const = default; };
inline constexpr size_t kLocalBankSlots = 28;
struct LocalMerchantBuyback {
    uint32_t id = 0, itemId = 0, price = 0;
    uint16_t count = 0;
    bool operator==(const LocalMerchantBuyback&) const = default;
};
inline constexpr size_t kLocalMaxBuyback = 12;
/// Depleted physical merchant offer. Restocking uses active simulation time;
/// offline time does not replenish stock. Only the partial interval is saved.
struct LocalVendorStockRecord {
    uint64_t npcGuid = 0;
    uint32_t entry = 0, itemId = 0, remaining = 0;
    uint64_t elapsedMs = 0;
    bool operator==(const LocalVendorStockRecord&) const = default;
};
/// A profession this character has learned. `skillId` is a SkillLine.dbc id,
/// `max` the rank cap a trainer has sold them, `current` the points earned
/// towards it. Points come from crafting; `progress` is the fraction of the
/// next point already earned, in thousandths - see localCraftSkillChance()
/// about why this realm accumulates the client's own skill-up chance rather
/// than rolling it.
inline constexpr size_t kLocalMaxStatAuras = 16;
inline constexpr size_t kLocalMaxHealingAuraViews=8;
struct LocalHealingAuraView {
    uint32_t spellId=0,remainingMs=0,durationMs=0;
    uint64_t casterGuid=0;
    uint8_t stacks=1;
    bool operator==(const LocalHealingAuraView&) const = default;
};
/// What an owner projects: one indefinite raid area aura. There is no timer
/// here by construction - every consumer of LocalStatAura::remainingMs treats it
/// as a decrementing lease, and an indefinite emitter has no lease. Rules,
/// bounds and source anchors live in include/game/local_area_aura.hpp.
struct LocalAreaAuraEmitter {
    uint32_t spellId=0, mapId=0, instanceId=0;
    /// Effect-zero amount, computed once through the original caster at
    /// activation, as AuraEffect::CalculateAmount does.
    uint32_t amount=0;
    /// Which of the three source effects this emitter carries. Zero-amount
    /// marker effects are carried and contribute nothing, which is what the
    /// reference does; it is not a claim that their aura types are implemented.
    uint8_t effectMask=0;
    /// Authority-only identity of one activation; never serialized.
    uint64_t generation=0;
    bool operator==(const LocalAreaAuraEmitter&) const = default;
};
/// What a recipient holds. Derived, rebuilt by the authority, never saved and
/// never accepted from a client. `effective` false is the reference's
/// "application kept, effects stripped" state (SpellAuras.cpp:621-625), which
/// exists so a dominant neighbour can still be found.
struct LocalAreaAuraApplication {
    uint32_t spellId=0, mapId=0, instanceId=0;
    uint64_t emitterGuid=0, emitterGeneration=0;
    uint32_t amount=0;
    uint8_t effectMask=0;
    bool effective=false;
    bool operator==(const LocalAreaAuraApplication&) const = default;
};
struct LocalStatAura {
    uint32_t spellId=0, remainingMs=0, mapId=0, instanceId=0;
    uint64_t casterGuid=0; // zero denotes legacy/self caster
    uint32_t absorbRemaining=0;
    uint8_t stacks=1;
    uint8_t procCharges=0;
    uint32_t procCooldownMs=0, manaRegenRemainder=0;
    uint32_t procAmountSnapshot=0;
    bool hasProcAmountSnapshot=false;
    uint32_t buffArmorSnapshot=0; // Caster-modified armor; zero reads legacy unmodified amount.
    uint64_t costModGeneration=0; // Session-only identity; a refresh cannot spend an older cast reservation.
    uint16_t reflectChanceBasisPointsSnapshot=0; // Ward effect1 at application; persisted.
    uint64_t applicationGeneration=0; // Authority-only identity of one aura application; never serialized.
    bool operator==(const LocalStatAura&) const = default;
};
struct LocalProfessionSkill { uint16_t skillId = 0, current = 0, max = 0, progress = 0; };
struct LocalRecipeAccess {
    uint32_t races = 0, classes = 0, excludedRaces = 0, excludedClasses = 0;
};
/// One trade-skill recipe, joined from the client's own SkillLineAbility.dbc
/// and Spell.dbc. Nothing here is authored: the reagents, the item produced and
/// the skill thresholds are all columns of the player's own data files.
struct LocalRecipe {
    uint32_t spellId = 0;
    uint16_t skillId = 0;
    /// SkillLineAbility.MinSkillLineRank: the skill needed to learn it.
    uint16_t requiredSkill = 0;
    /// TrivialSkillLineRankHigh/Low: the skill at which this recipe stops
    /// granting points (grey) and where it starts to taper (green).
    uint16_t trivialHigh = 0, trivialLow = 0;
    uint32_t createdItemId = 0;
    uint16_t createdCount = 1;
    std::string name;
    /// Spell.dbc Reagent[8]/ReagentCount[8], as many as the recipe declares.
    std::vector<LocalItemStack> reagents;
    // Non-consumed Spell.dbc Totem item requirements. Tool-category and world
    // focus rules are separate and are not represented by these exact IDs.
    std::array<uint32_t, 2> tools{};
    // Alternative SkillLineAbility race/class restrictions for this skill.
    std::vector<LocalRecipeAccess> access;
    // Keep previously learned recipe IDs loadable even when a source rule
    // cannot be executed. Such a recipe is visible but cannot be sold/crafted.
    std::string unsupportedReason;
};
struct LocalCooldown { uint32_t spellId = 0; uint32_t remainingMs = 0; };
inline constexpr size_t kLocalMaxCategoryCooldowns=16;
struct LocalCategoryCooldown { uint32_t category=0,family=0,remainingMs=0; };
enum class LocalCastStatus : uint8_t { None = 0, Casting, Finished, Interrupted, Failed };
enum class LocalQuestStatus : uint8_t { Active = 0, Complete = 1, Rewarded = 2 };
struct LocalQuestProgress {
    uint32_t id = 0;
    LocalQuestStatus status = LocalQuestStatus::Active;
    std::vector<uint16_t> progress;
};
struct LocalRealmPlayer {
    uint64_t guid = 0;
    std::string name;
    uint32_t mapId = 0, instanceId = 0;
    LocalResourceType resourceType = LocalResourceType::Mana;
    float x = -8949.95f, y = -132.493f, z = 83.5312f, orientation = 0;
    uint8_t race = 1, classId = 1, gender = 0, level = 1;
    // Appearance chosen at character creation (CharSections indices). Saved
    // with the player from save version 4 on; older saves read as zeros.
    uint8_t skin = 0, face = 0, hairStyle = 0, hairColor = 0, facialHair = 0;
    bool useFemaleModel = false;
    uint32_t health = 100, maxHealth = 100, mana = 100, maxMana = 100;
    uint32_t xp = 0, xpToLevel = 400, money = 0, positionRevision = 0;
    uint32_t formSpellId=0,druidMana=0,druidManaRemainder=0; // Save25: active form and hidden mana.
    bool dead = false;
    // Save32/wire87: the corpse stays at the death location while its owner
    // moves as a ghost. Health remains zero until authoritative reclaim.
    bool ghost = false, corpseValid = false;
    uint32_t corpseMapId = 0, corpseInstanceId = 0, corpseZoneId = 0;
    uint32_t zoneId = 0; // Terrain-derived local zone; unknown for remote actors.
    float corpseX = 0, corpseY = 0, corpseZ = 0, corpseOrientation = 0;
    // Legacy saves skip onboarding; createPlayer opts new identities into it.
    bool introSeen = true;
    std::vector<LocalItemStack> inventory;
    std::array<LocalItemStack, kLocalBankSlots> bank{}; // Stable personal bank slots; no purchased bags yet.
    // The most recent twelve sales, newest first. IDs never shift when an
    // older row is bought back, so a delayed LAN command cannot buy another row.
    uint32_t buybackSerial = 0;
    std::vector<LocalMerchantBuyback> buyback;
    // Canonical nineteen worn slots. Each reference reserves one inventory
    // copy; equipped items remain in inventory, including paired duplicates.
    std::array<uint32_t, kLocalEquipmentSlotCount> equipment{};
    // Only accepted, unrewarded quests occupy the bounded active log.
    std::vector<LocalQuestProgress> quests;
    // Strictly increasing unique IDs; objective data is no longer needed after
    // rewarding. Kept separate so completed quests never occupy active slots.
    std::vector<uint32_t> completedQuestIds;
    std::vector<uint32_t> knownSpells;
    std::vector<LocalHealingAuraView> healingAuras; // transient presentation only; never saved
    std::vector<LocalStatAura> statAuras; // timed recipient effects; shield capacity is bounded against content
    // What this character projects, and what it currently receives. The emitter
    // is saved; the derived applications are rebuilt by the authority and never
    // saved or accepted from a client. See include/game/local_area_aura.hpp.
    std::vector<LocalAreaAuraEmitter> areaEmitters;
    std::vector<LocalAreaAuraApplication> areaAuras;
    std::vector<std::pair<uint32_t,uint8_t>> talents; // talent ID and learned rank (1..5), at most 71 points

    // Trade-skill recipes this character has been taught. Separate from
    // knownSpells because a recipe is not cast at anything: it is crafted, and
    // the rules that gate it are skill points rather than mana and range.
    std::vector<uint32_t> knownRecipes;
    std::vector<LocalCooldown> cooldowns;
    std::vector<LocalCategoryCooldown> categoryCooldowns;
    bool migrateLegacyCooldowns=false; // Preserved until imported category metadata is available.
    // Six independent base-rune timers, authority-owned; save format 9.
    LocalRuneCooldowns runeCooldownMs{};
    // Owner-only cast progress; active casts are cancelled on load/reconnect.
    uint32_t castingSpellId = 0, castRemainingMs = 0, castTotalMs = 0, globalCooldownMs = 0;
    uint32_t castSequence = 0, castPushbackMs = 0; // Session-only owner presentation; never saved.
    uint64_t nextCostModGeneration=0, castCostModGeneration=0;
    uint32_t castPreparedCost=0, castCostModSpellId=0;
    bool castCostPrepared=false; // Prepare-time power cost is retained through expiry/refresh. Never saved.
    uint8_t castPushbackCount = 0; // Authority-only, at most two damaging hits per cast.
    uint64_t castTarget = 0;
    LocalCastStatus castStatus = LocalCastStatus::None;
    // Owner-only, session-local successful cast events, including instant spells
    // with no cooldown. Failed/interrupted casts never advance this serial.
    uint32_t castRevision = 0, lastCastSpellId = 0;
    uint64_t lastCastTarget = 0;
    // Authority-only origin used to interrupt movement and map/instance travel.
    float castOriginX = 0, castOriginY = 0, castOriginZ = 0;
    uint32_t castOriginMap = 0, castOriginInstance = 0;
    // Runtime values are kept on the authority; cooldowns are persisted.
    uint64_t attackTarget = 0;
    // Encounter-local combo state. Only target/points are sent to the owner;
    // the authority epoch prevents reuse after NPC reset or respawn.
    uint64_t comboTarget=0;
    uint64_t comboTargetEpoch=0;uint32_t comboPositionRevision=0;
    uint8_t comboPoints=0;
    float attackTimer = 0, deadTimer = 0;
    float offHandTimer=0;
    float meleePeriodMain=0,meleePeriodOff=0; // Authority-only period used to preserve swing progress across aura changes.
    uint32_t meleeWeaponMain=0,meleeWeaponOff=0,meleeForm=0;
    std::array<LocalMeleeView,4> meleeViews{};uint32_t meleeSerial=0,meleeViewPositionRevision=0; // Owner-only transient presentation window.
    uint32_t druidManaCapacity=0; // Derived maximum caster mana, owner wire only; rebuilt on load.
    uint32_t regenerationTickMs=0; // Authority-only two-second health/decay cadence.
    uint32_t manaRegenSubMilli=0; // Authority-only millionths; saved thousandths stay compatible.
    uint32_t manaRegenDelayMs=0,resourceRegenRemainder=0; // Save24: delay and thousandths of a resource unit.
    bool gameplayInitialized = false;
    uint32_t rangedAutoSpellId=0,rangedWeapon=0,rangedRemainingMs=0;
    uint64_t rangedTarget=0;
    float rangedOriginX=0,rangedOriginY=0,rangedOriginZ=0;
    uint32_t rangedOriginRevision=0;
    uint32_t returnMapId = 0, returnInstanceId = 0;
    float returnX = 0, returnY = 0, returnZ = 0, returnOrientation = 0;
    bool hasInstanceReturn = false;
    float portalCooldown = 0;
    // Flight points this character has visited. Retail sells a flight only to
    // a node the player has already discovered, and that rule is what keeps a
    // level-one character from crossing the continent in a minute.
    std::vector<uint32_t> knownTaxiNodes;
    uint16_t ridingSkill = 0; // Ground riding: untrained, apprentice 75, journeyman 150.
    bool migrateLegacyRiding = false; // Save16 encodes a pending migration as reserved riding value 65535.
    // A taxi flight in progress. Persisted with the character, so a save made
    // mid-flight resumes rather than dropping the player out of the sky.
    LocalFlightState flight;
    // Transport the character is standing on, and where on its hull. Position
    // is stored as an offset so the passenger rides with the hull instead of
    // being teleported to it every tick.
    uint32_t transportEntry = 0;
    float transportOffsetX = 0, transportOffsetY = 0, transportOffsetZ = 0;
    float transportLastYaw = 0;
    // Professions learned from a trainer. Bounded like every other per-character
    // list here; retail allows two primaries and the secondaries, so the cap is
    // a corruption guard rather than a limit a player can reach.
    std::vector<LocalProfessionSkill> professions;
    // Where the innkeeper this character last spoke to stands, and how long
    // until they can return to it. The position is the innkeeper's own, so
    // nothing about home is invented; an unbound character has hasHome false
    // and simply cannot use it.
    bool hasHome = false;
    uint32_t homeMapId = 0;
    float homeX = 0, homeY = 0, homeZ = 0, homeOrientation = 0;
    float hearthCooldown = 0;
    // What the owner's own client last said about how this character is moving;
    // see kLocalMovementFalling. Reported alongside the position and never
    // saved: it describes this instant, not the character.
    uint8_t movementState = 0;
    uint32_t mountSpellId = 0; // session-only; learned spell remains in persistent knownSpells

    // The fall in progress, measured by the authority alone. `fallStartZ` is the
    // height the descent began at and `fallRevision` the positionRevision it
    // began under - a relocation bumps that and abandons the measurement, which
    // is what stops a portal, a flight or the below-the-world rescue from
    // arriving as a fall. Runtime only, like attackTimer.
    bool falling = false;
    float fallStartZ = 0;
    uint32_t fallRevision = 0;
};
struct LocalItemDefinition {
    uint32_t id = 0, displayId = 0;
    std::string name;
    uint16_t stack = 1;
    // slot retains legacy schema-1/catalog meaning. Use inventoryType through
    // localEquipmentSlotMask for runtime equipment compatibility.
    uint8_t slot = 0, inventoryType = 0;
    uint32_t maxHealth = 0, attack = 0, armor = 0, heal = 0, mana = 0, value = 0;
};
enum class LocalProcEffect : uint8_t { None, DamageAttacker, RestoreMana, MeleeDamageShield, HealOwner, RestorePower, AddComboPoints, HealOwnerPctMaxHealth, ApplyOwnerAura, ConsumeOwnerAuraCharge, ConsumeSpellCostCharge, Ignite, RestorePetPower };
/// Who an effect is applied to, as distinct from whose aura selected it.
/// Unit::GetSpellModOwner and the owned-creature recipient are two different
/// relationships: an owner supplies modifiers, a recipient receives the effect.
enum class LocalProcRecipient : uint8_t { AuraOwner = 0, OwnedPet = 1 };
struct LocalProcDefinition {
    LocalProcEffect effect=LocalProcEffect::None;
    uint32_t spellId=0, flags=0, cooldownMs=0, amount=0, baseLevel=0, maxLevel=0;
    uint32_t schoolMask=0,spellFamily=0;
    std::array<uint32_t,3> spellFamilyFlags{};
    float amountPerLevel=0, range=0;
    uint8_t chance=0, charges=0;
    // Filters describe the event that triggers this aura. Child spell school
    // and family above remain independent (not reused as event filters).
    uint32_t triggerSchoolMask=0,triggerSpellFamily=0;
    std::array<uint32_t,3> triggerSpellFamilyFlags{};
    uint32_t hitMask=LocalProcHitNormal|LocalProcHitCritical;
    float ppm=0; // Zero uses chance; positive values need a weapon event period.
    uint8_t phaseMask=LocalProcPhaseHit;
    uint8_t spellTypeMask=7; // Source spell_proc damage1, heal2, other4.
    bool allowTriggered=false;
    uint8_t pushbackPercent=0; // Reviewed Earth Shield secondary effect.
    uint8_t resourceType=0; // RestorePower: rage1 or energy3 in displayed units; RestorePetPower: source focus2.
    uint8_t recipient=uint8_t(LocalProcRecipient::AuraOwner); // LocalProcRecipient; never reinterpreted as an owner.
    uint64_t requiredForms=0; // The passive aura's own form mask, independent of its sibling.
    uint32_t attributesMask=0;
    uint8_t disableEffectsMask=0,sourceEffectMask=1;
    bool hasUnsupportedConditions=false,hasUnsupportedScript=false;
};
struct LocalPassiveCastModifier {
    uint8_t operation=0; // 0: direct; 3/12/23: source effect slots 0/1/2; 5: range; 8: all effects; 10: cast; 11: recovery; 14: cost; 22: periodic.
    bool percentage=false; // Aura 108; aura 107 retains a flat amount.
    bool active=false; // Operation zero is a real direct-amount modifier.
    int32_t amount=0;
    std::array<uint32_t,3> mask{};
};
static_assert(sizeof(LocalPassiveCastModifier)==20, "Bounded modifier record size");
struct LocalSpellDefinition {
    uint16_t passiveArmorAttackPowerDivisor=0;
    uint8_t passiveOffhandDamagePct=0,passiveWeaponHitPct=0;
    uint8_t passivePhysicalDamagePct=0;
    uint8_t passiveEquipmentArmorPct=0,passiveFeralCritPct=0,passiveFeralDodgePct=0,passiveCatRunPct=0;
    uint8_t directEffectSlot=255,periodicEffectSlot=255; // Exact DBC slot; 255 denotes unknown/aggregate.
    uint8_t stormstrikeProfile=0,stormstrikeManaChancePct=0;
    uint8_t passiveIntellectAttackPowerPct=0,passiveDualWieldHitPct=0;
    bool passiveCanParry=false,passiveCanDualWield=false;
    uint8_t physicalDamageDonePct=0,damageTakenPct=0;
    uint8_t periodicHealMaxHealthPct=0;
    uint8_t arcaneBlastProfile=0; // Ordinary ranks 1; internal four-stack aura 2.
    // Source crit metadata distinguishes magic, melee, ranged and non-damaging
    // spells. Only explicitly reviewed retaliation leaves may roll proc crits.
    std::array<uint8_t,5> passiveTotalStatPct{};
    uint8_t passiveSpellCritPct=0;
    uint8_t clearcastingProfile=0; // Mage parent1, Omen parent2, Mage child3, Druid child4.
    int16_t chargedCostPct=0;
    std::array<uint32_t,3> chargedCostMask{};
    bool omenProcEligible=false; // Exact source script admission for an ordinary spell hit.
    uint32_t sourceRawCastTimeMs=0; // Unmodified DBC cast time used by spell PPM.
    uint8_t sourceDamageClass=0;
    bool sourceNotAProc=false; // Source attribute permits triggered spell proc selection.
    bool sourceDoNotConsumeResources=false;
    bool sourceIgnoreCasterModifiers=false;
    uint8_t rangedAutoProfile=0;
    // Reviewed SPELL_EFFECT_APPLY_AREA_AURA_RAID profile: caster-anchored, one
    // fixed radius, indefinite duration. Zero on every other spell. The marker
    // effects are retained with their real source values rather than normalised
    // away; carrying them is not a claim that their aura types are implemented.
    // SPELL_AURA_ABILITY_PERIODIC_CRIT (286) grant: the family and class mask it
    // affects. Zero on every spell the importer has admitted at this baseline -
    // no granting row is importable yet, and none is fabricated.
    uint32_t periodicCritFamily=0;
    std::array<uint32_t,3> periodicCritMask{};
    uint8_t areaAuraProfile=0,areaAuraEffectMask=0;
    std::array<uint8_t,3> areaAuraTypes{};
    std::array<int32_t,3> areaAuraAmounts{};
    std::array<uint32_t,3> areaAuraMiscValues{};
    float areaAuraRadius=0;
    // SpellDuration.dbc -1. durationMs stays zero: this is not a lease.
    bool indefiniteDuration=false;
    // Reviewed SPELL_EFFECT_SUMMON_PET profile: the creature entry the caster's
    // controlled summon is built from, and which effect slot named it. Zero on
    // every spell that is not such a summon.
    uint32_t summonPetEntry=0,summonPetDurationMs=0;
    uint8_t summonPetKind=0,summonPetEffectSlot=255;
    uint8_t wardProfile=0,moltenShieldsChancePct=0;
    bool npcOnly=false,sourceCantReflect=false,sourceAlwaysHit=false;
    float sourceProjectileSpeed=0;
    bool sourceCantCrit=false,procCanCrit=false;
    uint8_t spiritCritRatingPct=0,incomingCritReductionPct=0,mageArmorGroup=0;
    uint8_t meleeSpecialProfile=0; // 1: reviewed Bloodthirst AP-based melee special.
    uint32_t triggeredAuraSpellId=0; // Scripted self aura, resolved and admitted before cast commit.
    bool triggeredOnly=false; // Internal chain child: never learned, trained or directly cast.
    uint8_t meleeHastePct=0; // Reviewed temporary aura 138: both melee hands, never ranged.
    uint32_t procParentTalentId=0; // Internal child is valid only for its current allocated parent rank.
    uint8_t comboProfile=0,comboGain=0;
    bool comboFinisher=false,weaponDamage=false,normalizedWeapon=false,requiresBehind=false;
    uint16_t weaponPercent=100;
    float directPerCombo=0,periodicPerCombo=0,extraEnergyMultiplier=0;
    uint64_t requiredForms=0,excludedForms=0;
    uint8_t formId=0;bool notShapeshifted=false,allowWithoutForm=false;
    uint32_t id = 0, mana = 0, cooldownMs = 0, damage = 0, heal = 0;
    bool directIgnoresArmor=false,periodicIgnoresArmor=false;
    int16_t passiveSchoolThreatPercent=0;uint8_t passiveSchoolThreatMask=0;
    int8_t requiredItemClass=-1;
    uint32_t requiredItemSubclasses=0,requiredInventoryTypes=0;
    bool requiresMainHand=false,requiresOffHand=false;
    uint8_t chainTargets=1;
    uint16_t chainMultiplierPermille=1000;
    float chainRadius=0,areaRadius=0; // Reviewed caster-centered direct damage only.
    uint32_t cooldownCategory=0,categoryCooldownMs=0;
    bool noCategoryCooldownMods=false;
    uint32_t allowableClasses = 0;
    uint32_t talentId=0,talentTab=0;
    uint8_t talentRank=0,talentRow=0;
    std::array<uint32_t,3> talentPrerequisites{};
    std::array<uint8_t,3> talentPrerequisiteRanks{};
    uint32_t buffAbsorb=0,absorbSchoolMask=0;
    uint32_t manaPerAbsorbMilli=0; // Zero for ordinary shields; Mana Shield uses DBC multiplier * 1000.
    uint8_t maxAuraStacks=1;
    LocalProcDefinition proc;
    LocalProcDefinition secondaryProc; // Reviewed talent learning two complete passive proc auras.
    uint32_t manaPer5=0; // Fixed mana regeneration while the recipient aura lives.
    bool buffSelfOnly=true;
    uint32_t buffHealth=0,buffArmor=0; // fixed positive, timed stat buffs
    uint32_t passiveHealth=0,passiveArmor=0;
    uint8_t passiveMeleeCritPct=0; // Reviewed weapon-qualified aura 52 melee critical chance.
    // P06  form-boost amounts. AuraEffect::HandleShapeshiftBoosts
    // (SpellAuraEffects.cpp:1350-1445) is the reference's per-form grant table;
    // SpellShapeshiftForm.dbc's stanceSpell[] is not (0 of the nine modelled
    // forms carries one). These carry the boost spells' own DBC amounts rather
    // than a transcribed constant.
    uint16_t passiveAttackPower=0;      // Aura 99, CalcValue's BasePoints + DieSides term.
    float passiveAttackPowerPerLevel=0; // Aura 99, EffectRealPointsPerLevel; clamped by baseLevel/maxLevel.
    uint8_t passiveArmorPenetrationPct=0; // Aura 280, Unit::CalcArmorReducedDamage's bonusPct (Unit.cpp:2239-2267).
    uint8_t furorChancePct=0;   // Furor's SPELL_AURA_DUMMY amount (SpellAuraEffects.cpp:2100-2132).
    uint8_t retainedRage=0;     // Stance/Tactical Mastery's SPELL_AURA_DUMMY amount (:2193-2221), whole rage.
    uint8_t passiveManaRegenInterruptPct=0; // Aura 134: spirit regen retained after spending mana.
    std::array<uint8_t,5> passiveManaRegenStatPct{}; // Aura 219: percent of each stat as mana per five seconds.
    std::array<LocalPassiveCastModifier,3> passiveCastModifiers{};
    uint32_t spellFamily=0;
    std::array<uint32_t,3> spellFamilyFlags{};
    uint8_t passivePushbackPct=0; // Aura 108, spell modifier operation 9 only.
    std::array<uint32_t,3> pushbackSpellMask{};
    bool passive=false;
    // 255 consumes the active class resource; otherwise an explicit power type.
    uint8_t resourceType = 255;
    LocalRuneCost runeCost{};
    uint16_t runicPowerGain = 0; // Displayed units; SpellRuneCost stores tenths.
    float range = 30;
    std::string name;
    // Imported 3.3.5a metadata. Nonempty unsupportedReason rejects the whole
    // spell, including otherwise supported effects, rather than silently losing them.
    bool clientSpell = false;
    uint32_t iconId = 0, castTimeMs = 0, globalCooldownMs = 0, durationMs = 0;
    uint32_t interruptFlags = 1; // DBC casting flags; legacy catalog casts stop on movement.
    bool noPushback = false; // Spell AttributesEx6 NO_PUSHBACK.
    uint32_t visualId = 0, schoolMask = 0;
    uint32_t manaPercent = 0, baseLevel = 1, maxLevel = 0, damageMax = 0, healMax = 0;
    uint8_t snarePercent = 0; // Reviewed NPC movement reduction, strongest effect wins.
    // P04 control metadata, carried raw from Spell.dbc. `mechanic` is column 3
    // and `effectMechanic` columns 83-85; both were already read as a bare
    // `== 15` for the bleed rule and are now named. `auraInterruptFlags` is
    // column 32 and is carried verbatim, including the four bits the reference's
    // own enumeration does not name, because rejecting a row over a bit the
    // reference never reads would lose real content.
    uint8_t mechanic = 0;
    std::array<uint8_t,3> effectMechanic{};
    uint32_t auraInterruptFlags = 0;
    uint8_t preventionType = 0; // Column 214: what a silence actually prevents.
    // SPELL_ATTR4_DAMAGE_DOESNT_BREAK_AURAS. Carried on the DAMAGING spell: it
    // exempts that spell from breaking a control, never the control itself.
    bool sourceDamageDoesNotBreakAuras = false;
    // 0 none, 1 stun, 2 silence. Only the narrow single-effect, single-target,
    // fixed-duration, non-proc, non-channelled, non-area shape is admitted.
    uint8_t controlProfile = 0;
    uint8_t controlEffectSlot = 0;
    // P04 immunity and dispel metadata, carried raw from Spell.dbc.
    // `effectMask` has bit k set when column 71+k names any effect: it is what
    // the reference's IsEffect() loop walks (Creature.cpp:2305-2313), so the
    // "immune to all effects" rule can count the real slots rather than the
    // aggregated amounts. `dispelType` is column 2 (DispelType 0-11), the
    // value Unit::IsImmunedToSpell tests against IMMUNITY_DISPEL and the client
    // already resolves for its debuff frame. The three attribute bits are the
    // ones the immunity and resistance predicates read: SPELL_ATTR0_NO_IMMUNITIES
    // (0x20000000, Unit.cpp:9751), SPELL_ATTR2_NO_SCHOOL_IMMUNITIES (0x04000000,
    // Unit.cpp:9634/9792) and SPELL_ATTR4_NO_CAST_LOG (0x1, Unit.cpp:2341).
    uint8_t effectMask = 0;
    uint8_t dispelType = 0;
    bool sourceNoImmunities = false, sourceNoSchoolImmunities = false, sourceNoCastLog = false;
    bool sourceBinary = false; // reviewed whole-profile CU_BINARY attribution 
    // SPELL_EFFECT_DISPEL (38) admitted in exactly one shape: a magic dispel of
    // a hostile unit target beside a non-dispel effect, where Spell::CheckCast
    // skips the nothing-to-dispel gate (Spell.cpp:6278-6282). `dispelAttempts`
    // is BasePoints + 1 - how many auras EffectDispel would draw. Zero on every
    // other spell; a pure dispel is rejected by name, not carried.
    uint8_t dispelProfile = 0, dispelAttempts = 0;
    bool snareZeroHealingMarker = false; // Frostbolt aura 118 has exactly zero modifier.
    uint32_t periodicDamage = 0, periodicDamageMax = 0, periodicIntervalMs = 0;
    float periodicDamagePerLevel = 0;
    uint32_t periodicHeal = 0, periodicHealMax = 0;
    float periodicHealPerLevel = 0;
    bool healingSelfOnly = false;
    float minRange = 0, damagePerLevel = 0, healPerLevel = 0;
    // The next rank of this ability. the implementation (P04): filled from the reference's
    // `spell_ranks` table (SpellMgr::LoadSpellRanks, the authority the
    // reference actually reads) and, for a spell absent from it, from the
    // client's SkillLineAbility.SupercededBySpell column or a reviewed profile.
    // Retail replaces a rank rather than stacking it, and so does this - which
    // is what keeps a level-eighty spellbook the size of a spellbook.
    uint32_t supercededBySpell = 0;
    // SpellInfo::GetFirstRankSpell()->Id: the first rank of this spell's chain
    // (spell_ranks, Talent.dbc rank 1, or the root of the linked chain), or the
    // spell's own id when it is unranked. SpellInfo::IsRankOf compares these;
    // the group tables are keyed by them. Zero only on a hand-built definition.
    uint32_t firstRankSpell = 0;
    // P04 stacking inputs, carried raw from Spell.dbc: columns 71-73 `Effect`
    // and 95-97 `EffectApplyAuraName` (what LoadSpellSpecific's food / drink,
    // polymorph, charm and tracker arms and CanStackWith's periodic arm read),
    // SPELL_ATTR1_NO_THREAT (AttributesEx 0x400, SharedDefines.h:417, the elemental-shield arm),
    // SPELL_ATTR1_IS_CHANNELED | IS_SELF_CHANNELED (AttributesEx 0x44, SharedDefines.h:409/:413) and
    // SPELL_ATTR3_DOT_STACKING_RULE (AttributesEx3 0x80, SharedDefines.h:488, "stack separately for
    // each caster").
    std::array<uint8_t,3> sourceEffect{};
    std::array<uint16_t,3> effectAura{};
    bool sourceNoThreat = false, sourceChanneled = false, sourceDotStackingRule = false;
    // P05 shared combat inputs , carried raw from Spell.dbc and from the
    // reference's computed custom attributes (the source audit):
    // column 39 `SpellLevel`, the flat initial threat of a cast
    // (Spell::HandleThreatSpells, Spell.cpp:5779-5780);
    // SPELL_ATTR0_NO_ACTIVE_DEFENSE (Attributes 0x200000, SharedDefines.h:391 -
    // "cannot be dodged, parried or blocked", Unit::MeleeSpellHitResult :3354,
    // Unit::isSpellBlocked :3266); SPELL_ATTR3_COMPLETELY_BLOCKED (AttributesEx3
    // 0x8, :484, the full block of MeleeSpellHitResult :3351);
    // SPELL_ATTR3_SUPPRESS_TARGET_PROCS (AttributesEx3 0x20000, :498, "No initial
    // aggro", HandleThreatSpells :5764); SPELL_ATTR0_CU_DIRECT_DAMAGE, which
    // SpellMgr::LoadSpellInfoCustomAttributes derives from a school, weapon or
    // heal effect (SpellMgr.cpp:3341-3347) and which cancels the full block
    // (:3351); SPELL_ATTR0_CU_NO_INITIAL_THREAT, derived from the tracking /
    // ranged-haste / possess-pet / invisibility-detect / water-breathing auras
    // (:3293-3299), the drain / burn / leech / heal-pct / energize / create-item
    // effects (:3350-3358) and the hunter aspects (:3499-3502).
    uint16_t spellLevel = 0;
    bool sourceNoActiveDefense = false, sourceCompletelyBlocked = false, sourceSuppressTargetProcs = false;
    bool sourceDirectDamage = false, sourceNoInitialThreat = false;
    // P05  line of sight: SPELL_ATTR2_IGNORE_LINE_OF_SIGHT (AttributesEx2
    // 0x4, SharedDefines.h:446) or SPELL_ATTR5_ALWAYS_AOE_LINE_OF_SIGHT
    // (AttributesEx5 0x04000000, SharedDefines.h:581) - either lets a cast past
    // the line-of-sight test at Spell.cpp:6092-6093.
    bool sourceIgnoreLineOfSight = false;
    // P06  SPELL_ATTR0_ONLY_OUTDOORS (Attributes 0x8000,
    // SharedDefines.h:385), the gate Spell::CheckCast applies to a player caster
    // at Spell.cpp:5902-5904. Set on exactly two of the nine modelled form
    // spells: Travel Form 783 and Ghost Wolf 2645.
    bool sourceOnlyOutdoors = false;
    // P05  range, facing and target-attribute inputs.
    // `sourceRangeFlags` is SpellRange.dbc's Flags column (Spell.h:100-102:
    // 0 default, 1 melee, 2 ranged), which Spell::CheckRange branches on for
    // the melee reach and the completion leniency (Spell.cpp:7332-7396).
    // `sourceFacingFlags` is Spell.dbc column 19, FacingCasterFlags; bit 0 is
    // SPELL_FACING_FLAG_INFRONT (SpellDefines.h:136) and makes the cast fail
    // with SPELL_FAILED_UNIT_NOT_INFRONT unless the caster has the target in a
    // pi arc or is inside its boundary radius (Spell.cpp:7360).
    // `targetCreatureType` is Spell.dbc column 17, the mask
    // SpellInfo::CheckTargetCreatureType compares against
    // Unit::GetCreatureTypeMask (SpellInfo.cpp:1906-1919, refused at :1819-1825).
    // `sourceOnlyPeacefulTargets` is SPELL_ATTR1_ONLY_PEACEFUL_TARGETS
    // (AttributesEx 0x100, SharedDefines.h:415), refused as
    // SPELL_FAILED_TARGET_AFFECTING_COMBAT (SpellInfo.cpp:1694-1695).
    uint8_t sourceRangeFlags = 0, sourceFacingFlags = 0;
    uint32_t targetCreatureType = 0;
    bool sourceOnlyPeacefulTargets = false;
    uint32_t mountCreatureId = 0, mountDisplayId = 0, mountSpeedPercent = 0;

    std::string iconPath, unsupportedReason;
};
struct LocalQuestObjective {
    enum class Type : uint8_t { Kill = 0, Collect = 1, Talk = 2 };
    Type type = Type::Kill;
    uint32_t entry = 0;
    uint16_t count = 1;
};
struct LocalQuestDefinition {
    uint32_t id = 0, giverEntry = 0, turnInEntry = 0, prerequisite = 0;
    uint32_t allowableRaces = 0, allowableClasses = 0, requiredSkill = 0;
    uint8_t minLevel = 1;
    std::string title, description;
    uint32_t xp = 0, money = 0, rewardItem = 0;
    uint16_t rewardCount = 0;
    std::vector<LocalQuestObjective> objectives;
    // Legacy rewardItem/rewardCount is the first guaranteed reward. These
    // optional lists extend old content without changing character saves.
    std::vector<LocalItemStack> additionalRewards; // up to three more guaranteed items
    std::vector<LocalItemStack> rewardChoices;     // choose exactly one of up to six
};
inline size_t localQuestRewardCount(const LocalQuestDefinition& q) {
    return (q.rewardItem ? 1u : 0u) + q.additionalRewards.size();
}
inline LocalItemStack localQuestRewardAt(const LocalQuestDefinition& q,size_t index) {
    if(q.rewardItem) {if(!index)return {q.rewardItem,q.rewardCount};--index;}
    return index<q.additionalRewards.size()?q.additionalRewards[index]:LocalItemStack{};
}
inline bool validLocalQuestRewards(const LocalQuestDefinition& q) {
    if(bool(q.rewardItem)!=bool(q.rewardCount) || q.additionalRewards.size()>3 ||
       (!q.rewardItem && !q.additionalRewards.empty()) || q.rewardChoices.size()>6)return false;
    for(const auto& r:q.additionalRewards)if(!r.itemId || !r.count)return false;
    for(const auto& r:q.rewardChoices)if(!r.itemId || !r.count)return false;
    return true;
}
struct LocalNpcDefinition {
    uint32_t id = 0, displayId = 0, health = 40, damage = 4, armor = 0, xp = 50, money = 0;
    uint8_t level = 1;
    uint32_t faction = 0, unitFlags = 0;
    // creature_template.npcflag: which services this NPC offers. Zero in a
    // catalog built before the field was imported, which is why flight-master
    // identification also has a proximity fallback - see LocalTravelNetwork.
    uint32_t npcFlags = 0;
    std::string name;
    // Optional server-authored gossip. These strings do not live in client MPQs.
    std::string gossipText, subname;
    bool hostile = false, questGiver = false;
    float respawnSeconds = 30, aggroRadius = 0;
    std::vector<LocalItemStack> loot;
    // Explicit catalog stock overrides the compiled per-NPC npc_vendor table.
    // Empty uses the matching upstream merchant's exact stock, never generic
    // item categories. See local_services.hpp for unsupported purchase gates.
    std::vector<uint32_t> vendorItems;
    // What a trainer teaches, when a catalog states it outright rather than
    // leaving it to be read out of the subname. A SkillLine id and a class id
    // 1-11; zero means fall back to the transcription beside the code.
    uint16_t trainerSkill = 0;
    uint8_t trainerClass = 0;
    // P04 creature template immunity: the creature_immunities set this template
    // points at through creature_template.CreatureImmunitiesId, reduced to the
    // two columns that intersect any admitted spell (SchoolMask, 7 bits, and
    // MechanicsMask, 64 bits; the source audit section
    // 6.5 measured DispelTypeMask, Effects, Auras, ImmuneAoE and ImmuneChain to
    // intersect nothing). Creature::LoadTemplateImmunities applies them with the
    // placeholder spell id UINT32_MAX (Creature.cpp:2251), which is how the
    // reference tells template immunity from aura immunity. Zero on a creature
    // without a set, and on any catalog built before the field existed.
    uint8_t immuneSchoolMask = 0;
    uint64_t immuneMechanicsMask = 0;
    // P04 creature_template_resistance, one value per school in the
    // UNIT_FIELD_RESISTANCES order 1..6: holy, fire, nature, frost, shadow,
    // arcane (ObjectMgr.cpp:764-808 into CreatureTemplate::resistance). Negative
    // rows are clamped to zero at compile time, so the uint32 wrap of
    // Unit::GetResistance cannot arise here. Zero without a row.
    std::array<uint16_t,6> resistances{};
    // P05  UNIT_FIELD_COMBATREACH and UNIT_FIELD_BOUNDINGRADIUS, as
    // Creature::SetObjectScale derives them (Creature.cpp:3536-3550):
    // creature_model_info for the chosen display id, scaled by that model row's
    // DisplayScale. `combatReach` is what Unit::IsWithinCombatRange,
    // Unit::GetMeleeRange and Spell::CheckRange add to every range test
    // (Unit.cpp:766-803, Spell.cpp:7301-7396); `boundingRadius` is what
    // Unit::IsWithinBoundaryRadius compares against, floored at MIN_MELEE_REACH
    // (Unit.cpp:820-828), which is the facing check's own escape. Zero on a
    // catalog built previously; localCreatureCombatReach() then substitutes
    // the reference's own DEFAULT_WORLD_OBJECT_SIZE.
    float combatReach = 0, boundingRadius = 0;
};
/// creature_template.npcflag bits, as the server writes them. The catalog is
/// the authority whenever it carries the field; when it does not, the
/// transcription in local_service_npcs_generated.inc stands in - see
/// localEffectiveNpcFlags() in local_services.hpp, which is the only place
/// these should be read from a definition.
inline constexpr uint32_t kLocalNpcFlagTrainer = 0x00000010u;
inline constexpr uint32_t kLocalNpcFlagTrainerClass = 0x00000020u;
inline constexpr uint32_t kLocalNpcFlagTrainerProfession = 0x00000040u;
inline constexpr uint32_t kLocalNpcFlagVendor = 0x00000080u;
inline constexpr uint32_t kLocalNpcFlagVendorAmmo = 0x00000100u;
inline constexpr uint32_t kLocalNpcFlagVendorFood = 0x00000200u;
inline constexpr uint32_t kLocalNpcFlagVendorPoison = 0x00000400u;
inline constexpr uint32_t kLocalNpcFlagVendorReagent = 0x00000800u;
inline constexpr uint32_t kLocalNpcFlagRepair = 0x00001000u;
inline constexpr uint32_t kLocalNpcFlagInnkeeper = 0x00010000u;
inline constexpr uint32_t kLocalNpcFlagBanker = 0x00020000u;
inline constexpr uint32_t kLocalNpcFlagAuctioneer = 0x00200000u;

/// Below this height a character has left the world rather than gone somewhere
/// low, and the authority puts them back. The lowest real ground in 3.3.5a is
/// a few hundred units above sea level's zero at worst, so this leaves the
/// whole of the playable world - including the deepest instance floors - well
/// clear of it, and only catches a fall with nothing under it.
inline constexpr float kLocalWorldFloorZ = -2000.0f;

/// What the owner's own client reports about how its character is moving.
///
/// The authority owns positions and rules; it does not own terrain. There is no
/// height map, no collision and no liquid in this realm - the renderer holds all
/// three, because it has to draw them - so these two bits are the only way the
/// host can tell a character dropping through the air from one walking down a
/// hill, or one that hit water from one that hit stone. Retail draws the line in
/// exactly the same place: the client sets MOVEMENTFLAG_FALLING and the server
/// keeps the fall height and deals the damage.
///
/// Falling means airborne *and* descending, so the apex of a jump is where a
/// fall starts and a jump on the spot measures no drop at all.
///
/// These arrive with a reported position and are trusted exactly as far: the
/// authority still owns the measurement, the curve and the health, so a client
/// that lies about them can spare itself a fall it had coming and can do
/// nothing whatever to anybody else.
inline constexpr uint8_t kLocalMovementFalling = 0x01;
inline constexpr uint8_t kLocalMovementInLiquid = 0x02;
inline constexpr uint8_t kLocalMovementIndoors = 0x04;
inline constexpr uint8_t kLocalMovementMask = kLocalMovementFalling | kLocalMovementInLiquid | kLocalMovementIndoors;

/// Fall damage, as 3.3.5a deals it.
///
/// Transcribed from Player::HandleFall of the same server line this realm's
/// world data is imported from: nothing at all below 14.57 units of drop, then
/// 1.8% of the character's maximum health per further unit, less a 24.26%
/// offset, capped at the whole bar. Nothing here is fitted - the three numbers
/// are the upstream ones - and the shape they give is the one a player
/// remembers: the first damaging fall costs about two percent, and anything
/// past roughly sixty-nine units is fatal from full health.
///
/// The offset is what makes the curve start gently rather than at a step, and
/// truncating rather than rounding is what keeps a fall of exactly the safe
/// distance free for a low-level character whose two percent is under a point.
inline constexpr float kLocalFallSafeDistance = 14.57f;
inline constexpr float kLocalFallDamagePerUnit = 0.018f;
inline constexpr float kLocalFallDamageOffset = 0.2426f;
/// The drop at which the curve reaches the whole health bar (~69.03 units).
inline constexpr float kLocalFallLethalDistance =
    (1.0f + kLocalFallDamageOffset) / kLocalFallDamagePerUnit;
inline uint32_t localFallDamage(float droppedUnits, uint32_t maxHealth) {
    if (!std::isfinite(droppedUnits) || droppedUnits < kLocalFallSafeDistance || !maxHealth) return 0;
    const float fraction = kLocalFallDamagePerUnit * droppedUnits - kLocalFallDamageOffset;
    if (fraction <= 0.0f) return 0;
    if (fraction >= 1.0f) return maxHealth;
    return std::min(maxHealth, uint32_t(fraction * float(maxHealth)));
}

/// Any of the five merchant bits. A creature with one of them sells something.
inline constexpr uint32_t kLocalNpcFlagAnyVendor =
    kLocalNpcFlagVendor | kLocalNpcFlagVendorAmmo | kLocalNpcFlagVendorFood |
    kLocalNpcFlagVendorPoison | kLocalNpcFlagVendorReagent;

struct LocalNpcSpawn {
    uint32_t id = 0, entry = 0, mapId = 0;
    float x = 0, y = 0, z = 0, orientation = 0;
};
inline constexpr size_t kLocalMaxNpcSnares = 8;
inline constexpr size_t kLocalMaxNpcControls = 4;
// Four diminishing groups are reachable from the admitted spells; eight leaves
// headroom without pretending to the reference's twenty-one.
inline constexpr size_t kLocalMaxNpcDiminishing = 8;
// Named for the two places the importer already compared against a bare number.
inline constexpr uint8_t kLocalMechanicSilenced = 9;
// SPELL_PREVENTION_TYPE_SILENCE. Read from the spell being prevented.
inline constexpr uint8_t kLocalPreventionSilence = 1;
inline constexpr uint8_t kLocalMechanicStunned = 12;
inline constexpr uint8_t kLocalMechanicBleed = 15;
// AURA_INTERRUPT_FLAG_TAKE_DAMAGE. The reference's rule 1 is binary: no
// threshold and no chance, and it fires before the zero-damage early-out.
inline constexpr uint32_t kLocalAuraInterruptTakeDamage = 0x2u;
inline constexpr size_t kLocalMaxNpcDamageAuras = 8;
inline constexpr size_t kLocalMaxNpcStormstrikeAuras = 8;
struct LocalNpcStormstrikeAura {
    uint32_t spellId=17364,remainingMs=0;
    uint64_t casterGuid=0;
    uint8_t charges=4;
    uint32_t casterRevision=0; // Authority-only encounter lifecycle.
};
struct LocalNpcSnare {
    uint32_t spellId=0, remainingMs=0;
    uint64_t casterGuid=0;
    uint8_t percent=0;
    uint32_t casterRevision=0; // Authority-only: travel cancels the old encounter effect.
};
// P04: a control applied to an owned creature. Transient authority and LAN
// state, never character-save data, exactly like LocalNpcSnare. UNIT_STATE_ROOT
// is deliberately absent: the reference keeps root outside UNIT_STATE_CONTROLLED
// and a rooted unit still swings, casts and turns, so root is not a control
// this list models.
enum class LocalNpcControlKind : uint8_t { Stun=0, Silence=1 };
// P04 diminishing returns. Authority-only: a guest renders what the host owns,
// and the diminished result already travels as LocalNpcControl::remainingMs, so
// this record is neither persisted nor replicated.
struct LocalNpcDiminishing {
    uint8_t group=0, hitCount=0, stack=0;
    // Stamped when the last aura of the group is removed. Held at the width of
    // the clock it is compared against: truncating it to 32 bits turns the
    // window comparison into nonsense once a realm has been up 49.7 days, which
    // would silently switch diminishing returns off rather than wrap.
    uint64_t hitTimeMs=0;
};
struct LocalNpcControl {
    uint32_t spellId=0, remainingMs=0;
    uint64_t casterGuid=0;
    uint32_t casterRevision=0; // Authority-only: travel cancels the old encounter effect.
    uint8_t kind=0;            // LocalNpcControlKind
};
struct LocalNpcThreatView {
    uint64_t viewerGuid=0,amount=0;
    uint32_t rawBasisPoints=0;uint16_t scaledBasisPoints=0;uint8_t status=0;bool present=false;
};
struct LocalNpcThreat { uint64_t guid=0,amount=0; };
inline constexpr size_t kLocalMaxNpcThreat=100;
struct LocalRealmNpc {
    uint64_t combatEpoch=0; // Authority-only identity of this NPC encounter.
    LocalNpcThreatView playerThreat;
    std::array<LocalNpcThreat,kLocalMaxNpcThreat> threat{}; // Authority-only, thousandths of threat.
    std::vector<LocalHealingAuraView> damageAuras; // Authority-derived periodic damage views.
    std::vector<LocalNpcSnare> snares; // Transient; replicated, never character-save data.
    std::vector<LocalNpcControl> controls; // Transient; replicated, never character-save data.
    std::vector<LocalNpcDiminishing> diminishing; // Authority-only; neither saved nor replicated.
    std::vector<LocalNpcStormstrikeAura> stormstrikeAuras;
    uint64_t guid = 0, targetGuid = 0, lootOwner = 0;
    // Authority-only cohort captured at death. Only lootOwner is sent over LAN;
    // new group members cannot acquire an earlier corpse's reservation.
    std::array<uint64_t, 5> lootCandidates{};
    uint32_t entry = 0, displayId = 0, mapId = 0, instanceId = 0, health = 0, maxHealth = 0;
    float x = 0, y = 0, z = 0, orientation = 0;
    uint8_t level = 1;
    std::string name;
    bool hostile = false, aggressive = false, questGiver = false, dead = false, lootable = false;
    // Set when this NPC serves a taxi node, and which one. Resolved on the
    // authority when the NPC is spawned; replicated so a LAN guest can open
    // the flight list without its own copy of the network.
    bool flightMaster = false;
    uint32_t taxiNodeId = 0;
    /// Set when this NPC runs an auction house. Resolved on the authority from
    /// the catalog's npcflag, replicated so a guest sees the same auctioneers.
    bool auctioneer = false, banker = false;
    /// The rest of the services this NPC offers, resolved on the authority the
    /// same way and replicated for the same reason: a guest that decided for
    /// itself could offer training the host would refuse.
    bool vendor = false, repairer = false, classTrainer = false,
         professionTrainer = false, innkeeper = false;
    /// Which categories of goods a merchant carries, packed by
    /// localVendorCategories(). Zero on anything that is not a merchant.
    uint8_t vendorCategories = 0;
    /// What a trainer teaches: a SkillLine id for a profession trainer, a class
    /// id 1-11 for a class trainer. Zero when the NPC's own subname named
    /// something this realm does not model, in which case it teaches nothing.
    uint16_t trainerSkill = 0;
    uint8_t trainerClass = 0;
    // Static deck passengers retain model-local offsets on both host and guest.
    // These are transient world actors, not additional saved characters.
    uint32_t transportEntry = 0;
    float transportX = 0, transportY = 0, transportZ = 0, transportOrientation = 0;
    // Internal authority simulation values; never accepted from a client.
    uint32_t spawnId = 0;
    float homeX = 0, homeY = 0, homeZ = 0, attackTimer = 0, respawnTimer = 0;
    uint32_t npcCastingSpellId=0,npcCastRemainingMs=0,npcSpellTimerMs=0,npcSpellReturnMs=0;
    uint64_t npcCastTargetGuid=0;
    bool npcSpellTimerInitialized=false,npcSpellLaunched=false,npcSpellReflected=false,npcSpellReflectReturn=false,npcSpellMissed=false;
};
struct LocalMailboxSite {uint64_t guid=0;uint32_t mapId=0;float x=0,y=0,z=0,orientation=0;};
struct LocalWorldContent {
    mutable bool mailboxSitesReady=false;
    mutable uint32_t mailboxMap=0;
    mutable float mailboxX=0,mailboxY=0,mailboxZ=0;
    mutable std::vector<LocalMailboxSite> mailboxSites;
    uint32_t fingerprint = 0;
    std::string sourcePath;
    std::vector<LocalItemDefinition> items;
    std::vector<LocalSpellDefinition> spells;
    // Immutable installed-content index: talent ID, rank, spell ID. No player cache.
    bool talentIndexReady=false;
    std::vector<std::array<uint32_t,3>> talentSpellIndex;
    // Sorted by spellId, like every other definition list here.
    std::vector<LocalRecipe> recipes;
    std::vector<LocalQuestDefinition> quests;
    std::vector<LocalNpcDefinition> npcs;
    std::vector<LocalNpcSpawn> spawns;
    LocalRealmPlayer start;
    bool classResources = false, clientStarterSpells = false;
    std::string spellDiagnostic;
    std::shared_ptr<LocalWorldCatalog> catalog;
    // Lazy definitions have stable addresses and fixed cache caps. Spawn rows
    // remain on disk and are fetched only for active regions.
    mutable std::map<uint32_t, LocalItemDefinition> itemCache;
    mutable std::map<uint32_t, LocalSpellDefinition> spellCache;
    mutable std::map<uint32_t, LocalQuestDefinition> questCache;
    mutable std::map<uint32_t, LocalNpcDefinition> npcCache;
    mutable std::map<uint32_t, std::vector<uint32_t>> npcQuestCache;
    mutable std::string catalogError;
    const LocalItemDefinition* item(uint32_t id) const;
    const LocalSpellDefinition* spell(uint32_t id) const;
    const LocalRecipe* recipe(uint32_t spellId) const;
    const LocalQuestDefinition* quest(uint32_t id) const;
    const LocalNpcDefinition* npc(uint32_t id) const;
    std::vector<LocalQuestDefinition> questsForNpc(uint32_t entry) const;
};
struct LocalTradeItem {uint32_t item=0;uint16_t count=0,sourceCount=0;uint8_t bag=0;bool operator==(const LocalTradeItem&)const=default;};
struct LocalGraveyardSite {
    uint32_t id = 0, mapId = 0, raceMask = 0, zoneId = 0; // zero mask: neutral sanctuary
    float x = 0, y = 0, z = 0, orientation = 0;
};
inline constexpr float kLocalCorpseReclaimRadius = 10.0f;
inline bool localCanReclaimCorpse(const LocalRealmPlayer& p) {
    if (!p.dead || !p.ghost || !p.corpseValid || p.mapId != p.corpseMapId ||
        p.instanceId != p.corpseInstanceId) return false;
    const float dx=p.x-p.corpseX,dy=p.y-p.corpseY,dz=p.z-p.corpseZ;
    return std::isfinite(dx+dy+dz) && dx*dx+dy*dy+dz*dz <= kLocalCorpseReclaimRadius*kLocalCorpseReclaimRadius;
}
inline void localCaptureCorpse(LocalRealmPlayer& p) {
    if (!p.dead || p.corpseValid) return;
    p.corpseValid=true;p.ghost=false;p.corpseMapId=p.mapId;p.corpseInstanceId=p.instanceId;
    p.corpseZoneId=p.zoneId;p.corpseX=p.x;p.corpseY=p.y;p.corpseZ=p.z;p.corpseOrientation=p.orientation;
}
enum class LocalAction : uint8_t {
    Attack = 1, StopAttack, CastSpell, AcceptQuest, TurnInQuest, Loot, EquipItem,
    UseItem, Respawn, Interact, EnterPortal, LeaveInstance, AbandonQuest = 13,
    UnequipItem = 14, CancelCast = 15, CompleteIntro = 16,
    // Travel. TakeFlight carries the destination node in `id`; BoardTransport
    // carries the transport's gameobject entry there.
    TakeFlight = 17, BoardTransport = 18, LeaveTransport = 19,
    // Auction house. The board lives beside the gameplay rules rather than in
    // them, so LocalRealm handles these three rather than LocalGameplay::execute.
    // BuyoutAuction and BidAuction carry the listing in `id`, and BidAuction the
    // bid in `target`; ListAuction carries the item in `id` and the count in
    // `target`.
    BuyoutAuction = 20, BidAuction = 21, ListAuction = 22,
    // Merchants, repair and training. Merchant commands name the selected NPC
    // and the authority validates that exact nearby service; legacy zero-GUID
    // callers resolve the nearby service. A client cannot trade on the
    // far side of the world or buy training from something that is not a
    // trainer. `id` names the item, spell or skill; `target` carries the stack
    // size for the two merchant actions and nothing for the rest.
    SellToVendor = 23, BuyFromVendor = 24, RepairEquipment = 25,
    LearnSpell = 26, LearnProfession = 27, TrainProfessionRank = 28,
    // Innkeepers. SetHome binds to the innkeeper the player is standing at;
    // ReturnHome works anywhere, on a cooldown.
    SetHome = 29, ReturnHome = 30,
    // Trade skills. LearnRecipe is bought from the profession trainer the
    // player is standing at and carries the recipe in `id`; CraftItem needs no
    // trainer and carries the recipe the character already knows.
    LearnRecipe = 31, CraftItem = 32, CancelAuction = 33, Dismount = 34, BuybackItem = 35,
    BankDeposit = 36, BankWithdraw = 37, UnlearnProfession = 38, BankMove = 39,
    PartyInvite = 40, PartyAccept = 41, PartyDecline = 42, PartyLeave = 43, PartyRemove = 44, PartyPromote = 45,
    BankDepositSlot = 46, // bag slot -> bank slot; same snapshot fields as BankMove.
    ReadyStart=47, ReadyAnswer=48, TradeRequest=49, TradeOpen=50, TradeOffer=51,
    TradeMoney=52, TradeAccept=53, TradeUnaccept=54, TradeCancel=55,
    MailSend=56, MailTakeMoney=57, MailTakeItem=58, MailReturn=59, MailDelete=60, MailRead=61,
    BackpackMove=62, BankWithdrawSlot=63, BankDepositFromSlot=64,
    TrainRiding=65, DiscoverTaxi=66, LearnTalent=67, ResetTalents=68, CancelStatAura=69, CancelForm=70,
    // P03/D1: the owner retires their own summon. Acquisition remains a real
    // cast; P07 still owns stables and the rest of the lifecycle.
    DismissPet=71,
    // P07 : CMSG_PET_ACTION. `target` names the pet and `id` carries the
    // packed action word the client already builds - `(type << 24) | action`,
    // include/game/pet_action.hpp - so the authority and the connected-server
    // path speak one encoding. Commands 0-3 and reactions 0-2 are accepted and
    // everything else is refused, which is HandlePetActionHelper's `default:`.
    PetAction=72,
    ReclaimCorpse=74,
    PetSpellAutocast=73, // Pet GUID, spell id and explicit bid 0/1.
};
/// The highest action a client may send. Anything above it is rejected at the
/// wire rather than reaching the rules, so adding an action here is a
/// deliberate act and a forgotten one is inert instead of dangerous.
inline constexpr LocalAction kLocalActionMax = LocalAction::ReclaimCorpse;
struct LocalRealmCommand { LocalAction action = LocalAction::StopAttack; uint64_t target = 0; uint32_t id = 0; uint32_t bid = 0, buyout = 0, durationMinutes = 0; uint64_t serviceNpcGuid = 0; uint16_t auctionCount = 1; uint16_t bankSourceCount = 0, bankDestinationCount = 0;
    std::string mailRecipient,mailSubject,mailBody;
    std::vector<LocalTradeItem> mailAttachments;
};

// Deliberately bounded local simulation. This is a standalone ruleset, not an
// AzerothCore replacement claiming complete retail scripts or WoW formulas.
struct LocalParty;
struct LocalRealmPet;
class LocalGameplay {
public:
    static constexpr size_t MaxInventory = 24, MaxQuests = 32, MaxNpcs = 128, MaxInstances = 128;
    // Authority-owned, transient single-target healing effects. Recasts replace
    // an existing rank rather than consuming another slot.
    static constexpr size_t MaxPeriodicHeals = 256, MaxPeriodicHealsPerTarget = 8;
    // A spellbook and a recipe book, per character.
    //
    // Owner progress uses bounded multipart snapshots. These caps also bound
    // save parsing, memory use and reassembly; see local_realm.cpp's packet budget.
    static constexpr size_t MaxSpells = 192, MaxRecipes = 96;
    // Cooldowns are bounded separately because only the handful of abilities
    // actually cooling down are ever present; sizing this with the spellbook
    // would have spent 384 bytes a packet on entries that are never sent.
    static constexpr size_t MaxCooldowns = 16;
    // Two primaries and the secondaries, plus headroom for a client whose
    // SkillLine.dbc lists more secondary skills than 3.3.5a's three.
    static constexpr size_t MaxProfessions = 8;
    /// How far a merchant, trainer or innkeeper reaches. The same eight yards
    /// every other conversation in this realm uses.
    static constexpr float ServiceRange = 8.0f;
    /// Seconds between uses of the innkeeper's return. Retail's hearthstone is
    /// thirty minutes in 3.3.5a; this is the same rule with the same purpose -
    /// a way home that cannot replace travelling.
    static constexpr float HearthCooldownSeconds = 1800.0f;
    // Independent corruption/transport bound, not an active-log restriction.
    static constexpr size_t MaxCompletedQuests = 65536;
    LocalGameplay();
    ~LocalGameplay();
    LocalGameplay(LocalGameplay&&) noexcept;
    LocalGameplay& operator=(LocalGameplay&&) noexcept;
    // Authority persistence integration for single-target aura replacement.
    // Queried only on transfer/reset, never used as a combat actor roster.
    void setAuraOwnerProvider(std::function<std::vector<LocalRealmPlayer*>()> provider);
    bool loadContent(const std::string& path, std::string& error);
    bool loadCatalog(const std::string& directory, std::string& error);
    /// P05 line of sight . Optional: a realm with no collision pack
    /// installed answers every line-of-sight test with "visible", which is how
    /// every build up to the implementation behaved and how this one ships. A pack that does
    /// load folds its fingerprint into the content fingerprint, because two
    /// peers whose packs differ would disagree about which casts land.
    bool loadCollision(const std::string& directory, std::string& error);
    const LocalCollisionData* collision() const;
    /// The suite and the extractor author tiles rather than reading them.
    void adoptCollisionTile(LocalCollisionTile tile);
    bool setStarterSpells(const std::vector<LocalSpellDefinition>& spells, const std::string& diagnostic, std::string& error);
    bool setAreaTriggers(const std::vector<LocalAreaTriggerVolume>& volumes, std::string& error);
    const std::vector<LocalAreaTriggerVolume>& areaTriggers() const;
    bool setGraveyards(const std::vector<LocalGraveyardSite>& sites, std::string& error);
    const std::vector<LocalGraveyardSite>& graveyards() const;
    bool setFactionTemplates(const std::vector<LocalFactionTemplate>& rows,
                             const std::array<uint32_t, 12>& raceTemplates, std::string& error);
    const std::vector<LocalFactionTemplate>& factionTemplates() const;
    const std::array<uint32_t, 12>& raceFactionTemplates() const;
    // Shared attackability/aggression query for presentation; resolves the
    // same NPC and faction templates once rather than three times.
    struct NpcDisposition { bool attackable = false; bool aggressive = false; };
    NpcDisposition npcDisposition(const LocalRealmPlayer& player, const LocalRealmNpc& npc) const;
    bool canAttack(const LocalRealmPlayer& player, const LocalRealmNpc& npc) const;
    bool isAggressive(const LocalRealmPlayer& player, const LocalRealmNpc& npc) const;
    bool insidePortal(uint32_t portalId, const LocalRealmPlayer& player) const;
    std::vector<LocalRealmPortal> portals() const;

    // --- Instances ----------------------------------------------------------
    /// Install the client's Map.dbc rows. Without them the realm falls back to
    /// the catalog's own instance flag exactly as it did before, so this is
    /// additive: it can only make more of the client's dungeons reachable.
    bool setClientMaps(std::vector<LocalMapDefinition> maps, std::string& error);
    const std::vector<LocalMapDefinition>& clientMaps() const;
    const LocalMapDefinition* clientMap(uint32_t mapId) const;
    /// Whether this map holds private/shared instances, from either source.
    bool instanceMap(uint32_t mapId) const;

    // --- Services -----------------------------------------------------------
    /// Install the client's SkillLine.dbc rows. Only professions and secondary
    /// skills are kept; everything else a trainer could theoretically teach is
    /// a class ability, a weapon skill or a language, none of which this realm
    /// models. Empty rows leave the built-in fourteen in place.
    bool setSkillLines(const std::vector<LocalSkillLine>& lines, std::string& error);
    const std::vector<LocalSkillLine>& skillLines() const;
    /// The qualifying NPC of a kind the player is standing at, or nullptr.
    const LocalRealmNpc* serviceNpc(const LocalRealmPlayer& player, uint32_t npcFlag, uint64_t npcGuid = 0) const;
    /// What the merchant the player is standing at sells. Empty away from one.
    std::vector<uint32_t> vendorStock(const LocalRealmPlayer& player, uint64_t npcGuid = 0) const;
    int32_t vendorRemaining(const LocalRealmPlayer& player, uint32_t itemId, uint64_t npcGuid = 0) const;
    // Financial commands can roll back stock along with bags/gold if the
    // atomic character save fails.
    LocalVendorInventory vendorInventorySnapshot() const;
    void restoreVendorInventory(LocalVendorInventory snapshot);
    std::vector<LocalVendorStockRecord> savedVendorStock() const;
    bool restoreVendorStock(const std::vector<LocalVendorStockRecord>& records);
    /// Abilities the class trainer the player is standing at can teach them.
    std::vector<uint32_t> trainableSpells(const LocalRealmPlayer& player, uint64_t npcGuid = 0) const;
    /// Install the trade-skill recipes read from the client's own
    /// SkillLineAbility.dbc and Spell.dbc. Without them no profession can be
    /// practised and the trainer says so, rather than inventing recipes.
    bool setRecipes(std::vector<LocalRecipe> recipes, std::string& error);
    /// Recipes the profession trainer the player is standing at can teach them
    /// now: their skill line, within their current skill, not already known.
    std::vector<uint32_t> trainableRecipes(const LocalRealmPlayer& player, uint64_t npcGuid = 0) const;
    /// Recipes the character knows and currently has the reagents for.
    std::vector<uint32_t> craftableRecipes(const LocalRealmPlayer& player) const;

    // --- Travel -------------------------------------------------------------
    // Client taxi data, supplied by the application after the MPQs are open.
    // Without it flights and transports simply do not exist; nothing else in
    // the simulation depends on them.
    bool setTravelNetwork(std::vector<LocalTaxiNode> nodes,
                          std::vector<LocalTaxiPath> paths,
                          std::vector<LocalTaxiWaypoint> waypoints,
                          std::string& error);
    /// Adopt an already-parsed network. Starting a realm builds a fresh
    /// gameplay object, and the client's taxi rows are read once before that
    /// happens; this is how they survive it without being re-parsed.
    void useTravelNetwork(const LocalTravelNetwork& network);
    const LocalTravelNetwork& travel() const;
    double transportTime() const;
    void setTransportTime(double seconds);
    void advanceTransportTime(double seconds);
    // Nodes this player can currently fly to from the flight master they are
    // talking to, already filtered by what they have discovered.
    std::vector<uint32_t> flightDestinations(const LocalRealmPlayer& player,
                                             uint32_t fromNode) const;
    // Where every transport is, as of the last tick.
    const std::vector<LocalTransportState>& transports() const;
    // Mark a node discovered. Returns true when it was new.
    static bool discoverTaxiNode(LocalRealmPlayer& player, uint32_t nodeId);
    const std::vector<LocalInstanceState>& instances() const;
    bool restoreInstances(const std::vector<LocalInstanceState>& instances, std::string& error);
    static bool validCharacterOptions(uint8_t race, uint8_t classId, uint8_t gender);
    const LocalWorldContent& content() const;
    void refreshInventoryObjectives(LocalRealmPlayer& player);
    void useContent(std::shared_ptr<LocalWorldContent> content);
    std::shared_ptr<LocalWorldContent> sharedContent() const;
    bool validatePlayer(const LocalRealmPlayer& player, std::string& error) const;
    /// Bring a player to the state the shipped rules derive for it.
    ///
    /// `forcedLevel` (0 = the catalog's own starting level) applies only to a
    /// fresh character, and applies BEFORE the spellbook, the resource type and
    /// the health/mana pools are derived, because all three are read off
    /// `player.level`. Writing a level over a finished level-1 character
    /// instead would leave every one of them stale.
    void initializePlayer(LocalRealmPlayer& player, bool fresh, uint8_t forcedLevel = 0);
    bool execute(LocalRealmPlayer& player, const LocalRealmCommand& command,
                 const std::vector<LocalRealmPlayer*>& players, std::string& result);
    bool tick(float seconds, const std::vector<LocalRealmPlayer*>& players);
    // Atomically install the authenticated session roster (never saved/client supplied).
    bool setPartyMembership(const std::vector<LocalParty>& parties);
    // Restore the consumed corpse reservation after a failed atomic realm save.
    void restoreLootable(uint64_t guid, bool lootable);
    const std::vector<LocalRealmNpc>& npcs() const;
    void setRemoteNpcs(std::vector<LocalRealmNpc> npcs);
    // Owned creatures. Authority state; a guest receives them like NPCs and
    // never creates one, and a save restores only what validLocalPets accepts.
    const std::vector<LocalRealmPet>& pets() const;
    void setRemotePets(std::vector<LocalRealmPet> pets);
    bool restorePets(std::vector<LocalRealmPet> pets, std::string& error);
    const LocalRealmPet* controlledPet(uint64_t ownerGuid) const;
    // Recent authority health changes. Observation only; not saved or replicated.
    std::vector<LocalCombatEvent> combatEvents() const;
    uint64_t overwrittenCombatEvents() const;
private:
    bool executeCastSpell(LocalRealmPlayer& player, const LocalRealmCommand& command,
        const std::vector<LocalRealmPlayer*>& players, std::string& result, bool finishing);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wowee::game
