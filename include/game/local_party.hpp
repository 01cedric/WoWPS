#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <utility>

namespace wowee::game {

// Session-only membership. This does not change loot, XP, quests or instance
// bindings. Only authenticated human players are supplied as actors by LocalRealm.
struct LocalPartyActor { uint64_t guid = 0; uint8_t race = 0; };
struct LocalParty {
    uint32_t id = 0;
    std::vector<uint64_t> members; // Leader first, at least two, at most five.
};
struct LocalPartyInvite {
    uint32_t id = 0, partyId = 0;
    uint64_t from = 0, to = 0;
    double expires = 0;
};
enum class LocalPartyAction { Invite, Accept, Decline, Leave, Remove, Promote };

class LocalPartyDirector {
public:
    static constexpr size_t MaxMembers = 5, MaxParties = 50, MaxInvites = 100;
    static constexpr double InviteSeconds = 30;
    const LocalParty* party(uint64_t player) const {
        for (const auto& p : parties_)
            if (std::find(p.members.begin(), p.members.end(), player) != p.members.end()) return &p;
        return nullptr;
    }
    const LocalPartyInvite* invitation(uint64_t player) const {
        for (const auto& invite : invites_) if (invite.to == player) return &invite;
        return nullptr;
    }
    const std::vector<LocalParty>& parties() const { return parties_; }
    const std::vector<LocalPartyInvite>& invitations() const { return invites_; }

    void prune(double now, const std::vector<LocalPartyActor>& online) {
        for (auto& p : parties_) {
            p.members.erase(std::remove_if(p.members.begin(), p.members.end(),
                [&](uint64_t id) { return !actor(online, id); }), p.members.end());
        }
        // The first remaining member becomes leader after a disconnect/leave.
        parties_.erase(std::remove_if(parties_.begin(), parties_.end(),
            [](const auto& p) { return p.members.size() < 2; }), parties_.end());
        invites_.erase(std::remove_if(invites_.begin(), invites_.end(), [&](const auto& i) {
            const auto* p = party(i.from);
            return now >= i.expires || !actor(online, i.from) || !actor(online, i.to) || party(i.to) ||
                (p ? p->id != i.partyId || p->members.front() != i.from || p->members.size() >= MaxMembers : i.partyId != 0);
        }), invites_.end());
    }

