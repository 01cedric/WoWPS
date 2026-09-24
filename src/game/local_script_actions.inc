namespace {
uint64_t nextScriptCounter(uint64_t value) {
    if (!++value) ++value;
    return value;
}
}

bool LocalGameplay::executeScriptActions(const std::vector<uint32_t>& actionIds,
                                         const std::vector<LocalRealmPlayer*>& scope,
                                         std::string& error) {
    return executeScriptActionsScoped(actionIds,scope,scope,error);
}
bool LocalGameplay::executeScriptActions(const std::vector<uint32_t>& actionIds,
        const std::vector<LocalRealmPlayer*>& viewers,
        const std::vector<LocalRealmPlayer*>& authorityPlayers,std::string& error) {
    return executeScriptActionsScoped(actionIds,viewers,authorityPlayers,error);
}
bool LocalGameplay::executeScriptActionsScoped(const std::vector<uint32_t>& actionIds,
        const std::vector<LocalRealmPlayer*>& viewerScope,
        const std::vector<LocalRealmPlayer*>& authorityPlayers,std::string& error) {
    return impl_->executeScriptActionsScoped(actionIds,viewerScope,authorityPlayers,error);
}
bool LocalGameplay::Impl::executeScriptActionsScoped(const std::vector<uint32_t>& actionIds,
        const std::vector<LocalRealmPlayer*>& viewerScope,
        const std::vector<LocalRealmPlayer*>& authorityPlayers,std::string& error) {
    auto& g=*this;
    if(actionIds.size()>kLocalMaxScriptActionBatch){error="Script action batch is too large";return false;}
    std::vector<LocalRealmPlayer*> viewers;
    viewers.reserve(viewerScope.size());
    for(auto* player:viewerScope) {
        if(!player || !player->guid){error="Script action scope contains an invalid player";return false;}
        if(std::any_of(viewers.begin(),viewers.end(),[&](const auto* other){return other->guid==player->guid;})) {
            error="Script action scope contains a duplicate player";return false;
        }
        viewers.push_back(player);
    }
    std::sort(viewers.begin(),viewers.end(),[](const auto* a,const auto* b){return a->guid<b->guid;});
    std::vector<LocalRealmPlayer*> roster;
    roster.reserve(authorityPlayers.size());
    for(auto* player:authorityPlayers) {
        if(!player || !player->guid || std::any_of(roster.begin(),roster.end(),[&](const auto* other){return other->guid==player->guid;})) {
            error="Script action authority scope is invalid";return false;
        }
        roster.push_back(player);
    }

    auto stagedNpcs=g.npcs;
    auto stagedDialogues=g.scriptDialogues;
    uint64_t stagedDialogueRevision=g.scriptDialogueRevision;
    uint64_t stagedOverwritten=g.overwrittenScriptDialogues;
    uint64_t stagedNpcEpoch=g.nextNpcEpoch;
    const auto actor=[&](uint32_t actorId)->LocalRealmNpc* {
        const auto it=std::find_if(stagedNpcs.begin(),stagedNpcs.end(),[&](const auto& npc){return npc.scriptActorId==actorId&&!npc.scriptActorRetired;});
        return it==stagedNpcs.end()?nullptr:&*it;
    };
    const auto sameScope=[&](uint32_t mapId,uint32_t instanceId) {
        return std::all_of(viewers.begin(),viewers.end(),[&](const auto* player){
            return player->mapId==mapId && player->instanceId==instanceId;
        });
    };
    const auto appendDialogue=[&](uint64_t speaker,const LocalRealmPlayer& viewer,
                                  uint32_t mapId,uint32_t instanceId,const std::string& text) {
        if(stagedDialogues.size()==kLocalMaxScriptDialogues) {
            stagedDialogues.erase(stagedDialogues.begin());++stagedOverwritten;
        }
        stagedDialogueRevision=nextScriptCounter(stagedDialogueRevision);
        stagedDialogues.push_back({stagedDialogueRevision,speaker,viewer.guid,mapId,instanceId,text});
    };

    for(const auto id:actionIds) {
        const auto* action=g.content->scriptAction(id);
        if(!action){error="Script action reference is missing";return false;}
        if(action->kind==LocalScriptActionKind::Spawn) {
            if(!sameScope(action->mapId,action->instanceId)){error="Script actor spawn is outside its player scope";return false;}
            if(actor(action->actorId)){error="Script actor is already spawned";return false;}
            if(std::count_if(stagedNpcs.begin(),stagedNpcs.end(),[](const auto& npc){return npc.scriptActorId&&!npc.scriptActorRetired;})>=kLocalMaxScriptActors ||
               (stagedNpcs.size()>=LocalGameplay::MaxNpcs &&
                std::none_of(stagedNpcs.begin(),stagedNpcs.end(),[](const auto& npc){return npc.scriptActorRetired;})))
                {error="Script actor capacity is exhausted";return false;}
            if(!g.content->npc(action->npcEntry)){error="Script actor NPC definition is missing";return false;}
            LocalNpcSpawn spawn;spawn.id=0x80000000u|action->actorId;spawn.entry=action->npcEntry;
            spawn.mapId=action->mapId;spawn.x=action->x;spawn.y=action->y;spawn.z=action->z;spawn.orientation=action->orientation;
            stagedNpcEpoch=nextScriptCounter(stagedNpcEpoch);
            auto npc=g.makeNpcAtEpoch(spawn,action->instanceId,stagedNpcEpoch);
            npc.scriptActorId=action->actorId;npc.scriptLifetimeMs=action->lifetimeMs;
            const auto retired=std::find_if(stagedNpcs.begin(),stagedNpcs.end(),[](const auto& current){return current.scriptActorRetired;});
            if(std::any_of(stagedNpcs.begin(),stagedNpcs.end(),[&](const auto& current){
                   return current.guid==npc.guid && (retired==stagedNpcs.end() || &current!=&*retired);
               })) {
                error="Script actor GUID collides with an existing NPC";return false;
            }
            if(retired==stagedNpcs.end())stagedNpcs.push_back(std::move(npc));else *retired=std::move(npc);
            continue;
        }
        if(action->kind==LocalScriptActionKind::Dialogue) {
            if(viewers.empty()){error="Script dialogue has no viewer";return false;}
            if(action->actorId==kLocalScriptPlayerActor) {
                for(const auto* viewer:viewers)appendDialogue(viewer->guid,*viewer,viewer->mapId,viewer->instanceId,action->text);
            } else {
                const auto* speaker=actor(action->actorId);
                if(!speaker || speaker->dead || !speaker->health){error="Script dialogue speaker is unavailable";return false;}
                if(!sameScope(speaker->mapId,speaker->instanceId)){error="Script dialogue speaker is outside its player scope";return false;}
                for(const auto* viewer:viewers)appendDialogue(speaker->guid,*viewer,speaker->mapId,speaker->instanceId,action->text);
            }
            continue;
        }
        auto* source=actor(action->actorId);
        if(!source){error="Script actor is not spawned";return false;}
        if(action->kind==LocalScriptActionKind::Despawn) {
            const auto guid=source->guid;
            // The caller supplies every player in the affected realm scope.
            // Refuse rather than partially cleaning state outside this staged
            // NPC/dialogue transaction. Timed expiry performs full cleanup.
            if(std::any_of(roster.begin(),roster.end(),[&](const auto* player){
                   return player->vehicleGuid==guid || player->attackTarget==guid || player->rangedTarget==guid ||
                          player->castTarget==guid || player->comboTarget==guid;
               }) || std::any_of(g.pets.begin(),g.pets.end(),[&](const auto& pet){return pet.targetGuid==guid;}) ||
               std::any_of(g.petCasts.begin(),g.petCasts.end(),[&](const auto& row){return row.second.target==guid;}) ||
               std::any_of(g.petMissiles.begin(),g.petMissiles.end(),[&](const auto& row){return row.target==guid;}) ||
               std::any_of(g.vehicleProjectiles.begin(),g.vehicleProjectiles.end(),[&](const auto& row){return row.sourceGuid==guid;}) ||
               std::any_of(g.vehicleCasts.begin(),g.vehicleCasts.end(),[&](const auto& row){return row.sourceGuid==guid || row.targetGuid==guid;}) ||
               g.escortCombat.contains(guid) || source->escortOwner || source->transportEntry) {
                error="Referenced script actor cannot despawn";return false;
            }
            for(auto& npc:stagedNpcs)if(npc.targetGuid==guid){npc.targetGuid=0;npc.threat={};}
            source->scriptActorRetired=true;source->targetGuid=0;source->threat={};source->health=0;source->dead=true;
            continue;
        }
        if(action->kind==LocalScriptActionKind::Move) {
            if(source->mapId!=action->mapId || source->instanceId!=action->instanceId ||
               !sameScope(action->mapId,action->instanceId)) {error="Script actor move crosses its world scope";return false;}
            const float squared=distance2(source->x,source->y,source->z,action->x,action->y,action->z);
            // Script moves are short authority steps, not teleportation. Longer
            // paths must be authored as several collision-checked actions.
            if(!std::isfinite(squared) || squared>100.f*100.f ||
               !g.collision.isInLineOfSight(action->mapId,source->x,source->y,source->z,
                                             action->x,action->y,action->z,false)) {
                error="Script actor move is too long or obstructed";return false;
            }
            source->x=source->homeX=action->x;source->y=source->homeY=action->y;
            source->z=source->homeZ=action->z;source->orientation=action->orientation;
            continue;
        }
        if(source->dead || !source->health){error="Script combat actor is unavailable";return false;}
        uint64_t targetGuid=0;uint32_t targetMap=0,targetInstance=0;
        if(action->targetActorId) {
            const auto* target=actor(action->targetActorId);
            if(!target || target->dead || !target->health){error="Script combat target is unavailable";return false;}
            targetGuid=target->guid;targetMap=target->mapId;targetInstance=target->instanceId;
        } else {
            if(viewers.size()!=1 || viewers.front()->dead || !viewers.front()->health) {
                error="Player-targeted script combat requires one living player";return false;
            }
            targetGuid=viewers.front()->guid;targetMap=viewers.front()->mapId;targetInstance=viewers.front()->instanceId;
        }
        if(source->mapId!=targetMap || source->instanceId!=targetInstance) {
            error="Script combat target is outside the actor scope";return false;
        }
        source->targetGuid=targetGuid;source->hostile=true;source->aggressive=true;
        auto threat=std::find_if(source->threat.begin(),source->threat.end(),[&](const auto& row){return row.guid==targetGuid;});
        if(threat==source->threat.end())threat=std::find_if(source->threat.begin(),source->threat.end(),[](const auto& row){return !row.guid;});
        if(threat==source->threat.end()){error="Script combat actor threat list is full";return false;}
        threat->guid=targetGuid;threat->amount=std::max<uint64_t>(threat->amount,1000);
    }
    // npcs is permanently reserved to MaxNpcs. Assign existing slots and append
    // new ones in place so combat callers may safely retain references until
    // their current stack unwinds. Retired actors are hidden tombstones and are
    // erased only at the stable end-of-tick boundary.
    if(g.npcs.size()>stagedNpcs.size()){error="Script action staging lost NPC rows";return false;}
    if(g.npcs.capacity()<LocalGameplay::MaxNpcs){error="Script actor storage is not prepared";return false;}
    const auto common=std::min(g.npcs.size(),stagedNpcs.size());
    for(size_t i=0;i<common;++i)g.npcs[i]=std::move(stagedNpcs[i]);
    for(size_t i=common;i<stagedNpcs.size();++i)g.npcs.push_back(std::move(stagedNpcs[i]));
    g.scriptDialogues=std::move(stagedDialogues);
    g.scriptDialogueRevision=stagedDialogueRevision;g.overwrittenScriptDialogues=stagedOverwritten;
    g.nextNpcEpoch=stagedNpcEpoch;error.clear();return true;
}

