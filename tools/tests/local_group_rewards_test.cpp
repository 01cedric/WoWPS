#include "local_group_rewards_fixture.hpp"
#include <iostream>
int main() {
    LocalGameplay game;game.useContent(rewardContent());
    auto a=rewardPlayer(1),b=rewardPlayer(2),c=rewardPlayer(3),d=rewardPlayer(4),e=rewardPlayer(5),outsider=rewardPlayer(6);
    std::vector<LocalRealmPlayer*> all{&outsider,&e,&d,&c,&b,&a};
    auto reset=[&]{a=rewardPlayer(1);b=rewardPlayer(2);c=rewardPlayer(3);d=rewardPlayer(4);e=rewardPlayer(5);outsider=rewardPlayer(6);};
    // No ungrouped proximity rewards; unrelated last-hit cannot steal the tag.
    rewardKill(game,outsider,all);assert(a.xp==101 && outsider.xp==0 && b.xp==0);
    assert(a.quests[0].progress[0]==1 && b.quests[0].progress[0]==0);
    assert(game.npcs()[0].lootOwner==1 && !game.npcs()[0].lootCandidates[0]);
    reset();assert(game.setPartyMembership({{1,{1,2}}}));rewardKill(game,outsider,all);
    assert(a.xp==51 && b.xp==50 && c.xp==0 && outsider.xp==0);
    assert(a.quests[0].progress[0]==1 && b.quests[0].progress[0]==1 && outsider.quests[0].progress[0]==0);
    assert(game.npcs()[0].lootOwner==1);rewardKill(game,a,all);assert(game.npcs()[0].lootOwner==2);
    // Leader order does not reset rotation. Full bags do not silently move the reservation.
    assert(game.setPartyMembership({{1,{2,1}}}));std::string result;
    b.inventory.assign(24,{117,20});assert(!game.execute(b,{LocalAction::Loot,10,0},all,result));
    assert(game.npcs()[0].lootable && game.npcs()[0].lootOwner==2 && !b.money);
    assert(!game.execute(a,{LocalAction::Loot,10,0},all,result));b.inventory.clear();
    assert(game.execute(b,{LocalAction::Loot,10,0},all,result));assert(b.inventory[0].count==1 && b.money==3 && a.money==4 && !game.npcs()[0].lootable);
    assert(!game.execute(b,{LocalAction::Loot,10,0},all,result));
    rewardKill(game,a,all);assert(game.npcs()[0].lootOwner==1);
    // Atomic roster rejection retains existing two-person membership.
    for(const auto& invalid:std::vector<std::vector<LocalParty>>{{{0,{1,2}}},{{1,{1}}},{{1,{1,1}}},{{1,{1,2}},{2,{2,3}}},{{1,{1,2,3,4,5,6}}},{{1,{1,0}}}})assert(!game.setPartyMembership(invalid));
    reset();rewardKill(game,a,all);assert(a.xp+b.xp==101 && c.xp==0);
    // Five-person XP conservation, deterministic remainder and quest credit.
    reset();assert(game.setPartyMembership({{1,{5,4,3,2,1}}}));rewardKill(game,a,all);
    assert(a.xp==21 && b.xp==20 && c.xp==20 && d.xp==20 && e.xp==20 && !outsider.xp);
    for(auto* p:std::vector<LocalRealmPlayer*>{&a,&b,&c,&d,&e})assert(p->quests[0].progress[0]==1);
    // Same map+instance, alive and <=60 yards, with zero-health inconsistency rejected.
    reset();b.mapId=1;c.instanceId=1;d.x=60.01f;e.dead=true;e.health=0;rewardKill(game,a,all);
    assert(a.xp==101 && !b.xp && !c.xp && !d.xp && !e.xp);
    reset();b.x=60;c.x=61;d.health=0;e.x=61;rewardKill(game,a,all);assert(a.xp==51 && b.xp==50 && !d.xp);
    // Cap-level members still receive objective/loot eligibility, but no XP share.
    reset();b.level=80;c.x=d.x=e.x=100;rewardKill(game,a,all);assert(a.xp==101 && b.xp==0 && b.quests[0].progress[0]==1);
    // Death-time cohort blocks late joiners. Disconnected owners fail over only to that cohort.
    reset();assert(game.setPartyMembership({}));assert(game.setPartyMembership({{1,{1,2}}}));rewardKill(game,a,all);
    assert(game.npcs()[0].lootOwner==1);assert(game.setPartyMembership({{1,{1,2,3}}}));
    std::vector<LocalRealmPlayer*> left{&b,&c};assert(game.tick(0,left));assert(game.npcs()[0].lootOwner==2);
    std::vector<LocalRealmPlayer*> newcomer{&c};game.tick(0,newcomer);assert(game.npcs()[0].lootOwner==2);
    assert(!game.execute(c,{LocalAction::Loot,10,0},newcomer,result));
    // An offline tagger's unrelated finisher receives neither XP nor quest credit.
    reset();rewardKill(game,outsider,{&outsider},rewardNpc(10,1));assert(!outsider.xp && !outsider.quests[0].progress[0]);
    // No rewards from a corpse retry, unaccepted/rewarded quests or an absent roster after session reset.
    reset();a.quests.clear();b.quests[0].status=LocalQuestStatus::Rewarded;assert(game.setPartyMembership({{1,{1,2}}}));
    rewardKill(game,a,all);assert(a.quests.empty() && b.quests[0].progress[0]==0);
    assert(!game.execute(a,{LocalAction::CastSpell,10,1},all,result));assert(a.xp+b.xp==101);
    assert(game.setPartyMembership({}));reset();rewardKill(game,a,all);assert(a.xp==101 && !b.xp);
    // 01.68: split copper independently from the assigned item bundle.
    auto prepareMoney=[&]{reset();assert(game.setPartyMembership({}));assert(game.setPartyMembership({{1,{1,2,3,4,5}}}));rewardKill(game,a,all);assert(game.npcs()[0].lootOwner==1);};
    auto takeMoney=[&]{return game.execute(a,{LocalAction::Loot,10,0},all,result);};
    prepareMoney();assert(takeMoney());assert(a.money==2 && b.money==2 && c.money==1 && d.money==1 && e.money==1 && !outsider.money);
    assert(a.inventory.size()==1 && b.inventory.empty() && c.inventory.empty());assert(!takeMoney());assert(a.money+b.money+c.money+d.money+e.money==7);
    // Eligibility is checked at collection as well as death.
    prepareMoney();b.mapId=1;c.instanceId=1;d.x=60.01f;e.dead=true;e.health=0;
    assert(takeMoney());assert(a.money==7 && !b.money && !c.money && !d.money && !e.money);
    prepareMoney();b.x=60;c.x=d.x=e.x=61;assert(takeMoney());assert(a.money==4 && b.money==3 && !c.money);
    prepareMoney();assert(game.execute(a,{LocalAction::Loot,10,0},{&a},result));assert(a.money==7 && !b.money);
    // Late joiners are not part of the death-time cohort; departing groups retain existing corpse rights.
    reset();assert(game.setPartyMembership({}));assert(game.setPartyMembership({{1,{1,2}}}));rewardKill(game,a,all);
    assert(game.setPartyMembership({{1,{1,2,3}}}));assert(takeMoney());assert(a.money==4 && b.money==3 && !c.money);
    prepareMoney();assert(game.setPartyMembership({}));assert(takeMoney());assert(a.money==2 && b.money==2);
    // A remote recipient at the cap rejects the entire item/copper bundle before mutation.
    prepareMoney();b.money=1000000000;assert(!takeMoney());assert(a.inventory.empty() && !a.money && b.money==1000000000 && !c.money && game.npcs()[0].lootable);
    b.money=999999998;assert(takeMoney());assert(a.money==2 && b.money==1000000000 && c.money==1);
    prepareMoney();a.money=1000000000;assert(!takeMoney());assert(a.inventory.empty() && !b.money && game.npcs()[0].lootable);
    // Less copper than recipients, zero copper and duplicate actor pointers do not mint money.
    auto small=rewardContent();small->npcs[0].money=2;game.useContent(small);prepareMoney();
    auto duplicateActors=all;duplicateActors.push_back(&b);assert(game.execute(a,{LocalAction::Loot,10,0},duplicateActors,result));
    assert(a.money==1 && b.money==1 && !c.money && !d.money && !e.money);
    small->npcs[0].money=0;prepareMoney();assert(takeMoney());assert(!a.money && !b.money && a.inventory.size()==1);
    game.useContent(rewardContent());reset();assert(game.setPartyMembership({}));rewardKill(game,a,all);
    assert(takeMoney());assert(a.money==7 && !b.money); // Solo money is unchanged.
    std::cout<<"PASS group copper: 2/5-recipient conservation and remainders; items stay assigned; collection-time range/map/instance/life/offline gates; late join and dissolved-group rights; exact-cap and overflow atomicity; zero/small amounts; duplicate actors; solo/retry\n";
    std::cout<<"PASS group rewards: tag ownership; outsider/bot exclusion; two/five-member conserved XP; quest gates; map/instance/range/life/cap eligibility; round-robin and leader-order stability; full bags/owner/retry; bounded atomic roster; offline owner fallback restricted to death cohort; session clear\n";
}
