#include "game/local_party.hpp"
#include "game/local_party_client.hpp"
#include <cassert>
#include <iostream>
#include <set>
using namespace wowee::game;
int main(){
    LocalPartyDirector d;std::string result;double now=1;
    std::vector<LocalPartyActor> online={{1,1},{2,1},{3,3},{4,1},{5,1},{6,1},{7,2}};
    auto run=[&](LocalPartyAction a,uint64_t who,uint64_t target=0,uint32_t invite=0){return d.execute(a,who,target,invite,now,online,result);};
    auto invite=[&](uint64_t a,uint64_t b){assert(run(LocalPartyAction::Invite,a,b));return d.invitation(b)->id;};
    assert(!run(LocalPartyAction::Invite,1,1));assert(!run(LocalPartyAction::Invite,1,99));assert(!run(LocalPartyAction::Invite,1,7));
    auto two=invite(1,2),three=invite(1,3);
    assert(!run(LocalPartyAction::Invite,4,2));assert(!run(LocalPartyAction::Invite,2,4));
    assert(!run(LocalPartyAction::Accept,2,0,three));assert(!d.party(2));
    assert(run(LocalPartyAction::Accept,3,0,three));assert(d.party(1)->members==std::vector<uint64_t>({1,3}));
    assert(d.invitation(2)->partyId==d.party(1)->id);
    assert(run(LocalPartyAction::Accept,2,0,two));assert(d.party(2)->members.size()==3);
    assert(!run(LocalPartyAction::Accept,2,0,two));assert(!run(LocalPartyAction::Invite,3,4));
    invite(1,4);assert(run(LocalPartyAction::Promote,1,2));assert(!d.invitation(4));
    assert(!run(LocalPartyAction::Remove,1,3));assert(!run(LocalPartyAction::Promote,1,3));
    auto four=invite(2,4);assert(run(LocalPartyAction::Accept,4,0,four));
    auto five=invite(2,5),six=invite(2,6);assert(run(LocalPartyAction::Accept,5,0,five));
    assert(d.party(2)->members.size()==5 && !d.invitation(6));assert(!run(LocalPartyAction::Accept,6,0,six));
    assert(!run(LocalPartyAction::Invite,2,6));assert(run(LocalPartyAction::Leave,2));
    assert(d.party(1)->members.front()==3 && d.party(1)->members.size()==4);
    assert(run(LocalPartyAction::Remove,3,4));assert(!d.party(4));
    online.erase(online.begin()+2);d.prune(now,online);
    assert(d.party(1)->members==std::vector<uint64_t>({1,5}));
    assert(run(LocalPartyAction::Leave,5));assert(!d.party(1));
    two=invite(1,2);assert(run(LocalPartyAction::Decline,2,0,two));assert(!d.invitation(2));
    two=invite(1,2);now+=LocalPartyDirector::InviteSeconds;d.prune(now,online);
    assert(!d.invitation(2) && !run(LocalPartyAction::Accept,2,0,two));
    two=invite(1,2);online.erase(online.begin());d.prune(now,online);assert(!d.invitation(2));
    assert(!run(LocalPartyAction(99),2));
    // Incoming invitation acceptance invalidates the target's outgoing invites.
    LocalPartyDirector other;std::vector<LocalPartyActor> actors={{1,1},{2,1},{3,1},{4,1}};
    assert(other.execute(LocalPartyAction::Invite,2,3,0,0,actors,result));
    assert(other.execute(LocalPartyAction::Invite,1,2,0,0,actors,result));
    const auto incoming=other.invitation(2)->id;
    assert(other.execute(LocalPartyAction::Accept,2,0,incoming,0,actors,result));
    assert(!other.invitation(3));
    // Withdrawal uses the original remove/uninvite action and checks sender.
    LocalPartyDirector withdraw;std::vector<LocalPartyActor> available={{1,1},{2,1},{3,1},{4,1}};
    auto issue=[&](uint64_t from,uint64_t to){assert(withdraw.execute(LocalPartyAction::Invite,from,to,0,0,available,result));return withdraw.invitation(to)->id;};
    auto token=issue(1,2);assert(!withdraw.execute(LocalPartyAction::Remove,3,2,0,0,available,result));
    assert(withdraw.invitation(2)->id==token);
    assert(withdraw.execute(LocalPartyAction::Remove,1,2,0,0,available,result));
    assert(!withdraw.invitation(2) && !withdraw.party(1));
    assert(!withdraw.execute(LocalPartyAction::Accept,2,0,token,0,available,result));
    auto newer=issue(1,2);assert(newer!=token);
    assert(!withdraw.execute(LocalPartyAction::Accept,2,0,token,0,available,result));
    assert(withdraw.execute(LocalPartyAction::Accept,2,0,newer,0,available,result));
    token=issue(1,3);assert(withdraw.execute(LocalPartyAction::Remove,1,3,0,0,available,result));
    assert(withdraw.party(1)->members.size()==2 && !withdraw.invitation(3));
    std::cout<<"PASS invitation withdrawal: sender-only cancellation while solo/grouped, stale acceptance rejection, replacement tokens, membership preserved\n";
    // Native party resolution uses exactly the same order, excluding self.
    LocalPartyView view;view.partyId=1;
    LocalPartyMember host;host.guid=1;host.name="Host";host.maxHealth=100;host.health=50;host.maxPower=99999;host.power=88888;
    LocalPartyMember guest=host;guest.guid=2;guest.name="Guest";guest.x=99999;guest.y=-99999;
    view.members={host,guest};GroupListData client;updateLocalPartyClientData(client,view,1);
    assert(client.leaderGuid==1 && client.memberCount==1 && client.members[0].guid==2 && client.members[0].curHealth==50);
    assert(client.lootMethod==1 && client.lootThreshold==2);
    assert(client.members[0].maxPower==65535 && client.members[0].posX==32767 && client.members[0].posY==-32768);
    updateLocalPartyClientData(client,{},1);assert(client.isEmpty() && client.members.empty() && !client.leaderGuid && client.lootMethod==0);
    std::cout<<"PASS party authority: same-faction/online/leader gates; invite ownership/expiry/decline/rebinding; five-player cap; promotion/removal/leave/disconnect/succession/dissolution; native roster ordering and numeric bounds\n";
}
