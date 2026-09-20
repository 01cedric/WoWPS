#include "game/local_area_aura.hpp"
#include <cassert>
#include <iostream>

// P03/D2 raid area aura SOURCE RULES. This suite is deliberately free of the
// authority runtime and of the client DBCs: it exercises only the predicates and
// comparators in include/game/local_area_aura.hpp, which is where the reference's
// invariants are written down. The runtime that drives them is covered by
// tools/tests/local_area_aura_runtime_test.cpp, and the column pinning of
// the seven Retribution Aura ranks by the import audit.
//
// Reference: AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33, as quoted in
// the source audit:
//   §2.5(a)  SpellInfo.cpp:1449-1466, :2201-2203 - SPELL_SPECIFIC_AURA is keyed
//            on family + SpellFamilyFlags[2] & 0x20, never on the spell id, so
//            one caster projects one aura per group and every rank shares a key.
//   §2.5(b)  SpellAuras.cpp:532-576 IsPaladinAuraDominant - three tiers in this
//            order: larger |amount|, then the target's own self-cast, then the
//            lower caster GUID.
//   §2.5(b)  SpellAuras.cpp:621-625, :739 - the losing application stays alive
//            with its effects stripped (keepEffectless), or is created with
//            effMask = 0, so a dominant neighbour can still be found.
//   §2.7     Unit.cpp:2131-2183 DealDamageShieldDamage - only aura 15, effect 0,
//            retaliates, and only for an application that still has its effects.
namespace {
using namespace wowee::game;

// The header states the mask as a literal bit set; assert it is the contiguous
// low run the "one bit outside the mask" case below assumes, so that widening
// the reviewed shape has to revisit this file rather than silently skipping it.
static_assert((kLocalAreaAuraEffectMaskAll & uint8_t(kLocalAreaAuraEffectMaskAll + 1)) == 0,
              "the admitted effect mask must be a contiguous run of low bits");
constexpr uint8_t kOutsideMask = uint8_t(kLocalAreaAuraEffectMaskAll + 1);
// validLocalAreaAuraEmitter / validLocalAreaAuraApplications bound both records
// with the same two literals. Named here so each boundary is asserted from both
// sides rather than restated.
constexpr uint32_t kMaxAmount = 100000, kMaxInstance = 65535;

constexpr uint32_t kRankOne = 7294, kRankSeven = 54043, kOtherAura = 465;
constexpr uint32_t kAmountOne = 10, kAmountSeven = 112;

LocalAreaAuraEmitter emitter(uint32_t spellId = kRankOne, uint32_t amount = kAmountOne) {
    LocalAreaAuraEmitter e;
    e.spellId = spellId; e.mapId = 0; e.instanceId = 0; e.amount = amount;
    e.effectMask = kLocalAreaAuraEffectMaskAll; e.generation = 1;
    return e;
}

LocalAreaAuraApplication application(uint64_t emitterGuid, uint32_t spellId = kRankOne,
                                     uint32_t amount = kAmountOne) {
    LocalAreaAuraApplication a;
    a.spellId = spellId; a.mapId = 0; a.instanceId = 0;
    a.emitterGuid = emitterGuid; a.emitterGeneration = 1; a.amount = amount;
    a.effectMask = kLocalAreaAuraEffectMaskAll; a.effective = true;
    return a;
}

LocalSpellDefinition auraSpell(uint32_t id, uint32_t family, uint32_t familyFlag2) {
    LocalSpellDefinition d;
    d.id = id; d.spellFamily = family; d.spellFamilyFlags = {8, 0, familyFlag2};
    return d;
}

/// IsPaladinAuraDominant is a strict order: two applications that differ in any
/// tier must disagree about which of them wins, or the reconciliation pass could
/// strip both or neither.
void assertAsymmetric(const LocalAreaAuraApplication& a, const LocalAreaAuraApplication& b,
                      uint64_t recipientGuid) {
    if (a == b) return;
    assert(localAreaAuraDominates(a, b, recipientGuid) !=
           localAreaAuraDominates(b, a, recipientGuid));
}
}

