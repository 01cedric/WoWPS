#pragma once
#include "local_group_rewards_fixture.hpp"
#include "game/local_spell_import.hpp"
#include "game/local_ignite_import.hpp"
#include "game/local_talents.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
using namespace wowee;
struct IgniteSource0237 {
    std::map<std::string,pipeline::DBCFile> tables;
    std::map<std::string,std::vector<uint8_t>> bytes;
    detail::ClientSpellTables source;
    explicit IgniteSource0237(const char* directory) {
        for(const auto* name:{"Spell","SpellRange","SpellCastTimes","SpellDuration","SpellIcon","SkillLineAbility","SkillLine","Talent","TalentTab","SpellRuneCost","SpellRadius"}) {
            std::ifstream input(std::filesystem::path(directory)/(std::string(name)+".dbc"),std::ios::binary);
            bytes[name]={std::istreambuf_iterator<char>(input),{}};assert(tables[name].load(bytes[name]));
        }
        source.spells=&tables.at("Spell");source.ranges=&tables.at("SpellRange");
        source.casts=&tables.at("SpellCastTimes");source.durations=&tables.at("SpellDuration");
        detail::ClientSpellTables::buildIndex(source.spells,source.spellIndex);
        detail::ClientSpellTables::buildIndex(source.ranges,source.rangeIndex);
        detail::ClientSpellTables::buildIndex(source.casts,source.castIndex);
        detail::ClientSpellTables::buildIndex(source.durations,source.durationIndex);
    }
    LocalSpellDefinition definition(uint8_t rank) const {
        constexpr uint32_t ids[]={11119,11120,12846,12847,12848};
        LocalSpellDefinition d;d.id=ids[rank-1];d.clientSpell=true;d.allowableClasses=128;
        const auto row=detail::ClientSpellTables::lookup(source.spellIndex,d.id);assert(row>=0);
        detail::decodeClientSpell(source,uint32_t(row),d);
        d.talentId=34;d.talentTab=41;d.talentRow=1;d.talentRank=rank;return d;
    }
    bool decode(uint8_t rank,LocalSpellDefinition& d,LocalSpellDefinition* child=nullptr) const {
        d=definition(rank);const auto row=detail::ClientSpellTables::lookup(source.spellIndex,d.id);
        return decodeClientIgniteTalent(source,uint32_t(row),d,child);
    }
    std::shared_ptr<LocalWorldContent> content() {
        auto table=[&](const char* name){return &tables.at(name);};
        auto imported=importClientStarterSpells(table("Spell"),table("SpellRange"),table("SpellCastTimes"),table("SpellDuration"),
            table("SpellIcon"),table("SkillLineAbility"),table("SkillLine"),table("Talent"),table("SpellRuneCost"),table("SpellRadius"));
        detail::importClientTalents(imported,table("Talent"),table("TalentTab"),table("Spell"),table("SpellRange"),table("SpellCastTimes"),
            table("SpellDuration"),table("SpellIcon"),table("SpellRuneCost"),table("SpellRadius"));
        auto c=rewardContent();c->spells=std::move(imported.spells);
        std::sort(c->spells.begin(),c->spells.end(),[](auto& a,auto& b){return a.id<b.id;});return c;
    }
};
