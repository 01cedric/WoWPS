#include "local_party_instances_fixture.hpp"
#include <iostream>
int main(int argc,char** argv) {
    assert(argc==2);LocalGameplay game;instanceContent(game,argv[1]);std::string result;
    std::array<LocalRealmPlayer,8> p;for(size_t i=0;i<p.size();++i)p[i]=rewardPlayer(i+1);
    auto& a=p[0];auto& b=p[1];auto& outsider=p[7];
    const auto shared=enterInstance(game,outsider);
    const auto solo=enterInstance(game,outsider,45,true);assert(shared!=solo);
    assert(game.setPartyMembership({{1,{1,2,3,4,5}},{2,{6,7}}}));
    const auto first=enterInstance(game,a);assert(first!=shared && first!=solo);
    for(size_t i=1;i<5;++i)assert(enterInstance(game,p[i],45,i%2)==first);
    const auto second=enterInstance(game,p[5]);assert(second!=first);
    assert(enterInstance(game,p[6],45,true)==second);
    assert(game.instances().size()==4);
    const auto owner=game.instances()[2].groupId;assert(owner>>63);
    assert(enterInstance(game,a,78)!=first && game.instances().back().groupId==owner);
    // Promotion/member reordering cannot change a party's owner key.
    assert(game.setPartyMembership({{1,{2,1,3,4,5}},{2,{6,7}}}));assert(enterInstance(game,b)==first);
    // Removing a member revokes re-entry, but does not strand somebody already inside.
    assert(game.setPartyMembership({{1,{2,3,4,5}},{2,{6,7}}}));assert(enterInstance(game,a)==shared);
    const auto retained=b.instanceId;
    assert(game.setPartyMembership({{2,{6,7}}}));assert(b.instanceId==retained);
    assert(game.execute(b,{LocalAction::LeaveInstance},{&b},result));assert(!b.instanceId && !b.hasInstanceReturn && b.orientation==.5f);
    assert(enterInstance(game,b)==shared);
    assert(game.setPartyMembership({{3,{1,2,8}},{2,{6,7}}}));
    const auto rebuilt=enterInstance(game,a);assert(rebuilt!=first && rebuilt!=second);
    assert(enterInstance(game,outsider)==rebuilt);
    // Saved instances retain identities, but a new session's party 1 must not inherit them.
    LocalGameplay loaded;instanceContent(loaded,argv[1]);assert(loaded.restoreInstances(game.instances(),result));
    assert(loaded.setPartyMembership({{1,{1,2}}}));const auto fresh=enterInstance(loaded,a);assert(fresh!=first && fresh!=rebuilt);
    assert(loaded.instances().back().groupId!=owner && loaded.instances().back().groupId!=game.instances().back().groupId);
    // Restoring a transaction checkpoint preserves existing bindings and discards a failed first allocation.
    const auto before=game.instances();assert(game.setPartyMembership({{4,{1,2}},{2,{6,7}}}));
    const auto failed=enterInstance(game,a);assert(game.restoreInstances(before,result));
    assert(game.setPartyMembership({{4,{1,2}},{5,{3,4}},{2,{6,7}}}));
    const auto other=enterInstance(game,p[2]);assert(other==failed);
    assert(enterInstance(game,a)!=other);assert(enterInstance(game,p[5])==second);
    // Entrance, life, cooldown and boolean mode checks reject without allocating.
    const auto count=game.instances().size();entrance(a);a.x=100;
    assert(!game.execute(a,{LocalAction::EnterPortal,0,45},{&a},result));entrance(a);a.dead=true;
    assert(!game.execute(a,{LocalAction::EnterPortal,0,45},{&a},result));a.dead=false;a.portalCooldown=1;
    assert(!game.execute(a,{LocalAction::EnterPortal,0,45},{&a},result));a.portalCooldown=0;
    assert(!game.execute(a,{LocalAction::EnterPortal,owner,45},{&a},result));assert(game.instances().size()==count && a.instanceId==0);
    // Existing bounds remain fail-closed and never move the character.
    LocalGameplay full;instanceContent(full,argv[1]);std::vector<LocalInstanceState> bindings;
    for(uint32_t i=1;i<=LocalGameplay::MaxInstances;++i)bindings.push_back({i,189,i});
    assert(full.restoreInstances(bindings,result));assert(full.setPartyMembership({{1,{1,2}}}));entrance(a);
    assert(!full.execute(a,{LocalAction::EnterPortal,0,45},{&a},result));assert(a.instanceId==0 && full.instances().size()==bindings.size());
    assert(full.restoreInstances({{65535,189,99}},result));
    assert(!full.execute(a,{LocalAction::EnterPortal,0,45},{&a},result));assert(a.instanceId==0);
    std::cout<<"PASS party instances: five members, separate parties, legacy shared/private, both entry modes, promotion, removal/dissolution/reformation, multi-map owner, restart ID isolation, rollback allocation reuse, entrance/life/cooldown/mode checks, capacity and ID exhaustion\n";
}