int main() {
    // --- One emitter's validity --------------------------------------------
    {
        assert(validLocalAreaAuraEmitter(emitter()));
        // A zero-amount marker emitter is real: auras 79 and 193 carry no amount
        // and exist only to occupy their bit of the mask (audit §2.8).
        assert(validLocalAreaAuraEmitter(emitter(kRankOne, 0)));

        auto bad = emitter(); bad.spellId = 0; assert(!validLocalAreaAuraEmitter(bad));
        // An emitter with no effects projects nothing; it is not a live aura.
        bad = emitter(); bad.effectMask = 0; assert(!validLocalAreaAuraEmitter(bad));
        // One bit past the reviewed three-effect shape.
        bad = emitter(); bad.effectMask = kOutsideMask; assert(!validLocalAreaAuraEmitter(bad));
        bad = emitter(); bad.effectMask = uint8_t(kLocalAreaAuraEffectMaskAll | kOutsideMask);
        assert(!validLocalAreaAuraEmitter(bad));
        // Both bounds, asserted from both sides.
        bad = emitter(); bad.amount = kMaxAmount; assert(validLocalAreaAuraEmitter(bad));
        bad.amount = kMaxAmount + 1; assert(!validLocalAreaAuraEmitter(bad));
        bad = emitter(); bad.instanceId = kMaxInstance; assert(validLocalAreaAuraEmitter(bad));
        bad.instanceId = kMaxInstance + 1; assert(!validLocalAreaAuraEmitter(bad));
        // Every single admitted effect bit is admitted on its own: the mask is a
        // set of independent effects, not an all-or-nothing profile.
        for (uint8_t bit = 1; bit <= kLocalAreaAuraEffectMaskAll; bit <<= 1) {
            auto single = emitter(); single.effectMask = bit;
            assert(validLocalAreaAuraEmitter(single));
        }
    }
    // --- The owner's whole emitter list -------------------------------------
    {
        std::vector<LocalAreaAuraEmitter> rows;
        assert(validLocalAreaAuraEmitters(rows)); // projecting nothing is valid
        for (size_t i = 0; i < kLocalMaxAreaAuraEmitters; ++i)
            rows.push_back(emitter(uint32_t(kRankOne + i)));
        assert(validLocalAreaAuraEmitters(rows));

        auto tooMany = rows;
        tooMany.push_back(emitter(uint32_t(kRankOne + kLocalMaxAreaAuraEmitters)));
        assert(!validLocalAreaAuraEmitters(tooMany));

        // SpellInfo::IsAuraExclusiveBySpecificPerCasterWith: one specific aura
        // per caster, so the same spell can never be live twice on one owner.
        auto duplicate = rows; duplicate[1].spellId = duplicate[0].spellId;
        assert(!validLocalAreaAuraEmitters(duplicate));
        // A distinct generation does not make a second copy admissible.
        duplicate[1].generation = duplicate[0].generation + 1;
        assert(!validLocalAreaAuraEmitters(duplicate));

        // One invalid member invalidates the list, wherever it sits.
        auto invalid = rows; invalid.back().effectMask = 0;
        assert(!validLocalAreaAuraEmitters(invalid));
        invalid = rows; invalid.front().spellId = 0;
        assert(!validLocalAreaAuraEmitters(invalid));
    }
    // --- A recipient's derived applications ---------------------------------
    {
        std::vector<LocalAreaAuraApplication> rows;
        assert(validLocalAreaAuraApplications(rows));
        for (size_t i = 0; i < kLocalMaxAreaAuraApplications; ++i)
            rows.push_back(application(uint64_t(i + 1)));
        assert(validLocalAreaAuraApplications(rows));

        auto tooMany = rows;
        tooMany.push_back(application(uint64_t(kLocalMaxAreaAuraApplications + 1)));
        assert(!validLocalAreaAuraApplications(tooMany));

        auto bad = rows; bad[0].spellId = 0; assert(!validLocalAreaAuraApplications(bad));
        // A derived application always names the source it came from.
        bad = rows; bad[0].emitterGuid = 0; assert(!validLocalAreaAuraApplications(bad));
        bad = rows; bad[0].effectMask = kOutsideMask;
        assert(!validLocalAreaAuraApplications(bad));
        bad = rows; bad[0].amount = kMaxAmount; assert(validLocalAreaAuraApplications(bad));
        bad[0].amount = kMaxAmount + 1; assert(!validLocalAreaAuraApplications(bad));
        bad = rows; bad[0].instanceId = kMaxInstance; assert(validLocalAreaAuraApplications(bad));
        bad[0].instanceId = kMaxInstance + 1; assert(!validLocalAreaAuraApplications(bad));

        // SpellAuras.cpp:621-625 / :739 - the application that lost arbitration
        // stays in the list. Whether it kept its mask (keepEffectless) or was
        // created with effMask = 0, it is a valid record precisely because it is
        // not effective; an EFFECTIVE application with no effects is not.
        bad = rows; bad[0].effective = false; bad[0].effectMask = 0;
        assert(validLocalAreaAuraApplications(bad));
        bad[0].effective = true;
        assert(!validLocalAreaAuraApplications(bad));

        // One application per (source, spell): a second is the same source twice.
        bad = rows; bad[1].emitterGuid = bad[0].emitterGuid;
        assert(!validLocalAreaAuraApplications(bad));
        // Same source, different spell is two different auras and is admitted.
        bad[1].spellId = kOtherAura; assert(validLocalAreaAuraApplications(bad));
        // Same spell from two different sources is the multi-source case itself.
        bad = rows; bad[1].spellId = bad[0].spellId;
        assert(validLocalAreaAuraApplications(bad));
        // A different generation does not distinguish two records of one source.
        bad = rows; bad[1].emitterGuid = bad[0].emitterGuid;
        bad[1].emitterGeneration = bad[0].emitterGeneration + 1;
        assert(!validLocalAreaAuraApplications(bad));
    }
    // --- The exclusivity key is family + family flags, never the spell id ----
    {
        // SpellInfo.cpp:2201-2203. All seven Retribution Aura ranks share one
        // record except for six columns, none of which is a family column, so
        // rank 1 and rank 7 must key the same.
        const auto rankOne = auraSpell(kRankOne, 10, 0x20);
        const auto rankSeven = auraSpell(kRankSeven, 10, 0x20);
        assert(localAreaAuraGroup(rankOne) == localAreaAuraGroup(rankSeven));
        assert(localAreaAuraGroup(rankOne).family == 10);
        const std::array<uint32_t, 3> expectedFlags{8, 0, 0x20};
        assert(localAreaAuraGroup(rankOne).familyFlags == expectedFlags);

        // A different family-flag word is a different exclusivity group: two
        // such auras coexist on one caster.
        assert(!(localAreaAuraGroup(rankOne) == localAreaAuraGroup(auraSpell(kOtherAura, 10, 0x40))));
        // Every flag word participates in the key, not only the third.
        auto otherFirstWord = rankOne; otherFirstWord.spellFamilyFlags = {16, 0, 0x20};
        assert(!(localAreaAuraGroup(rankOne) == localAreaAuraGroup(otherFirstWord)));
        auto otherSecondWord = rankOne; otherSecondWord.spellFamilyFlags = {8, 1, 0x20};
        assert(!(localAreaAuraGroup(rankOne) == localAreaAuraGroup(otherSecondWord)));
        // A different family with identical flags is a different group.
        assert(!(localAreaAuraGroup(rankOne) == localAreaAuraGroup(auraSpell(kRankOne, 11, 0x20))));
        // Two unrelated spell ids with one family profile are one group.
        assert(localAreaAuraGroup(auraSpell(1, 10, 0x20)) ==
               localAreaAuraGroup(auraSpell(2, 10, 0x20)));
    }
    // --- IsPaladinAuraDominant, tier by tier, in source order ---------------
    {
        constexpr uint64_t kRecipient = 9, kLow = 2, kHigh = 5;

        // Tier 1: larger primary amount wins, whoever cast it.
        {
            const auto strong = application(kLow, kRankOne, kAmountSeven);
            const auto weak = application(kHigh, kRankOne, kAmountOne);
            assert(localAreaAuraDominates(strong, weak, kRecipient));
            assert(!localAreaAuraDominates(weak, strong, kRecipient));
            assertAsymmetric(strong, weak, kRecipient);
            // Tier 1 is checked BEFORE the self-cast tier: the recipient's own
            // weaker aura loses to a stronger neighbour's.
            const auto ownWeak = application(kRecipient, kRankOne, kAmountOne);
            const auto neighbourStrong = application(kLow, kRankOne, kAmountSeven);
            assert(localAreaAuraDominates(neighbourStrong, ownWeak, kRecipient));
            assert(!localAreaAuraDominates(ownWeak, neighbourStrong, kRecipient));
            assertAsymmetric(neighbourStrong, ownWeak, kRecipient);
            // ... and before the GUID tier: a higher-GUID source with a larger
            // amount still wins.
            const auto highStrong = application(kHigh, kRankOne, kAmountSeven);
            const auto lowWeak = application(kLow, kRankOne, kAmountOne);
            assert(localAreaAuraDominates(highStrong, lowWeak, kRecipient));
            assertAsymmetric(highStrong, lowWeak, kRecipient);
        }
        // Tier 2: on an exact tie the recipient's own self-cast aura wins. The
        // self-caster is given the HIGHER guid on purpose, so tier 3 alone would
        // decide the other way and cannot be mistaken for this rule.
        {
            const auto own = application(kRecipient, kRankOne, kAmountOne);
            const auto neighbour = application(kLow, kRankOne, kAmountOne);
            static_assert(kRecipient > kLow, "the self-cast tier must outrank the GUID tier");
            assert(localAreaAuraDominates(own, neighbour, kRecipient));
            assert(!localAreaAuraDominates(neighbour, own, kRecipient));
            assertAsymmetric(own, neighbour, kRecipient);
            // Self-ness is relative to the recipient being reconciled, not to
            // the record: the same pair judged for a different recipient falls
            // through to the GUID tier and reverses.
            assert(!localAreaAuraDominates(own, neighbour, kHigh));
            assert(localAreaAuraDominates(neighbour, own, kHigh));
        }
        // Tier 3: a further tie is broken by the lower emitter GUID.
        {
            const auto low = application(kLow, kRankOne, kAmountOne);
            const auto high = application(kHigh, kRankOne, kAmountOne);
            assert(localAreaAuraDominates(low, high, kRecipient));
            assert(!localAreaAuraDominates(high, low, kRecipient));
            assertAsymmetric(low, high, kRecipient);
            // Neither is the recipient's own, so tier 2 abstains in both
            // directions and the order is decided purely by GUID.
            assert(low.emitterGuid != kRecipient && high.emitterGuid != kRecipient);
        }
        // Irreflexive: an application never dominates itself, so a single source
        // cannot strip its own effects.
        {
            const auto only = application(kLow, kRankOne, kAmountOne);
            assert(!localAreaAuraDominates(only, only, kRecipient));
            assert(!localAreaAuraDominates(only, only, kLow));
        }
        // Exhaustive asymmetry over the whole small space of (guid, amount)
        // pairs, judged from two different recipients.
        {
            const uint64_t guids[] = {1, 2, 9};
            const uint32_t amounts[] = {0, kAmountOne, kAmountSeven};
            for (uint64_t recipient : guids)
                for (uint64_t ga : guids)
                    for (uint32_t aa : amounts)
                        for (uint64_t gb : guids)
                            for (uint32_t ab : amounts)
                                assertAsymmetric(application(ga, kRankOne, aa),
                                                 application(gb, kRankOne, ab), recipient);
        }
    }
    // --- The damage shield reads only effective, in-place, effect-0 rows -----
    {
        constexpr uint32_t kMap = 1, kInstance = 3;
        // Unit.cpp:2131-2183 walks SPELL_AURA_DAMAGE_SHIELD, which is effect 0
        // of the reviewed profile, and takes the amount off the effect itself.
        assert(localAreaAuraShieldAmount({}, kMap, kInstance) == 0);

        auto weak = application(1, kRankOne, kAmountOne);
        auto strong = application(2, kRankOne, kAmountSeven);
        for (auto* a : {&weak, &strong}) { a->mapId = kMap; a->instanceId = kInstance; }
        assert(localAreaAuraShieldAmount({weak}, kMap, kInstance) == kAmountOne);
        assert(localAreaAuraShieldAmount({weak, strong}, kMap, kInstance) == kAmountSeven);
        assert(localAreaAuraShieldAmount({strong, weak}, kMap, kInstance) == kAmountSeven);

        // A stripped application retaliates for nothing, even while it is the
        // largest record present and keeps its effect mask (keepEffectless).
        auto stripped = strong; stripped.effective = false;
        assert(stripped.effectMask & 1);
        assert(localAreaAuraShieldAmount({weak, stripped}, kMap, kInstance) == kAmountOne);
        assert(localAreaAuraShieldAmount({stripped}, kMap, kInstance) == 0);

        // An admitted aura that carries only the two zero-amount marker effects
        // has no shield: the bit, not the amount, is what selects effect 0.
        auto markersOnly = strong;
        markersOnly.effectMask = uint8_t(kLocalAreaAuraEffectMaskAll & ~uint8_t(1));
        assert(markersOnly.effectMask);
        assert(localAreaAuraShieldAmount({weak, markersOnly}, kMap, kInstance) == kAmountOne);
        assert(localAreaAuraShieldAmount({markersOnly}, kMap, kInstance) == 0);
        // Effect 0 alone is enough.
        auto shieldOnly = strong; shieldOnly.effectMask = 1;
        assert(localAreaAuraShieldAmount({shieldOnly}, kMap, kInstance) == kAmountSeven);

        // A record left behind on another map or in another instance of the same
        // map is not in place and never retaliates.
        auto otherMap = strong; otherMap.mapId = kMap + 1;
        assert(localAreaAuraShieldAmount({weak, otherMap}, kMap, kInstance) == kAmountOne);
        assert(localAreaAuraShieldAmount({otherMap}, kMap, kInstance) == 0);
        auto otherInstance = strong; otherInstance.instanceId = kInstance + 1;
        assert(localAreaAuraShieldAmount({weak, otherInstance}, kMap, kInstance) == kAmountOne);
        assert(localAreaAuraShieldAmount({otherInstance}, kMap, kInstance) == 0);
        // The query is the recipient's own position: asked about where the
        // stronger record actually is, it answers with that record.
        assert(localAreaAuraShieldAmount({weak, otherMap}, kMap + 1, kInstance) == kAmountSeven);
        assert(localAreaAuraShieldAmount({weak, otherInstance}, kMap, kInstance + 1) == kAmountSeven);

        // A zero-amount effective shield is still nothing to retaliate with.
        auto zero = weak; zero.amount = 0;
        assert(localAreaAuraShieldAmount({zero}, kMap, kInstance) == 0);

        // Everything asserted above is a list the validator would accept, so no
        // case here is proved on a shape the authority could never produce.
        assert(validLocalAreaAuraApplications({weak, strong}) &&
               validLocalAreaAuraApplications({weak, stripped}) &&
               validLocalAreaAuraApplications({weak, markersOnly}) &&
               validLocalAreaAuraApplications({weak, otherMap}) &&
               validLocalAreaAuraApplications({weak, otherInstance}));
    }
    std::cout << "PASS P03/D2 raid area aura source rules: emitter and application validity with "
                 "both bounds, per-caster spell exclusivity and one application per (source, spell), "
                 "the losing application kept alive effect-free, the exclusivity key taken from the "
                 "spell family rather than the id, IsPaladinAuraDominant's three tiers in source "
                 "order with exhaustive asymmetry, and the damage shield reading only effective, "
                 "in-place, effect-0 applications\n";
    return 0;
}