    bool execute(LocalPartyAction action, uint64_t self, uint64_t target, uint32_t expectedInvite,
                 double now, const std::vector<LocalPartyActor>& online, std::string& result) {
        if (unsigned(action)>unsigned(LocalPartyAction::Promote)) { result="Invalid party action";return false; }
        prune(now, online);
        const auto reject = [&](const char* reason) { result = reason; return false; };
        const auto* source = actor(online, self);
        if (!source) return reject("Only connected players can manage a party");
        const auto* current = party(self);
        if (action == LocalPartyAction::Invite) {
            const auto* destination = actor(online, target);
            if (!target || target == self || !destination) return reject("Choose another connected player");
            if (!team(source->race) || team(source->race) != team(destination->race))
                return reject("You can only invite players of your faction");
            if (current && current->members.front() != self) return reject("Only the party leader can invite players");
            if (current && current->members.size() >= MaxMembers) return reject("The party is full (five players)");
            if (party(target)) return reject("That player is already in a party");
            if (invitation(target)) return reject("That player already has a pending invitation");
            if (invitation(self)) return reject("Answer your current party invitation first");
            if (invites_.size() >= MaxInvites || nextInvite_ == UINT32_MAX) return reject("Party invitation limit reached");
            invites_.push_back({++nextInvite_, current ? current->id : 0, self, target, now + InviteSeconds});
            result = "Party invitation sent"; return true;
        }
        if (action == LocalPartyAction::Accept || action == LocalPartyAction::Decline) {
            const auto* pending = invitation(self);
            if (!pending || !expectedInvite || pending->id != expectedInvite)
                return reject("That party invitation has expired or changed");
            const auto invite = *pending;
            if (action == LocalPartyAction::Decline) {
                eraseInvite(invite.id); result = "Party invitation declined"; return true;
            }
            const auto* leader = actor(online, invite.from);
            if (!leader || team(leader->race) != team(source->race)) return reject("The inviter is no longer available");
            // Copy the bounded directory first: allocation failure cannot leave
            // half a group or consume an invitation that was never accepted.
            auto next = parties_;
            if (invite.partyId) {
                auto p = std::find_if(next.begin(), next.end(), [&](const auto& v) { return v.id == invite.partyId; });
                if (p == next.end() || p->members.front() != invite.from || p->members.size() >= MaxMembers)
                    return reject("The party is no longer accepting this invitation");
                p->members.push_back(self);
            } else {
                if (next.size() >= MaxParties || nextParty_ == UINT32_MAX) return reject("Party limit reached");
                next.push_back({nextParty_ + 1, {invite.from, self}});
            }
            parties_ = std::move(next);
            if (!invite.partyId) {
                ++nextParty_;
                // Other invitations issued before the first acceptance belong
                // to this newly created party, not to a second overlapping one.
                for (auto& i : invites_) if (i.from == invite.from && !i.partyId) i.partyId = nextParty_;
            }
            eraseInvite(invite.id); prune(now, online);
            result = "Joined the party"; return true;
        }
        // Uninvite also withdraws an invitation sent by this player. It must
        // not remove another leader's invitation or accidentally create a party.
        if (action == LocalPartyAction::Remove) {
            if (const auto* pending = invitation(target); pending && pending->from == self) {
                eraseInvite(pending->id); result = "Party invitation withdrawn"; return true;
            }
        }
        if (!current) return reject("You are not in a party");
        const uint32_t id = current->id;
        if (action != LocalPartyAction::Leave && current->members.front() != self)
            return reject("Only the party leader can do that");
        const uint64_t selected = action == LocalPartyAction::Leave ? self : target;
        if (!selected || (action != LocalPartyAction::Leave && selected == self) ||
            std::find(current->members.begin(), current->members.end(), selected) == current->members.end())
            return reject("Choose another member of your party");
        auto p = std::find_if(parties_.begin(), parties_.end(), [&](const auto& v) { return v.id == id; });
        auto member = std::find(p->members.begin(), p->members.end(), selected);
        if (action == LocalPartyAction::Promote) {
            std::iter_swap(p->members.begin(), member); result = "Party leader changed";
        } else {
            p->members.erase(member); result = action == LocalPartyAction::Leave ? "Left the party" : "Player removed from the party";
        }
        prune(now, online); return true;
    }

private:
    static const LocalPartyActor* actor(const std::vector<LocalPartyActor>& actors, uint64_t guid) {
        for (const auto& a : actors) if (a.guid == guid) return &a;
        return nullptr;
    }
    static uint8_t team(uint8_t race) {
        switch (race) {
            case 1: case 3: case 4: case 7: case 11: return 1;
            case 2: case 5: case 6: case 8: case 10: return 2;
            default: return 0;
        }
    }
    void eraseInvite(uint32_t id) {
        invites_.erase(std::remove_if(invites_.begin(), invites_.end(),
            [&](const auto& i) { return i.id == id; }), invites_.end());
    }
    std::vector<LocalParty> parties_;
    std::vector<LocalPartyInvite> invites_;
    uint32_t nextParty_ = 0, nextInvite_ = 0;
};

// Public UI/transport data deliberately excludes bags, bank, recipes and quest
// history. Party health updates must not copy entire saved characters.
struct LocalPartyMember {
    uint64_t guid = 0;
    std::string name;
    uint32_t mapId = 0, instanceId = 0, health = 0, maxHealth = 0, power = 0, maxPower = 0;
    float x = 0, y = 0, z = 0;
    uint8_t race = 0, classId = 0, level = 0, powerType = 0;
    bool dead = false;
    bool operator==(const LocalPartyMember&) const = default;
};
struct LocalPartyView {
    uint32_t partyId = 0, inviteId = 0;
    uint64_t inviter = 0;
    std::string inviterName;
    std::vector<LocalPartyMember> members; // Includes self; leader first.
    bool operator==(const LocalPartyView&) const = default;
};

} // namespace wowee::game
