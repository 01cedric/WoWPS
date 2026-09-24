// Realm-wide object state. Save rollback and replay protection use the same
// canonical vector; streaming never owns or recreates depletion timers.
const std::vector<LocalGameObjectState>& LocalGameplay::gameObjectStates() const {
    return impl_->gameObjectStates;
}
const LocalGameObjectState* LocalGameplay::gameObjectState(uint32_t id) const {
    const auto& rows=impl_->gameObjectStates;
    const auto it=std::lower_bound(rows.begin(),rows.end(),id,[](const auto& row,uint32_t key){return row.id<key;});
    return it!=rows.end() && it->id==id?&*it:nullptr;
}
bool LocalGameplay::validateGameObjectStates(const std::vector<LocalGameObjectState>& rows) const {
    if(rows.size()>kLocalMaxGameObjects)return false;
    const auto& c=*impl_->content;
    const bool references=!c.sourcePath.empty() || !c.gameObjects.empty();
    uint32_t previous=0;
    for(const auto& row:rows) {
        if(!row.id || row.id<=previous || !row.revision || row.status>kLocalGameObjectDormant || row.remainingMs>86400000 ||
           ((!row.status || row.status==kLocalGameObjectDormant) && row.remainingMs))return false;
        previous=row.id;
        if(references) {
            const auto* object=c.gameObject(row.id);
            if(!object || !localGameObjectStateful(object->kind) || row.remainingMs>object->respawnMs ||
               (row.status==kLocalGameObjectDormant && !object->poolId) ||
               (row.status && row.status!=kLocalGameObjectDormant && (object->kind==LocalGameObjectKind::Door?row.status!=1:row.status!=2)) ||
               (row.status && row.status!=kLocalGameObjectDormant && object->respawnMs && !row.remainingMs))return false;
        }
    }
    return true;
}
bool LocalGameplay::restoreGameObjectStates(const std::vector<LocalGameObjectState>& rows) {
    if(!validateGameObjectStates(rows))return false;
    std::vector<LocalGameObjectState> restored;
    if(impl_->content->sourcePath.empty() && impl_->content->gameObjects.empty())restored=rows;
    else {
        for(const auto& object:impl_->content->gameObjects)if(localGameObjectStateful(object.kind)) {
            const auto it=std::lower_bound(rows.begin(),rows.end(),object.id,[](const auto& row,uint32_t id){return row.id<id;});
            // A spawn newly placed into a pool starts dormant; the pool then
            // chooses its members as it does for a fresh realm.
            restored.push_back(it!=rows.end() && it->id==object.id?*it:LocalGameObjectState{object.id,1,uint8_t(object.poolId?kLocalGameObjectDormant:0),0});
        }
    }
    impl_->gameObjectStates=std::move(restored);impl_->normalizeGameObjectPools();return true;
}
const LocalGameObject* LocalGameplay::nearbyGameObject(const LocalRealmPlayer& player) const {
    const LocalGameObject* best=nullptr;float nearest=std::numeric_limits<float>::infinity();
    for(const auto& object:content().gameObjects) {
        if(!localGameObjectUsable(object,player,content()))continue;
        if(localGameObjectStateful(object.kind)) {
            const auto* state=gameObjectState(object.id);if(!state || state->status==kLocalGameObjectDepleted || state->status==kLocalGameObjectDormant)continue;
        }
        const float distance=distance2(object.x,object.y,object.z,player.x,player.y,player.z);
        if(distance<nearest){best=&object;nearest=distance;}
    }
    return best;
}