LocalScriptActionCheckpoint LocalGameplay::scriptActionCheckpoint() const {
    return {impl_->npcs,impl_->scriptDialogues,impl_->pendingScriptCommits,impl_->scriptDialogueRevision,
            impl_->overwrittenScriptDialogues,impl_->nextNpcEpoch};
}
void LocalGameplay::restoreScriptActionCheckpoint(LocalScriptActionCheckpoint checkpoint) {
    impl_->npcs=std::move(checkpoint.npcs);impl_->npcs.reserve(MaxNpcs);
    impl_->scriptDialogues=std::move(checkpoint.dialogues);
    impl_->pendingScriptCommits=std::move(checkpoint.pendingKills);
    impl_->scriptDialogueRevision=checkpoint.dialogueRevision;
    impl_->overwrittenScriptDialogues=checkpoint.overwrittenDialogues;impl_->nextNpcEpoch=checkpoint.nextNpcEpoch;
}
bool LocalGameplay::validPendingScriptKills(const std::vector<LocalPendingScriptKill>& kills) {
    if(kills.size()>MaxPendingScriptKills)return false;
    for(size_t i=0;i<kills.size();++i) {
        const auto& kill=kills[i];
        if(!kill.playerGuid || !kill.npcEntry || !kill.count ||
           kill.xp>uint64_t(UINT32_MAX)*kill.count)return false;
        if(i && kills[i-1].playerGuid==kill.playerGuid && kills[i-1].npcEntry==kill.npcEntry)
            return false; // Runtime canonical form always run-length encodes this pair.
    }
    return true;
}
const std::vector<LocalPendingScriptKill>& LocalGameplay::pendingScriptKills() const {
    return impl_->pendingScriptCommits;
}
bool LocalGameplay::restorePendingScriptKills(std::vector<LocalPendingScriptKill> kills) {
    if(!validPendingScriptKills(kills))return false;
    impl_->pendingScriptCommits=std::move(kills);return true;
}
const std::vector<LocalScriptDialogue>& LocalGameplay::scriptDialogues() const{return impl_->scriptDialogues;}
bool LocalGameplay::setRemoteScriptDialogues(std::vector<LocalScriptDialogue> dialogues) {
    if(dialogues.size()>kLocalMaxScriptDialogues)return false;
    uint64_t revision=0;
    for(const auto& row:dialogues) {
        if(!row.revision || row.revision<=revision || !row.speakerGuid || !row.viewerGuid ||
           row.mapId>10000 || row.instanceId>65535 ||
           row.text.empty() || row.text.size()>255 || row.text.find('\0')!=std::string::npos ||
           !localScriptDialogueTypeValid(row.chatType))return false;
        revision=row.revision;
    }
    impl_->scriptDialogues=std::move(dialogues);
    impl_->scriptDialogueRevision=revision;
    return true;
}
uint64_t LocalGameplay::overwrittenScriptDialogues() const{return impl_->overwrittenScriptDialogues;}
