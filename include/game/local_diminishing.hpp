#pragma once
#include "game/local_gameplay.hpp"

namespace wowee::game {
// P04 diminishing returns. Transcribed from the pinned reference at
// 9c416aaacb5537636abb13c80f55a88947838e33:
//   SharedDefines.h:3479-3511   DiminishingReturnsType, DiminishingGroup
//   Unit.h:268-276              DiminishingLevels
//   SpellMgr.cpp:107-311        group classifier and group type
//   SpellMgr.cpp:313-401        max level, limit duration, duration-limited set
//   Unit.cpp:11292-11431        read, increment, ladder, stack
//   Spell.cpp:3195-3216         the cast-site entry rule
//
// The classifier is ported whole, including the explicit family branches no
// accepted spell reaches today. They are pure table: omitting them would let the
// next admitted spell land in the wrong group silently, which is a worse failure
// than carrying rows that never fire.

enum class LocalDiminishingGroup : uint8_t {
    None=0, Banish=1, Charge=2, OpeningStun=3, ControlledStun=4, ControlledRoot=5,
    Cyclone=6, Disarm=7, Disorient=8, Entrapment=9, Fear=10, Horror=11,
    MindControl=12, Root=13, Stun=14, ScatterShot=15, Silence=16, Sleep=17,
    Taunt=18, LimitOnly=19, DragonsBreath=20
};
enum class LocalDiminishingType : uint8_t { None=0, Player=1, All=2 };
// DIMINISHING_LEVEL_IMMUNE and DIMINISHING_LEVEL_4 are the SAME value, 3. Taunt
// alone runs to 4. Flattening the two ladders into one table would make taunt
// immune a rung early; they stay separate here for that reason.
inline constexpr uint8_t kLocalDiminishingLevel1=0, kLocalDiminishingLevel2=1,
    kLocalDiminishingLevel3=2, kLocalDiminishingLevelImmune=3, kLocalDiminishingLevelTauntImmune=4;
inline constexpr uint32_t kLocalDiminishingWindowMs=15000;

// SpellMechanic ids used by the classifier's mechanic tail. kLocalMechanicBleed,
// kLocalMechanicSilenced and kLocalMechanicStunned already live in
// local_gameplay.hpp; the rest are named here where they are first needed.
// Transcribed row by row from SharedDefines.h:1313-1349, not from memory: an
// earlier draft of this header had fear at 4 (MECHANIC_DISTRACT), knockout at
// 16 (MECHANIC_BANDAGE) and horror at 29 (MECHANIC_IMMUNE_SHIELD), and the
// knockout error alone dropped Repentance out of DIMINISHING_DISORIENT.
inline constexpr uint8_t kLocalMechanicCharm=1, kLocalMechanicDisarm=3, kLocalMechanicFear=5,
    kLocalMechanicRoot=7, kLocalMechanicSleep=10, kLocalMechanicKnockout=14,
    kLocalMechanicPolymorph=17, kLocalMechanicBanish=18, kLocalMechanicShackle=20,
    kLocalMechanicHorror=24, kLocalMechanicSapped=30;

/// Every mechanic the spell carries, parent and per-effect alike
/// (SpellInfo::GetAllEffectsMechanicMask). A 64-bit mask, not a 32-bit one:
/// the reference's own tests use 1ULL and MECHANIC_SAPPED is 30.
inline uint64_t localAllEffectsMechanicMask(const LocalSpellDefinition& d) {
    uint64_t mask=0;
    if(d.mechanic)mask|=1ULL<<d.mechanic;
    for(auto m:d.effectMechanic)if(m)mask|=1ULL<<m;
    return mask;
}

/// SpellMgr.cpp:107. `triggered` is the reference's m_triggeredByAuraSpell: a
/// stun applied by a proc lands in the shared Stun group, a deliberately cast
/// one in ControlledStun. The two diminish independently.
inline LocalDiminishingGroup localDiminishingGroupForSpell(const LocalSpellDefinition& d,bool triggered) {
    using G=LocalDiminishingGroup;
    // SpellMgr.cpp:109, IsPositive(). The reference reads a server-computed
    // AttributesCu bit this build does not have, so this is an approximation:
    // a spell with no hostile effect is treated as positive. Measured against
    // the player's own data it excludes none of the eighteen spells that reach
    // a group, and it is recorded as a divergence rather than as a port.
    if(!d.damage&&!d.periodicDamage&&!d.snarePercent&&!d.controlProfile&&!d.npcPlayerControl&&!d.npcWeaponEffect&&!d.npcWeaponPercent)return G::None;
    // SpellMgr.cpp:112-116: an aura-11 taunt short-circuits every other rule.
    // Nothing sets this today - aura 11 unlocks zero client spells and is not
    // admitted (the source audit §6.5) - so the branch is
    // reserved, not reachable, and the taunt ladder below has no producer.
    if(d.controlProfile==3)return G::Taunt;
    const auto& f=d.spellFamilyFlags;
    switch(d.spellFamily) {
        case 0: // GENERIC
            if(d.visualId==2816&&d.iconId==15)return G::ControlledStun;
            if(d.id==47481)return G::ControlledStun;
            if(d.id==7074)return G::None;
            break;
        case 1: return G::None; // UNK1, event spells
        case 3: // MAGE
            if(d.id==12494||d.id==55080)return G::Root;
            if(d.iconId==2939&&d.visualId==9963)return G::ControlledStun;
            if(d.iconId==193)return G::ControlledRoot;
            if(f[0]&0x800000u)return G::DragonsBreath;
            break;
        case 4: // WARRIOR
            if(f[0]&0x2u)return G::LimitOnly;
            if(d.id==23694)return G::Root;
            if(f[0]&0x01000000u)return G::Charge;
            break;
        case 5: // WARLOCK
            if((f[0]&0x80000000u)||(f[1]&0x200u))return G::LimitOnly;
            if(f[1]&0x10000000u)return G::Fear;
            break;
        case 7: // DRUID
            if(f[0]&0x20000u)return G::OpeningStun;
            if(f[1]&0x20u)return G::Cyclone;
            if(f[0]&0x200u)return G::ControlledRoot;
            if(f[0]&0x400u)return G::LimitOnly;
            if(d.id==45334)return G::None;
            break;
        case 8: // ROGUE
            if(f[0]&0x8u)return G::Disorient;
            if(f[0]&0x1000000u)return G::Fear;
            if(f[0]&0x400u)return G::OpeningStun;
            if(d.iconId==163)return G::LimitOnly;
            break;
        case 9: // HUNTER
            if((f[0]&0x400u)&&d.iconId==538)return G::LimitOnly;
            if((f[0]&0x40000u)&&d.iconId==132)return G::ScatterShot;
            if(d.visualId==7484&&d.iconId==20)return G::Entrapment;
            if((f[1]&0x1000u)&&d.iconId==1721)return G::Disorient;
            if(f[0]&0x8u)return G::Disorient;
            break;
        case 10: // PALADIN
            if(f[0]&0x100000u)return G::LimitOnly;
            if((f[1]&0x804000u)&&d.iconId==309)return G::Fear;
            break;
        case 11: // SHAMAN
            if(f[2]&0x4000u)return G::ControlledRoot;
            break;
        case 15: // DEATHKNIGHT
            if(d.iconId==2797)return G::Disorient;
            if((f[0]&0x10000000u)&&d.iconId==2285)return G::LimitOnly;
            break;
        default: break;
    }
    const auto m=localAllEffectsMechanicMask(d);
    const auto has=[&](uint8_t bit){return (m&(1ULL<<bit))!=0;};
    if(has(kLocalMechanicCharm))return G::MindControl;
    if(has(kLocalMechanicSilenced))return G::Silence;
    if(has(kLocalMechanicSleep))return G::Sleep;
    if(has(kLocalMechanicSapped)||has(kLocalMechanicPolymorph)||has(kLocalMechanicShackle))return G::Disorient;
    if(has(kLocalMechanicKnockout)&&d.iconId!=292)return G::Disorient;
    if(has(kLocalMechanicDisarm))return G::Disarm;
    if(has(kLocalMechanicFear))return G::Fear;
    if(has(kLocalMechanicStunned))return triggered?G::Stun:G::ControlledStun;
    if(has(kLocalMechanicBanish))return G::Banish;
    if(has(kLocalMechanicRoot))return triggered?G::Root:G::ControlledRoot;
    if(has(kLocalMechanicHorror))return G::Horror;
    return G::None;
}

/// SpellMgr.cpp:290. DRTYPE_ALL is the branch that reaches a plain creature: it
/// is the other arm of a disjunction whose DRTYPE_PLAYER arm carries the
/// ownership and flags_extra tests, so for an All group neither is ever read.
inline LocalDiminishingType localDiminishingGroupType(LocalDiminishingGroup g) {
    using G=LocalDiminishingGroup;
    switch(g) {
        case G::Taunt: case G::ControlledStun: case G::Stun: case G::OpeningStun:
        case G::Cyclone: case G::Charge: return LocalDiminishingType::All;
        case G::LimitOnly: case G::None: return LocalDiminishingType::None;
        default: return LocalDiminishingType::Player;
    }
}

/// SpellMgr.cpp:313.
inline uint8_t localDiminishingMaxLevel(LocalDiminishingGroup g) {
    return g==LocalDiminishingGroup::Taunt?kLocalDiminishingLevelTauntImmune:kLocalDiminishingLevelImmune;
}

/// SpellMgr.cpp:379.
inline bool localDiminishingDurationLimited(LocalDiminishingGroup g) {
    using G=LocalDiminishingGroup;
    switch(g) {
        case G::Banish: case G::ControlledStun: case G::ControlledRoot: case G::Cyclone:
        case G::Disorient: case G::Entrapment: case G::Fear: case G::Horror:
        case G::MindControl: case G::OpeningStun: case G::Root: case G::Stun:
        case G::Sleep: case G::LimitOnly: return true;
        default: return false;
    }
}

/// SpellMgr.cpp:324. Transcribed whole. It has NO PRODUCER in this build: the
/// clamp needs a player-owned or ALL_DIMINISH target and this realm has neither,
/// and every admitted control is shorter than the 10 s floor anyway. It is here
/// so the table is right when a producer appears; nothing claims it works.
inline uint32_t localDiminishingLimitDuration(LocalDiminishingGroup g,const LocalSpellDefinition& d) {
    if(!localDiminishingDurationLimited(g))return 0;
    const auto& f=d.spellFamilyFlags;
    switch(d.spellFamily) {
        case 7: if(f[0]&0x400u)return 40000; break;                       // Faerie Fire
        case 9: if(f[1]&0x1000u)return 6000;                              // Wyvern Sting
                if(f[0]&0x400u)return 120000; break;                      // Hunter's Mark
        case 10: if(f[0]&0x4u)return 6000; break;                         // Repentance
        case 5: if(f[1]&0x8000000u)return 6000;                           // Banish
                if(f[2]&0x800u)return 12000;                              // Curse of Tongues
                if(f[1]&0x200u)return 120000;                             // Curse of Elements
                if(f[0]&0x400000u)return 12000; break;                    // Curse of Exhaustion
        default: break;
    }
    return 10000;
}

/// Unit.cpp:11355. Two ladders, deliberately not merged.
/// `targetOptIn` is the reference's player-owned-or-ALL_DIMINISH test on the
/// TARGET. In this build it is always false: LocalNpcDefinition carries no
/// flags_extra, and zero of the 14,496 creature templates this realm can spawn
/// carry ALL_DIMINISH upstream - see the source audit §3.
/// It is a parameter rather than a dropped clause so the shape stays honest.
inline float localDiminishingMultiplier(LocalDiminishingGroup g,uint8_t level,bool targetOptIn,bool tauntOptIn) {
    if(g==LocalDiminishingGroup::Taunt) {
        if(!tauntOptIn)return 1.0f;
        switch(level) {
            case kLocalDiminishingLevel2: return 0.65f;
            case kLocalDiminishingLevel3: return 0.4225f;
            case kLocalDiminishingLevelImmune: return 0.274625f; // == LEVEL_4
            case kLocalDiminishingLevelTauntImmune: return 0.0f;
            default: return 1.0f;
        }
    }
    const auto type=localDiminishingGroupType(g);
    if(!((type==LocalDiminishingType::Player&&targetOptIn)||type==LocalDiminishingType::All))return 1.0f;
    switch(level) {
        case kLocalDiminishingLevel2: return 0.5f;
        case kLocalDiminishingLevel3: return 0.25f;
        case kLocalDiminishingLevelImmune: return 0.0f;
        default: return 1.0f;
    }
}

/// Unit.cpp:11292. Reads, and MUTATES on expiry - the reference's own
/// GetDiminishing resets hitCount in place when the window has closed. The
/// window is gated on stack == 0 and measured from removal, not from the cast,
/// so a control held for a minute never decays while it is applied.
template<class Unit>
inline uint8_t localDiminishingRead(Unit& n,LocalDiminishingGroup g,uint64_t nowMs) {
    for(auto& r:n.diminishing) {
        if(r.group!=uint8_t(g))continue;
        if(!r.hitCount||!r.hitTimeMs)return kLocalDiminishingLevel1;
        if(!r.stack&&nowMs>r.hitTimeMs&&nowMs-r.hitTimeMs>kLocalDiminishingWindowMs) {
            r.hitCount=kLocalDiminishingLevel1;return kLocalDiminishingLevel1;
        }
        return r.hitCount;
    }
    return kLocalDiminishingLevel1;
}

/// Unit.cpp:11319. A NEW record seeds at LEVEL_2, not LEVEL_1 - correct because
/// the caller has already read the level for this cast before incrementing.
template<class Unit>
inline void localDiminishingIncrement(Unit& n,LocalDiminishingGroup g,uint64_t nowMs) {
    for(auto& r:n.diminishing) {
        if(r.group!=uint8_t(g))continue;
        if(r.hitCount<localDiminishingMaxLevel(g))++r.hitCount;
        return;
    }
    if(n.diminishing.size()>=kLocalMaxNpcDiminishing)return;
    n.diminishing.push_back(LocalNpcDiminishing{uint8_t(g),kLocalDiminishingLevel2,0,nowMs});
}

/// Unit.cpp:11412. Apply raises the stack; unapply lowers it and stamps the
/// removal time at zero. Every path that removes a control must call this or the
/// stack sticks and the record never decays.
template<class Unit>
inline void localDiminishingApply(Unit& n,LocalDiminishingGroup g,bool apply,uint64_t nowMs) {
    for(auto& r:n.diminishing) {
        if(r.group!=uint8_t(g))continue;
        if(apply){if(r.stack<255)++r.stack;}
        else if(r.stack){--r.stack;if(!r.stack)r.hitTimeMs=nowMs;}
        return;
    }
}

inline bool validLocalNpcDiminishing(const LocalRealmNpc& n) {
    if(n.diminishing.size()>kLocalMaxNpcDiminishing)return false;
    for(size_t i=0;i<n.diminishing.size();++i) {
        const auto& r=n.diminishing[i];
        if(!r.group||r.group>uint8_t(LocalDiminishingGroup::DragonsBreath))return false;
        if(r.hitCount>localDiminishingMaxLevel(LocalDiminishingGroup(r.group)))return false;
        if(r.stack>kLocalMaxNpcControls)return false;
        for(size_t j=0;j<i;++j)if(n.diminishing[j].group==r.group)return false;
    }
    return true;
}
}
