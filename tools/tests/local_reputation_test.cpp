#include "game/local_gameplay.hpp"
#include "game/local_services.hpp"
#include <cassert>
#include <iostream>

using namespace wowee::game;

int main() {
    LocalFactionReputationBase base;
    base.factionId=72;
    base.raceMasks={1u<<0,1u<<1,0,0};
    base.classMasks={0,0,0,0};
    base.base={3000,-3000,0,0};
    bool matched=false;
    assert(localFactionBaseReputation(base,1,1,&matched)==3000 && matched);
    assert(localFactionBaseReputation(base,2,1,&matched)==-3000 && matched);

    LocalRealmPlayer p;
    assert(localReputationStanding(p, 72) == 0);
    assert(localReputationRank(p, 72) == 3); // Neutral
    assert(localMeetsReputation(p, 72, 3));
    assert(!localMeetsReputation(p, 72, 4));

    assert(localChangeReputation(p, 72, 3000));
    assert(localReputationStanding(p, 72) == 3000);
    assert(localReputationRank(p, 72) == 4);
    assert(localMeetsReputation(p, 72, 4));
    assert(localReputationDiscountBasisPoints(localReputationRank(p, 72)) == 500);

    assert(localChangeReputation(p, 72, 6000));
    assert(localReputationRank(p, 72) == 5);
    assert(localReputationDiscountBasisPoints(localReputationRank(p, 72)) == 1000);

    assert(localChangeReputation(p, 21, -6000));
    assert(p.reputations.size() == 2);
    assert(p.reputations[0].factionId == 21 && p.reputations[1].factionId == 72);
    assert(localReputationRank(p, 21) == 1); // Hostile
    assert(validLocalReputations(p));

    assert(localChangeReputation(p, 72, 1000000));
    assert(localReputationStanding(p, 72) == 42999);
    assert(localReputationRank(p, 72) == 7);
    assert(localReputationDiscountBasisPoints(7) == 2000);
    assert(!localChangeReputation(p, 72, 1)); // already clamped

    LocalRealmPlayer invalid=p;
    invalid.reputations.push_back({72, 0});
    assert(!validLocalReputations(invalid));
    invalid=p; invalid.reputations[0].standing=-42001;
    assert(!validLocalReputations(invalid));

    LocalRealmPlayer full;
    for (uint32_t i=1;i<=kLocalMaxReputations;++i)
        assert(localChangeReputation(full, i, 1));
    assert(full.reputations.size()==kLocalMaxReputations);
    assert(!localChangeReputation(full, 999999, 1));
    assert(validLocalReputations(full));

    std::cout << "PASS 4.3 reputation core: standings/ranks/gates/clamps/bounds/discounts\n";
    return 0;
}
