#include "game/local_talents.hpp"
#include "core/application.hpp"
#include "ui/local_nameplate_policy.hpp"
#include "ui/local_pad_focus.hpp"
#include "addons/addon_manager.hpp"
#include "addons/lua_engine.hpp"
#include "addons/local_vehicle_api.hpp"
#include "core/coordinates.hpp"
#include "game/local_realm.hpp"
#include "game/local_services.hpp"
#include "game/local_target_selection.hpp"
#include "game/local_quest_dialogue.hpp"
#include "game/game_handler.hpp"
#include "rendering/renderer.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/camera.hpp"
#include "ui/ui_texture_load.hpp"
#include "ui/ui_upload_budget.hpp"
#include "ui/wotlk_button_style.hpp"
#include "ui/keybinding_manager.hpp"
#include "ui/local_action_cooldown.hpp"
#include "core/logger.hpp"
#include "core/window.hpp"
#include "pipeline/asset_manager.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#ifdef WOWEE_PS4
#include "platform/ps4/input_ps4.hpp"
#include <orbis/Pad.h>
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace wowee::core {
namespace {
float distanceTo(const game::LocalRealmPlayer& player, const game::LocalRealmNpc& npc) {
    const float x = player.x - npc.x, y = player.y - npc.y, z = player.z - npc.z;
    return std::sqrt(x*x + y*y + z*z);
}
// questProgress, rewarded and statBar used to live here. They were this file's
// own quest log and stat bars, and FrameXML owns both now - B38 made the
// original interface mandatory on the console rather than a fallback, which
// left them referenced by nothing. Removed rather than commented out: dead
// code is a build failure under -Werror, and the interface that replaced them
// is the one to change.
std::string itemName(const game::LocalWorldContent& content, uint32_t id) {
    const auto* item = content.item(id);
    return item ? item->name : "Item " + std::to_string(id);
}
std::string objectiveName(const game::LocalWorldContent& content, const game::LocalQuestObjective& objective) {
    if (objective.type == game::LocalQuestObjective::Type::Collect) return itemName(content, objective.entry);
    if(objective.type==game::LocalQuestObjective::Type::Script)return objective.text;
    const auto* npc = content.npc(objective.entry);
    return npc ? npc->name : "Target " + std::to_string(objective.entry);
}
}

void Application::renderLocalRealmOverlay() {
    if (!localRealmEntered_ || !localRealm_ || state != AppState::IN_GAME || !renderer) {
        localVehicleAim_={};return;
    }
    const auto* livePlayer = localRealm_->localPlayer();
    if (!livePlayer) { localVehicleAim_={};return; }
    // Original FrameXML controls and the fallback share one acknowledged aim.
    auto& localVehicleAim_=gameHandler?gameHandler->localVehicleAimInput():this->localVehicleAim_;
    // Action methods may mutate snapshots; retain a stable view for this frame.
    const auto self = *livePlayer;
    if (self.dead) {
        // Death ends NPC services; stale talk panels must not own ghost input.
        localRealmDialogueNpc_=0;localRealmDialogueQuest_=0;
        localRealmVendorOpen_=localRealmTrainerOpen_=localRealmAuctionOpen_=false;
        localRealmNpcPanelOpen_=false;
    }
    // UI actions enqueue/apply commands without replacing the NPC snapshot.
    // Keep the existing per-frame copy: commands are allowed to mutate it.
    const auto npcs = localRealm_->npcs();
    const auto& content = localRealm_->content();
    const auto& io = ImGui::GetIO();
    const bool panelsOpen = localRealmMenuOpen_ || localRealmDialogueNpc_ ||
        localRealmInventoryOpen_ || localRealmJournalOpen_ || localRealmNpcPanelOpen_ ||
        localRealmAuctionOpen_ || localRealmVendorOpen_ || localRealmTrainerOpen_;
    if (!panelsOpen) localRealmPopupOpen_ = false;
    const bool originalPanelOpen = localFrameXml_.panelOpen();
    const bool keys = !io.WantTextInput && !panelsOpen && !originalPanelOpen;
    const auto pressed = [&](ImGuiKey key) { return keys && ImGui::IsKeyPressed(key, false); };
    const float scale = std::clamp(io.DisplaySize.y / 900.0f, 0.8f, 1.35f);
    const float margin = 18.0f * scale;
    auto beginPanel = [&](const char* title, ImVec2 position, ImVec2 size, bool* open = nullptr) {
        ImGui::SetNextWindowPos(position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88f);
        const bool visible = ImGui::Begin(title, open,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoFocusOnAppearing);
        if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
        ImGui::SetWindowFontScale(scale);
        return visible;
    };
    auto target = [&]() -> const game::LocalRealmNpc* {
        for (const auto& npc : npcs) if (npc.guid == localRealmTarget_) return &npc;
        return nullptr;
    };
    auto select = [&](uint64_t guid) {
        localRealmTarget_ = guid;
        if (gameHandler) gameHandler->setTargetGuidRaw(guid);
    };
    std::vector<const game::LocalRealmNpc*> nearby;
    for (const auto& npc : npcs)
        if (npc.mapId == self.mapId && npc.instanceId == self.instanceId && distanceTo(self, npc) < 100.0f) nearby.push_back(&npc);
    std::sort(nearby.begin(), nearby.end(), [&](const auto* a, const auto* b) {
        return distanceTo(self, *a) < distanceTo(self, *b);
    });
    bool toggleTarget=pressed(ImGuiKey_Tab);
#ifdef WOWEE_PS4
    const auto& targetPad=platform::ps4::padState();
    if(keys && localFrameXml_.ready() && targetPad.connected &&
       !platform::ps4::inputTextFocus() && !platform::ps4::keyboardCapturesInput() && (targetPad.pressed&ORBIS_PAD_BUTTON_TRIANGLE))toggleTarget=true;
#endif
    if(toggleTarget && !self.dead){
        const auto next=ui::toggledLocalTarget(localRealmTarget_,game::nearestLivingLocalTarget(self,npcs));
        if(!next){localRealm_->stopAttack();if(gameHandler)gameHandler->clearFocus();}
        select(next);
        LOG_INFO("[PAD_TARGET] ",next?"selected nearest target":"cleared target");
    }
    auto interactWith = [&](const game::LocalRealmNpc& npc) {
        if (self.dead) return;
        if (npc.dead) { localRealm_->loot(npc.guid); return; }
        if(npc.vehicleId) {
            // Prefer the driving seat, then a free passenger seat. The host
            // rechecks occupancy when the command arrives.
            const auto occupied=[&](uint8_t seat) {
                for(const auto& p:localRealm_->players())if(p.vehicleGuid==npc.guid && p.vehicleSeat==seat)return true;
                return false;
            };
            uint8_t seat=npc.vehicleControllerSeat;
            if(occupied(seat))for(seat=0;seat<npc.vehicleSeatCount && occupied(seat);++seat){}
            localRealm_->enterVehicle(npc.guid,seat);
            return;
        }
        // The realm validates range, map, life state and faction. Preserve its
        // diagnostic when the selected character cannot be spoken to.
        if (!localRealm_->interact(npc.guid) || !game::localNpcInTalkRange(self,npc)) return;
        if (localFrameXml_.open(npc.guid)) return;
        if(gameHandler) gameHandler->greetLocalRealmNpc(npc);
        localRealmDialogueNpc_ = npc.guid;
        localRealmDialogueQuest_ = 0;
        localRealmDialogueFocus_ = true;
        localRealmNpcPanelOpen_ = localRealmInventoryOpen_ = localRealmJournalOpen_ = false;
    };
    auto interact = [&] {
        if (self.dead) return;
        if(self.vehicleGuid){localRealm_->exitVehicle();return;}
        if(const auto* object=localRealm_->nearbyGameObject()){useLocalRealmObject(object->id);return;}
        const auto* npc = target();
        if (!npc || distanceTo(self, *npc) > 8.0f) {
            npc = nullptr;
            for (const auto* candidate : nearby) {
                if (distanceTo(self, *candidate) <= 8.0f &&
                    ((!candidate->hostile && !candidate->dead) || candidate->lootable)) {
                    npc = candidate; select(npc->guid); break;
                }
            }
        }
        if (!npc) { localRealmNpcPanelOpen_ = true; return; }
        interactWith(*npc);
    };
    const auto* actionTarget = target();
    const bool mailboxOwnsAction = !actionTarget || actionTarget->dead || !actionTarget->hostile;
    const auto* nearbyObject=localRealm_->nearbyGameObject();
    if(keys && !self.dead && !self.vehicleGuid && mailboxOwnsAction && nearbyObject) {
        const auto prompt="Square / 4: "+nearbyObject->name;
        ImGui::GetForegroundDrawList()->AddText(ImVec2(24.f,io.DisplaySize.y*.58f),IM_COL32(255,220,130,255),prompt.c_str());
    }
    if(keys && !self.dead && !self.vehicleGuid && !nearbyObject && mailboxOwnsAction && game::nearbyLocalMailbox(content,self))
        ImGui::GetForegroundDrawList()->AddText(ImVec2(24.f,io.DisplaySize.y*.58f),IM_COL32(255,220,130,255),"Square: Mailbox");
    const game::LocalRealmNpc* vehicle=nullptr;
    for(const auto& npc:npcs)if(npc.guid==self.vehicleGuid){vehicle=&npc;break;}
    const auto* vehicleKit=vehicle?content.vehicleKit(vehicle->vehicleId):nullptr;
    const auto publishedVehicleCasts=localRealm_->vehicleCasts();
    const game::LocalVehicleCast* vehicleCast=nullptr;
    for(const auto& cast:publishedVehicleCasts)if(cast.sourceGuid==self.vehicleGuid && cast.ownerGuid==self.guid){vehicleCast=&cast;break;}
    auto* vehicleTree=localFrameXml_.ready() && addonManager_ && addonManager_->getLuaEngine()?
        &addonManager_->getLuaEngine()->widgets():nullptr;
    const auto originalVehicleWidgetShown=[&](const ui::Widget* widget) {
        if(!vehicleTree || !widget || !widget->visible || widget->alpha<=.001f ||
           widget->rectW<=0 || widget->rectH<=0)return false;
        for(const auto* parent=widget;parent;parent=vehicleTree->get(parent->parent))
            if(!parent->shown)return false;
        return true;
    };
    const bool originalVehicleBar=vehicleKit && vehicleTree &&
        originalVehicleWidgetShown(vehicleTree->findByName("VehicleMenuBar")) &&
        vehicleTree->findByName("VehicleMenuBarActionButton1");
    const auto vehicleSlotAvailable=[&](size_t slot) {
        return vehicleKit && self.vehicleSeat<8 && slot<game::kLocalVehicleAbilities &&
            vehicleKit->abilities[slot].spellId && (vehicleKit->abilities[slot].seatMask&(1u<<self.vehicleSeat));
    };
    if(!vehicleSlotAvailable(localVehicleAbilitySlot_))for(size_t i=0;i<game::kLocalVehicleAbilities;++i)
        if(vehicleSlotAvailable(i)){localVehicleAbilitySlot_=uint8_t(i);break;}
    bool hasVehicleAim=false;
    for(size_t i=0;i<game::kLocalVehicleAbilities;++i)
        if(vehicleSlotAvailable(i) && vehicleKit->abilities[i].projectileSpeed>0)hasVehicleAim=true;
    const bool validVehicleAim=hasVehicleAim && !self.dead && vehicle && !vehicle->dead;
    const double aimNow=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const float confirmedYaw=validVehicleAim?vehicle->vehicleAim[self.vehicleSeat][0]:0;
    const float confirmedPitch=validVehicleAim?vehicle->vehicleAim[self.vehicleSeat][1]:0;
    localVehicleAim_.observe(self.guid,validVehicleAim?self.vehicleGuid:0,self.vehicleSeat,
        confirmedYaw,confirmedPitch,aimNow);
    bool vehicleInputClosed=false;
    const auto vehicleIdentityCurrent=[&]() {
        const auto* current=localRealm_->localPlayer();
        return !vehicleInputClosed && current && !current->dead && current->guid==self.guid &&
            current->vehicleGuid==self.vehicleGuid && current->vehicleSeat==self.vehicleSeat;
    };
    const auto editVehicleAim=[&](float yaw,float pitch) {
        if(validVehicleAim && vehicleIdentityCurrent())
            localVehicleAim_.edit(yaw,pitch,vehicleKit->minPitch,vehicleKit->maxPitch);
    };
    const auto leaveVehicle=[&]() {
        vehicleInputClosed=true;localVehicleAim_={};localRealm_->exitVehicle();
    };
    const auto nextVehicleSeat=[&]() {
        vehicleInputClosed=true;localVehicleAim_={};localRealm_->cycleVehicleSeat(1);
    };
    if(keys && validVehicleAim) {
        const float aimStep=std::clamp(io.DeltaTime,0.f,.1f);
        const int yawKeys=int(ImGui::IsKeyDown(ImGuiKey_Keypad4))-int(ImGui::IsKeyDown(ImGuiKey_Keypad6));
        const int pitchKeys=int(ImGui::IsKeyDown(ImGuiKey_Keypad8))-int(ImGui::IsKeyDown(ImGuiKey_Keypad2));
        if(yawKeys || pitchKeys)editVehicleAim(localVehicleAim_.yaw+yawKeys*aimStep,
            localVehicleAim_.pitch+pitchKeys*aimStep);
    }
    const auto useVehicleSlot=[&](uint8_t slot) {
        if(!vehicleSlotAvailable(slot) || !vehicleIdentityCurrent() || vehicleCast)return;
        localVehicleAbilitySlot_=slot;
        const auto& ability=vehicleKit->abilities[slot];
        if(ability.projectileSpeed>0 && !localVehicleAim_.settled())return;
        if(originalVehicleBar) {
            addonManager_->runInterfaceCommand("UseAction("+std::to_string(addons::kLocalVehicleFirstAction+slot)+")");
            return;
        }
        localRealm_->useVehicleAbility(slot,ability.projectileSpeed>0?0:
            ability.repair?self.vehicleGuid:localRealmTarget_);
    };
    auto primaryAction = [&](bool contextual) {
        // Corpse recovery owns the contextual action before mail, NPCs or
        // combat. The authority checks corpse identity, map/instance and range.
        if (self.ghost) { localRealm_->reclaimCorpse(); return; }
        if (self.dead) return;
        if(self.vehicleGuid){if(contextual)leaveVehicle();else useVehicleSlot(localVehicleAbilitySlot_);return;}
        if(contextual && mailboxOwnsAction && nearbyObject){useLocalRealmObject(nearbyObject->id);return;}
        if(contextual && mailboxOwnsAction && gameHandler)if(const auto* mailbox=game::nearbyLocalMailbox(content,self)) {
            gameHandler->openMailbox(mailbox->guid);return;
        }
        const auto* npc = target();
        // The snapshot's hostile flag is personalized by the authoritative
        // realm using LocalGameplay::canAttack, including neutral mobs. A
        // quest marker by itself never makes an attackable NPC friendly.
        if (contextual && npc && (npc->dead || !npc->hostile)) interactWith(*npc);
        else localRealm_->attack(localRealmTarget_);
    };
    const auto selectedSpell=[&](bool heal) -> const game::LocalSpellDefinition* {
        const game::LocalSpellDefinition* unavailable=nullptr;
        for(auto id:self.knownSpells) {
            const auto* spell=content.spell(id);if(!spell)continue;
            if(heal?(spell->heal>0||spell->periodicHeal>0):spell->damage>0||spell->periodicDamage>0) {
                if(spell->unsupportedReason.empty())return spell;
                if(!unavailable)unavailable=spell;
            } else if(!heal&&!unavailable&&!spell->unsupportedReason.empty())unavailable=spell;
        }
        return unavailable;
    };
    auto cast = [&](bool heal) {
        if(const auto* spell=selectedSpell(heal))localRealm_->castSpell(spell->id,heal?self.guid:localRealmTarget_);
    };
    const auto spellIcon=[&](const game::LocalSpellDefinition& spell,float pixels) {
        const auto icon=ui::cachedIconTexture(spell.iconId,assetManager.get(),window.get(),
            localRealmSpellIconPaths_,localRealmSpellIconCache_);
        if(icon){ImGui::Image((ImTextureID)(uintptr_t)icon,ImVec2(pixels*scale,pixels*scale));ImGui::SameLine();}
    };
#ifdef WOWEE_PS4
    const auto& actionPad=platform::ps4::padState();
    if(keys && self.vehicleGuid && actionPad.connected && !platform::ps4::keyboardCapturesInput() &&
       !platform::ps4::inputTextFocus()) {
        const bool yawModifier=(actionPad.buttons&ORBIS_PAD_BUTTON_L2)!=0;
        const bool pitchModifier=(actionPad.buttons&ORBIS_PAD_BUTTON_R2)!=0;
        const int heldDirection=((actionPad.buttons&ORBIS_PAD_BUTTON_R1)?1:0)-((actionPad.buttons&ORBIS_PAD_BUTTON_L1)?1:0);
        if(validVehicleAim && heldDirection && yawModifier!=pitchModifier) {
            const float step=heldDirection*std::clamp(io.DeltaTime,0.f,.1f);
            editVehicleAim(localVehicleAim_.yaw-(yawModifier?step:0),
                localVehicleAim_.pitch+(pitchModifier?step:0));
            ui::noteInterfaceConsumedKey(ImGuiKey_GamepadL1);ui::noteInterfaceConsumedKey(ImGuiKey_GamepadR1);
        }
        const int direction=(yawModifier || pitchModifier)?0:
            ((actionPad.pressed&ORBIS_PAD_BUTTON_R1)?1:0)-((actionPad.pressed&ORBIS_PAD_BUTTON_L1)?1:0);
        if(direction)for(int i=1;i<=int(game::kLocalVehicleAbilities);++i) {
            const auto slot=uint8_t((int(localVehicleAbilitySlot_)+direction*i+int(game::kLocalVehicleAbilities))%int(game::kLocalVehicleAbilities));
            if(vehicleSlotAvailable(slot)){localVehicleAbilitySlot_=slot;break;}
        }
        if(direction){ui::noteInterfaceConsumedKey(ImGuiKey_GamepadL1);ui::noteInterfaceConsumedKey(ImGuiKey_GamepadR1);}
        if((actionPad.pressed&ORBIS_PAD_BUTTON_SQUARE) &&
           !(actionPad.pressed&ORBIS_PAD_BUTTON_TRIANGLE) &&
           !ui::interfaceConsumedKey(ImGuiKey_GamepadFaceLeft) && (yawModifier || pitchModifier)) {
            if(yawModifier && !pitchModifier)nextVehicleSeat();
            if(pitchModifier && !yawModifier)useVehicleSlot(localVehicleAbilitySlot_);
            ui::noteInterfaceConsumedKey(ImGuiKey_GamepadFaceLeft);
        }
    }
    if(keys && (localFrameXml_.ready() || self.vehicleGuid) && !localFrameXml_.padBarFocused() && actionPad.connected &&
       !platform::ps4::keyboardCapturesInput() && (actionPad.pressed&ORBIS_PAD_BUTTON_SQUARE) &&
       !platform::ps4::inputTextFocus() &&
       (!self.vehicleGuid || !(actionPad.buttons&(ORBIS_PAD_BUTTON_L1|ORBIS_PAD_BUTTON_R1|ORBIS_PAD_BUTTON_L2|ORBIS_PAD_BUTTON_R2))) &&
       !(actionPad.pressed&ORBIS_PAD_BUTTON_TRIANGLE) && !ui::interfaceConsumedKey(ImGuiKey_GamepadFaceLeft))primaryAction(true);
#endif
    if (!self.vehicleGuid && pressed(ImGuiKey_1)
#ifdef WOWEE_PS4
        && !localFrameXml_.ready()
#endif
    ) {
        bool contextual = false;
#ifdef WOWEE_PS4
        const auto& pad = platform::ps4::padState();
        contextual = pad.connected && (pad.buttons & ORBIS_PAD_BUTTON_SQUARE) &&
            !(pad.buttons & (ORBIS_PAD_BUTTON_L1 | ORBIS_PAD_BUTTON_R1 |
                             ORBIS_PAD_BUTTON_L2 | ORBIS_PAD_BUTTON_R2));
#endif
        // R2 + Square remains the explicit attack binding. Only unmodified
        // Square selects the talk/attack action for the current target.
        primaryAction(contextual);
    }
    if (!self.vehicleGuid && pressed(ImGuiKey_2)) cast(false);
    if (!self.vehicleGuid && pressed(ImGuiKey_3)) cast(true);
    if (pressed(ImGuiKey_4)) { if(self.vehicleGuid)leaveVehicle();else interact(); }
    if(self.vehicleGuid) {
        if(keys && !vehicleKit)ImGui::GetForegroundDrawList()->AddText(ImVec2(24.f,io.DisplaySize.y*.58f),
            IM_COL32(255,220,130,255),"Square / 4: Exit   L2+Square / V: Next seat");
        if(pressed(ImGuiKey_V))nextVehicleSeat();
        for(size_t i=0;i<game::kLocalVehicleAbilities;++i)if(pressed(ImGuiKey(int(ImGuiKey_F1)+i)))useVehicleSlot(uint8_t(i));
        if(originalVehicleBar && !panelsOpen && !originalPanelOpen) {
            if(const auto* button=vehicleTree->findByName("VehicleMenuBarActionButton"+std::to_string(localVehicleAbilitySlot_+1));
               originalVehicleWidgetShown(button)) {
                const float factor=vehicleTree->uiScale();
                ImGui::GetForegroundDrawList()->AddRect(
                    ImVec2(button->left*factor,io.DisplaySize.y-(button->bottom+button->rectH)*factor),
                    ImVec2((button->left+button->rectW)*factor,io.DisplaySize.y-button->bottom*factor),
                    IM_COL32(255,220,100,230),3.f,0,2.f);
            }
            ImGui::GetForegroundDrawList()->AddText(ImVec2(24.f,io.DisplaySize.y*.58f),IM_COL32(255,220,130,255),
                "L1/R1: select   R2+Square / F1-F6: use\nL2+Square / V: next seat   Square / 4: exit");
            if(vehicleCast) {
                const auto* spell=content.spell(vehicleCast->spellId);
                const auto tenths=(vehicleCast->remainingMs+99)/100;
                const auto text=std::string("Casting ")+(spell?spell->name:"vehicle ability")+"  "+
                    std::to_string(tenths/10)+"."+std::to_string(tenths%10)+"s";
                ImGui::GetForegroundDrawList()->AddText(ImVec2(24.f,io.DisplaySize.y*.58f+42.f),IM_COL32(255,220,130,255),text.c_str());
            }
            if(validVehicleAim)ImGui::GetForegroundDrawList()->AddText(ImVec2(24.f,io.DisplaySize.y*.58f+(vehicleCast?62.f:42.f)),IM_COL32(210,210,210,255),
                localVehicleAim_.settled()?"Numpad 4/6: yaw   8/2: elevation\nL2+L1/R1: yaw   R2+L1/R1: elevation":"Updating aim...");
        }
        if(vehicleKit && !originalVehicleBar && !panelsOpen && !originalPanelOpen) {
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*.5f,io.DisplaySize.y-165.f*scale),ImGuiCond_Always,ImVec2(.5f,1.f));
            ImGui::SetNextWindowBgAlpha(.9f);
            if(ImGui::Begin("Vehicle abilities",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav)) {
                ImGui::Text("%s   Health %u/%u   Energy %u/%u",vehicle->name.c_str(),vehicle->health,vehicle->maxHealth,vehicle->vehiclePower,vehicleKit->maxPower);
                if(vehicleCast) {
                    const auto* spell=content.spell(vehicleCast->spellId);
                    ImGui::Text("Casting %s",spell?spell->name.c_str():"vehicle ability");
                    const float progress=vehicleCast->totalMs?1.f-float(vehicleCast->remainingMs)/vehicleCast->totalMs:0;
                    ImGui::ProgressBar(std::clamp(progress,0.f,1.f),ImVec2(300.f*scale,0));
                }
                for(size_t i=0;i<game::kLocalVehicleAbilities;++i)if(vehicleSlotAvailable(i)) {
                    const auto& a=vehicleKit->abilities[i];const auto* spell=content.spell(a.spellId);
                    const auto cooldown=std::max(vehicle->vehicleCooldownMs[i],vehicle->vehicleGlobalCooldownMs);
                    ImGui::PushID(int(i));
                    ImGui::BeginDisabled(vehicleCast || cooldown ||
                        (a.powerType==game::LocalVehiclePowerType::Energy && vehicle->vehiclePower<a.powerCost) || self.dead ||
                        (a.projectileSpeed>0 && !localVehicleAim_.settled()));
                    const auto label=std::string(i==localVehicleAbilitySlot_?"> ":"  ")+"F"+std::to_string(i+1)+": "+(spell?spell->name:"Ability");
                    if(ImGui::Button(label.c_str())){localVehicleAbilitySlot_=uint8_t(i);useVehicleSlot(uint8_t(i));}
                    ImGui::EndDisabled();ImGui::SameLine();
                    if(a.powerType==game::LocalVehiclePowerType::None)ImGui::Text("No cost   %.1fs",cooldown/1000.f);
                    else ImGui::Text("%u energy   %.1fs",a.powerCost,cooldown/1000.f);
                    if(ImGui::IsItemHovered())ImGui::SetTooltip("%.1fs cast   %.1f yd area   school %u",a.castTimeMs/1000.f,a.areaRadius,unsigned(a.schoolMask));
                    ImGui::PopID();
                }
                if(validVehicleAim) {
                    ImGui::Separator();
                    constexpr float degrees=57.29577951308232f;
                    float yaw=localVehicleAim_.yaw*degrees,pitch=localVehicleAim_.pitch*degrees;
                    ImGui::SetNextItemWidth(260.f*scale);
                    if(ImGui::SliderFloat("Yaw",&yaw,-180.f,180.f,"%.1f deg"))
                        editVehicleAim(yaw/degrees,localVehicleAim_.pitch);
                    ImGui::SetNextItemWidth(260.f*scale);
                    if(ImGui::SliderFloat("Elevation",&pitch,vehicleKit->minPitch*degrees,vehicleKit->maxPitch*degrees,"%.1f deg"))
                        editVehicleAim(localVehicleAim_.yaw,pitch/degrees);
                    ImGui::TextDisabled("Numpad 4/6: yaw   8/2: elevation");
                    ImGui::TextDisabled("L2+L1/R1: yaw   R2+L1/R1: elevation");
                    if(!localVehicleAim_.settled())ImGui::TextUnformatted("Updating aim...");
                }
                ImGui::TextDisabled("L1/R1: select   R2+Square / F1-F6: use");
                ImGui::TextDisabled("L2+Square / V: next seat   Square / 4: exit");
                if(ImGui::Button("Next seat"))nextVehicleSeat();
                ImGui::SameLine();if(ImGui::Button("Exit vehicle"))leaveVehicle();
                if(vehicleCast){ImGui::SameLine();if(ImGui::Button("Cancel cast"))localRealm_->cancelCast();}
            }
            ImGui::End();
        }
    }
    if(!gameHandler && keys && validVehicleAim && vehicleIdentityCurrent() && localVehicleAim_.ready(aimNow))
        localVehicleAim_.submitted(aimNow,localRealm_->aimVehicle(localVehicleAim_.yaw,localVehicleAim_.pitch));
    if (!self.vehicleGuid && pressed(ImGuiKey_5)) {
        if(localFrameXml_.ready()) addonManager_->runInterfaceCommand("ToggleBackpack()");
        else localRealmInventoryOpen_ = !localRealmInventoryOpen_;
    }
    if (!self.vehicleGuid && pressed(ImGuiKey_6)) {
        if(localFrameXml_.ready()) addonManager_->runInterfaceCommand("ToggleFrame(QuestLogFrame)");
        else localRealmJournalOpen_ = !localRealmJournalOpen_;
    }
    if (!self.vehicleGuid && pressed(ImGuiKey_7)) {
        for (const auto& stack : self.inventory) {
            const auto* item = content.item(stack.itemId);
            if (item && item->heal && stack.count) { localRealm_->useItem(item->id); break; }
        }
    }
    if (!self.vehicleGuid && pressed(ImGuiKey_8) && !self.dead) localRealm_->stopAttack();
    if (!self.vehicleGuid && pressed(ImGuiKey_9)) localRealmNpcPanelOpen_ = !localRealmNpcPanelOpen_;
    // A nested popup owns its Back press, including the frame where ImGui
    // has already dismissed a combo during NewFrame. Original FrameXML may
    // also have closed its panel earlier in this frame. Neither press may
    // fall through and close/open the native game menu as well.
    if (!io.WantTextInput && !localRealmPopupOpen_ && !originalPanelOpen &&
            !ui::interfaceConsumedKey(ImGuiKey_Escape) &&
            !ui::interfaceConsumedKey(ImGuiKey_GamepadFaceRight) &&
            (ImGui::IsKeyPressed(ImGuiKey_Escape,false) ||
            (panelsOpen && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight,false)))) {
        if (localRealmDialogueNpc_ && localRealmDialogueQuest_) {
            localRealmDialogueQuest_ = 0;
            localRealmDialogueFocus_ = true;
        }
        else if (localRealmDialogueNpc_) localRealmDialogueNpc_ = 0;
        // A service window sits on top of the conversation that opened it, so
        // Back closes it first and leaves the player still talking - which is
        // where they were when they opened it.
        else if (localRealmAuctionOpen_ || localRealmVendorOpen_ || localRealmTrainerOpen_) {
            localRealmAuctionOpen_ = localRealmVendorOpen_ = localRealmTrainerOpen_ = false;
        }
        else if (localRealmInventoryOpen_ || localRealmJournalOpen_ || localRealmNpcPanelOpen_) {
            localRealmInventoryOpen_ = localRealmJournalOpen_ = localRealmNpcPanelOpen_ = false;
        } else if (localFrameXml_.ready()) localFrameXml_.toggleGameMenu();
        else localRealmMenuOpen_ = !localRealmMenuOpen_;
    }

    // Native aiming aids use the same server->render conversion and Vulkan
    // projection as nameplates. These bounded overlays are not spell models.
    if(auto* camera=renderer->getCamera();camera && !panelsOpen && !originalPanelOpen) {
        auto* draw=ImGui::GetBackgroundDrawList();
        const auto project=[&](const glm::vec3& world,ImVec2& screen) {
            const auto render=coords::canonicalToRender(coords::serverToCanonical(world));
            const glm::vec4 clip=camera->getViewProjectionMatrix()*glm::vec4(render,1);
            if(!std::isfinite(clip.w) || clip.w<=.01f)return false;
            const glm::vec3 ndc=glm::vec3(clip)/clip.w;
            if(!std::isfinite(ndc.x) || !std::isfinite(ndc.y) || !std::isfinite(ndc.z) ||
               std::abs(ndc.x)>1 || std::abs(ndc.y)>1 || ndc.z<0 || ndc.z>1)return false;
            screen=ImVec2((ndc.x*.5f+.5f)*io.DisplaySize.x,(ndc.y*.5f+.5f)*io.DisplaySize.y);
            return true;
        };
        if(validVehicleAim && vehicleIdentityCurrent() && vehicleSlotAvailable(localVehicleAbilitySlot_)) {
            const auto& ability=vehicleKit->abilities[localVehicleAbilitySlot_];
            if(ability.projectileSpeed>0) {
                const auto seat=game::localVehicleSeatPosition(*vehicle,self.vehicleSeat);
                const glm::vec3 origin(seat[0],seat[1],seat[2]+vehicleKit->muzzleHeight);
                const float heading=vehicle->orientation+confirmedYaw;
                const float horizontal=ability.projectileSpeed*std::cos(confirmedPitch);
                const glm::vec3 velocity(horizontal*std::cos(heading),horizontal*std::sin(heading),
                    ability.projectileSpeed*std::sin(confirmedPitch));
                const auto position=[&](float time) {
                    return origin+velocity*time+glm::vec3(0,0,-.5f*ability.projectileGravity*time*time);
                };
                // Integrate speed analytically to trim the guide at the
                // authored traveled-distance limit, including vertical shots.
                const auto traveled=[&](double time) {
                    const double gravity=ability.projectileGravity;
                    if(gravity<=.000001)return double(ability.projectileSpeed)*time;
                    const double h=std::abs(double(horizontal)),z=velocity.z;
                    const auto primitive=[&](double v) {
                        if(h<.000001)return .5*v*std::abs(v);
                        return .5*(v*std::hypot(h,v)+h*h*std::asinh(v/h));
                    };
                    return (primitive(z)-primitive(z-gravity*time))/gravity;
                };
                double endTime=ability.projectileLifetimeMs*.001;
                if(traveled(endTime)>ability.range) {
                    double lo=0,hi=endTime;
                    for(unsigned i=0;i<24;++i) {
                        const double middle=(lo+hi)*.5;
                        if(traveled(middle)>ability.range)hi=middle;else lo=middle;
                    }
                    endTime=lo;
                }
                ImVec2 previous;bool previousVisible=project(origin,previous);
                for(unsigned i=1;i<=32;++i) {
                    ImVec2 point;const bool visible=project(position(float(endTime*i/32)),point);
                    if(visible && previousVisible)draw->AddLine(previous,point,IM_COL32(255,214,105,180),1.5f*scale);
                    if(visible && i==32)draw->AddCircle(point,5.f*scale,IM_COL32(255,214,105,220),12,1.5f*scale);
                    previous=point;previousVisible=visible;
                }
            }
        }
        size_t markerCount=0;
        for(const auto& shot:localRealm_->vehicleProjectiles()) {
            if(markerCount++>=game::kLocalMaxVehicleProjectiles)break;
            if(shot.mapId!=self.mapId || shot.instanceId!=self.instanceId)continue;
            const float dx=shot.x-self.x,dy=shot.y-self.y,dz=shot.z-self.z;
            if(dx*dx+dy*dy+dz*dz>500.f*500.f)continue;
            ImVec2 point;if(!project(glm::vec3(shot.x,shot.y,shot.z),point))continue;
            const auto tint=shot.ownerGuid==self.guid?IM_COL32(255,227,126,240):IM_COL32(255,155,84,240);
            draw->AddCircleFilled(point,3.f*scale,tint,8);
            ImVec2 tail;
            if(project(glm::vec3(shot.x-shot.vx*.04f,shot.y-shot.vy*.04f,shot.z-shot.vz*.04f),tail))
                draw->AddLine(tail,point,tint,2.f*scale);
        }
    }

    // World nameplates use the same Vulkan projection convention as GameScreen:
    // the projection already flips Y, so the viewport conversion must not.
    if (auto* camera = renderer->getCamera()) {
        auto* draw = ImGui::GetBackgroundDrawList();
        unsigned plateCount = 0;
        for (const auto* npc : nearby) {
            const bool isTarget = npc->guid == localRealmTarget_;
            if (!ui::localNpcPlateVisible(distanceTo(self,*npc), isTarget, npc->dead, npc->lootable) ||
                (!isTarget && plateCount >= ui::LocalNpcPlateLimit)) continue;
            const auto canonical = coords::serverToCanonical(glm::vec3(npc->x, npc->y, npc->z + 2.3f));
            const glm::vec4 clip = camera->getViewProjectionMatrix() * glm::vec4(coords::canonicalToRender(canonical), 1);
            if (clip.w <= 0.01f) continue;
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (std::abs(ndc.x) > 1 || std::abs(ndc.y) > 1 || ndc.z < 0 || ndc.z > 1) continue;
            ++plateCount;
            const float x = (ndc.x*.5f+.5f)*io.DisplaySize.x;
            const float y = (ndc.y*.5f+.5f)*io.DisplaySize.y;
            const bool selected = npc->guid == localRealmTarget_;
            // Quest availability is shown by the authoritative overhead marker;
            // being a quest giver alone does not mean a quest is available.
            std::string label = npc->name;
            if (npc->dead) label += npc->lootable ? " [Loot]" : " [Dead]";
            const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
            const float half = std::max(65.0f, textSize.x*.5f+9);
            const ImVec2 a(x-half, y-22), b(x+half, y+10);
            draw->AddRectFilled(a, b, IM_COL32(9, 13, 20, 200), 4);
            if (selected) draw->AddRect(a, b, IM_COL32(240, 202, 85, 255), 4, 0, 2);
            draw->AddText(ImVec2(x-textSize.x*.5f,y-19),
                npc->hostile ? IM_COL32(255,164,138,255) : IM_COL32(238,224,153,255), label.c_str());
            const float hp = npc->maxHealth ? static_cast<float>(npc->health)/npc->maxHealth : 0;
            draw->AddRectFilled(ImVec2(a.x+3,y+3), ImVec2(a.x+3+(b.x-a.x-6)*hp,y+7),
                npc->hostile ? IM_COL32(174,51,41,255) : IM_COL32(56,164,94,255));
            if (!panelsOpen && !originalPanelOpen && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) &&
                io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y) {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) select(npc->guid);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { select(npc->guid); interact(); }
            }
        }
    }

    // Friendly LAN players have names, not NPC health boxes. Server-supplied
    // character names stay associated with their GUID and map/instance.
    if (auto* camera = renderer->getCamera()) {
        auto* draw = ImGui::GetBackgroundDrawList();
        for (const auto& other : localRealm_->players()) {
            if (other.guid == self.guid || other.mapId != self.mapId || other.instanceId != self.instanceId) continue;
            const float dx=other.x-self.x, dy=other.y-self.y, dz=other.z-self.z;
            if (dx*dx+dy*dy+dz*dz > ui::LocalPlayerNameDistance*ui::LocalPlayerNameDistance) continue;
            const auto point = coords::canonicalToRender(coords::serverToCanonical(glm::vec3(other.x,other.y,other.z+2.4f)));
            const glm::vec4 clip = camera->getViewProjectionMatrix()*glm::vec4(point,1);
            if (clip.w<=.01f) continue;
            const glm::vec3 ndc=glm::vec3(clip)/clip.w;
            if (std::abs(ndc.x)>1 || std::abs(ndc.y)>1 || ndc.z<0 || ndc.z>1) continue;
            const ImVec2 size=ImGui::CalcTextSize(other.name.c_str());
            const ImVec2 at((ndc.x*.5f+.5f)*io.DisplaySize.x-size.x*.5f, (ndc.y*.5f+.5f)*io.DisplaySize.y-size.y);
            draw->AddText(ImVec2(at.x+1,at.y+1),IM_COL32(0,0,0,220),other.name.c_str());
            draw->AddText(at,IM_COL32(128,128,255,255),other.name.c_str());
        }
    }

    // Original WotLK art is read from the user's MPQs. Missing textures
    // have a legible fallback; no assets are bundled with the package.
    auto art = [&](const char* path) -> ImTextureID {
        auto found = localRealmUiArt_.find(path);
        if (found == localRealmUiArt_.end()) {
            if (!ui::claimUiTextureUpload()) return 0;
            std::string assetPath(path);
            if (assetPath.size()<4 || assetPath.substr(assetPath.size()-4)!=".blp") assetPath += ".blp";
            const auto texture = ui::uploadUiTextureFromBlp(assetManager.get(), assetPath, window.get());
            if (!texture) LOG_WARNING("Local HUD art missing or unavailable: ", path);
            found = localRealmUiArt_.emplace(path, texture).first;
        }
        return (ImTextureID)(uintptr_t)found->second;
    };
    auto unitFrame = [&](const char* id, ImVec2 pos, const char* name,
                         uint32_t level, uint32_t hp, uint32_t maxHp,
                         uint32_t power, uint32_t maxPower, bool hostile, bool player) {
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(ImVec2(300*scale,140*scale));
        ImGui::Begin(id,nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground|
            ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoNav);
        auto* d=ImGui::GetWindowDrawList();
        auto point=[&](float x,float y){return ImVec2(pos.x+x*scale,pos.y+y*scale);};
        d->AddRectFilled(point(62,22),point(260,69),IM_COL32(12,12,15,235),4);
        const float h=maxHp?std::clamp(float(hp)/maxHp,0.f,1.f):0;
        const float m=maxPower?std::clamp(float(power)/maxPower,0.f,1.f):0;
        d->AddRectFilled(point(75,34),point(75+178*h,50),hostile?IM_COL32(170,35,28,255):IM_COL32(25,145,38,255));
        const ImU32 powerColor=player&&self.resourceType==game::LocalResourceType::Rage?IM_COL32(170,35,28,255):
            player&&self.resourceType==game::LocalResourceType::Energy?IM_COL32(205,180,35,255):IM_COL32(35,80,180,255);
        d->AddRectFilled(point(75,52),point(75+178*m,64),powerColor);
        const auto frame=art(player?"Interface/TargetingFrame/UI-Player-Frame.blp":"Interface/TargetingFrame/UI-TargetingFrame.blp");
        if(frame)d->AddImage(frame,point(0,0),point(280,140));
        else d->AddRect(point(4,8),point(268,78),IM_COL32(180,157,102,255),8,0,2);
        d->AddText(point(81,15),IM_COL32(255,214,110,255),name);
        char health[64];std::snprintf(health,sizeof(health),"%u / %u",hp,maxHp);
        d->AddText(point(121,34),IM_COL32_WHITE,health);
        if(maxPower){std::snprintf(health,sizeof(health),"%u / %u",power,maxPower);d->AddText(point(121,52),IM_COL32_WHITE,health);}
        std::snprintf(health,sizeof(health),"%u",level);d->AddText(point(23,73),IM_COL32(255,214,110,255),health);
        ImGui::End();
    };
    if (!localFrameXml_.ready()) {
    unitFrame("##localPlayerFrame",ImVec2(margin,margin),self.name.c_str(),self.level,
              self.health,self.maxHealth,self.mana,self.maxMana,false,true);
    if(const auto* npc=target()) {
        unitFrame("##localTargetFrame",ImVec2(margin+310*scale,margin),npc->name.c_str(),npc->level,
                  npc->health,npc->maxHealth,0,0,npc->hostile,false);
    }

    if(self.castingSpellId||self.castStatus==game::LocalCastStatus::Interrupted||self.castStatus==game::LocalCastStatus::Failed) {
        const float width=420*scale;
        beginPanel("Casting",ImVec2((io.DisplaySize.x-width)*.5f,io.DisplaySize.y-210*scale-margin),ImVec2(width,78*scale));
        if(self.castingSpellId) {
            const auto* spell=content.spell(self.castingSpellId);
            if(spell)spellIcon(*spell,24);
            ImGui::Text("%s  %.1fs",spell?spell->name.c_str():"Casting",self.castRemainingMs/1000.f);
            ImGui::ProgressBar(self.castTotalMs?1.f-float(self.castRemainingMs)/self.castTotalMs:0,ImVec2(-80*scale,16*scale));
            ImGui::SameLine();if(ImGui::SmallButton("Cancel"))localRealm_->cancelCast();
        } else ImGui::TextUnformatted(self.castStatus==game::LocalCastStatus::Interrupted?"Cast interrupted":"Cast failed: check target, range and resource");
        ImGui::End();
    }
    const float buttonSize=44*scale;
    const float barW=std::min(730.f*scale,io.DisplaySize.x-margin*2);
    const ImVec2 barPos((io.DisplaySize.x-barW)*.5f,io.DisplaySize.y-108*scale);
    ImGui::SetNextWindowPos(barPos);
    ImGui::SetNextWindowSize(ImVec2(barW,100*scale));
    ImGui::Begin("##localActionBar",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|
        ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoBackground);
    auto* barDraw=ImGui::GetWindowDrawList();
    barDraw->AddRectFilled(barPos,ImVec2(barPos.x+barW,barPos.y+62*scale),IM_COL32(24,22,20,240),4);
    const auto endCap=art("Interface/MainMenuBar/UI-MainMenuBar-EndCap-Human.blp");
    barDraw->PushClipRect(ImVec2(0,0),io.DisplaySize,false);
    if(endCap){
        barDraw->AddImage(endCap,ImVec2(barPos.x-80*scale,barPos.y-30*scale),ImVec2(barPos.x+48*scale,barPos.y+98*scale));
        barDraw->AddImage(endCap,ImVec2(barPos.x+barW-48*scale,barPos.y-30*scale),ImVec2(barPos.x+barW+80*scale,barPos.y+98*scale),ImVec2(1,0),ImVec2(0,1));
    }
    barDraw->PopClipRect();
    const auto quickslot=art("Interface/Buttons/UI-Quickslot2.blp");
    const auto actionButton = [&](const char* id,const char* iconPath,const char* label,const char* tip,
                                  bool disabled,auto&& action,uint32_t cooldownMs=0) {
        ImGui::PushID(id);
        disabled = disabled || panelsOpen || originalPanelOpen;
        ImGui::BeginDisabled(disabled);
        const ImVec2 p=ImGui::GetCursorScreenPos();
        const bool clicked=ImGui::Button("##action",ImVec2(buttonSize,buttonSize));
        const auto icon=art(iconPath);
        if(icon)barDraw->AddImage(icon,ImVec2(p.x+5*scale,p.y+5*scale),ImVec2(p.x+buttonSize-5*scale,p.y+buttonSize-5*scale));
        if(disabled)barDraw->AddRectFilled(p,ImVec2(p.x+buttonSize,p.y+buttonSize),IM_COL32(0,0,0,150));
        if(quickslot)barDraw->AddImage(quickslot,ImVec2(p.x-5*scale,p.y-5*scale),ImVec2(p.x+buttonSize+5*scale,p.y+buttonSize+5*scale));
        if(cooldownMs) {
            char countdown[16];ui::localActionCooldownText(cooldownMs,countdown);
            const ImVec2 textSize=ImGui::CalcTextSize(countdown);
            const ImVec2 centre(p.x+(buttonSize-textSize.x)*.5f,p.y+(buttonSize-textSize.y)*.5f-3*scale);
            barDraw->AddText(ImVec2(centre.x+1,centre.y+1),IM_COL32(0,0,0,255),countdown);
            barDraw->AddText(centre,IM_COL32(255,244,195,255),countdown);
        }
        barDraw->AddText(ImVec2(p.x+2,p.y+buttonSize-14*scale),IM_COL32(255,230,145,255),label);
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)){ImGui::BeginTooltip();ImGui::TextUnformatted(tip);ImGui::EndTooltip();}
        if(clicked&&!disabled)action();
        ImGui::PopID();ImGui::SameLine();
    };
    actionButton("attack","Interface/Icons/Ability_MeleeDamage.blp","S",self.ghost?"Reclaim corpse nearby — Square / 1":"Talk / Attack / Loot — Square | Attack — R2 + Square",self.dead&&!self.ghost,[&]{primaryAction(true);});
    for(bool healing:{false,true}) {
        const auto* spell=selectedSpell(healing);
        const auto path=spell?localRealmSpellIconPaths_.find(spell->iconId):localRealmSpellIconPaths_.end();
        const char* icon=path!=localRealmSpellIconPaths_.end()?path->second.c_str():"Interface/Icons/INV_Misc_QuestionMark.blp";
        const uint32_t cooldownMs=spell?ui::localActionCooldownMs(self,content,spell->id):0;
        const bool disabled=self.dead||!spell||!spell->unsupportedReason.empty()||self.castingSpellId||cooldownMs;
        actionButton(healing?"heal":"spell",icon,healing?"R2 O":"R2 T",spell?spell->name.c_str():"No learned spell",disabled,[&]{cast(healing);},cooldownMs);
    }
    actionButton("interact","Interface/Icons/INV_Misc_Note_01.blp","R2 X","Talk / Loot — R2 + Cross",false,interact);
    actionButton("bags","Interface/Buttons/Button-Backpack-Up.blp","L2 S","Inventory / Equipment — L2 + Square",false,[&]{localRealmInventoryOpen_=!localRealmInventoryOpen_;});
    actionButton("quests","Interface/Icons/INV_Misc_Book_09.blp","L2 T","Quest log — L2 + Triangle",false,[&]{localRealmJournalOpen_=!localRealmJournalOpen_;});
    actionButton("potion","Interface/Icons/INV_Potion_54.blp","L2 O","Use healing item — L2 + Circle",self.dead,[&]{for(const auto& stack:self.inventory){const auto* item=content.item(stack.itemId);if(item&&item->heal&&stack.count){localRealm_->useItem(item->id);break;}}});
    actionButton("stop","Interface/Icons/Ability_Vanish.blp","L2 X","Stop attack — L2 + Cross",self.dead,[&]{localRealm_->stopAttack();});
    actionButton("nearby","Interface/Icons/INV_Misc_GroupLooking.blp","R1 S","Nearby NPCs / Portals — R1 + Square",false,[&]{localRealmNpcPanelOpen_=!localRealmNpcPanelOpen_;});
    actionButton("menu","Interface/Icons/INV_Misc_Gear_01.blp","OPT","Game menu — Options",false,[&]{localRealmMenuOpen_=!localRealmMenuOpen_;});
    ImGui::NewLine();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,ImVec4(.45f,.16f,.68f,1));
    ImGui::ProgressBar(self.xpToLevel?std::clamp(float(self.xp)/self.xpToLevel,0.f,1.f):0.f,ImVec2(barW-16*scale,8*scale),"");
    ImGui::PopStyleColor();
    ImGui::TextUnformatted("R3: cursor / camera | Cross: jump / cursor click | Triangle: target / clear target | Options: close");
    ImGui::End();
    }
    localRealmNotice_.observe(localRealm_->actionStatusRevision(),
        localRealm_->error().empty()?localRealm_->actionStatus():localRealm_->error(),ImGui::GetTime());
    const float noticeAlpha=localRealmNotice_.alpha(ImGui::GetTime());
    if(noticeAlpha>0) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*.5f,io.DisplaySize.y*.18f),ImGuiCond_Always,ImVec2(.5f,0));
        ImGui::Begin("##localActionStatus",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|
            ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoInputs);
        ImGui::TextColored(ImVec4(1,.3f,.2f,noticeAlpha),"%s",localRealmNotice_.text.c_str());
        ImGui::End();
    }
    auto scriptDialogues=localRealm_->scriptDialogues();
    // Original creature speech (creature_text) goes to the chat log as the
    // monster say/yell/emote/whisper it is; authored script lines keep the
    // centred notice. Revisions are monotonic per realm session.
    for(const auto& dialogue:scriptDialogues)if(dialogue.chatType && dialogue.revision>localCreatureChatRevision_) {
        localCreatureChatRevision_=dialogue.revision;
        if(!gameHandler)continue;
        game::MessageChatData chat;
        chat.type=static_cast<game::ChatType>(dialogue.chatType);chat.language=game::ChatLanguage::UNIVERSAL;
        chat.senderGuid=dialogue.speakerGuid;chat.receiverGuid=self.guid;chat.receiverName=self.name;chat.message=dialogue.text;
        for(const auto& npc:npcs)if(npc.guid==dialogue.speakerGuid){chat.senderName=npc.name;break;}
        gameHandler->addLocalChatMessage(chat);
    }
    std::erase_if(scriptDialogues,[](const auto& dialogue){return dialogue.chatType!=0;});
    if(!scriptDialogues.empty()) {
        const auto latest=std::max_element(scriptDialogues.begin(),scriptDialogues.end(),
            [](const auto& a,const auto& b){return a.revision<b.revision;});
        if(latest->revision!=localScriptDialogueNotice_.revision) {
            std::string speaker;
            if(latest->speakerGuid==self.guid)speaker=self.name;
            if(speaker.empty())for(const auto& npc:npcs)if(npc.guid==latest->speakerGuid){speaker=npc.name;break;}
            if(speaker.empty())for(const auto& player:localRealm_->players())if(player.guid==latest->speakerGuid){speaker=player.name;break;}
            localScriptDialogueNotice_.observe(latest->revision,
                speaker.empty()?latest->text:speaker+": "+latest->text,ImGui::GetTime());
        }
    }
    const float dialogueAlpha=localScriptDialogueNotice_.alpha(ImGui::GetTime());
    if(dialogueAlpha>0) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*.5f,io.DisplaySize.y*.24f),ImGuiCond_Always,ImVec2(.5f,0));
        ImGui::SetNextWindowSizeConstraints(ImVec2(0,0),ImVec2(io.DisplaySize.x*.72f,io.DisplaySize.y*.25f));
        ImGui::Begin("##localScriptDialogue",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|
            ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoInputs);
        ImGui::PushTextWrapPos(io.DisplaySize.x*.68f);
        ImGui::TextColored(ImVec4(1.f,.86f,.48f,dialogueAlpha),"%s",localScriptDialogueNotice_.text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::End();
    }

    if (localRealmDialogueNpc_) {
        const auto found = std::find_if(npcs.begin(), npcs.end(), [&](const auto& n) {
            return n.guid == localRealmDialogueNpc_;
        });
        if (found == npcs.end() || !game::localNpcInTalkRange(self,*found)) {
            localRealmDialogueNpc_ = 0;
            localRealmDialogueQuest_ = 0;
        } else {
            const auto& npc = *found;
            // GossipFrame's authored 384x512 layout: retain the aspect and the
            // four individual texture tiles. Scaling a whole texture to an
            // arbitrary panel rectangle distorts the frame and clips its sides.
            const float s = std::min(scale, std::max(.4f,(io.DisplaySize.y-115.f)/512.f));
            const ImVec2 origin(margin, std::min(85.f*s,io.DisplaySize.y-512.f*s-margin));
            ImGui::SetNextWindowPos(origin);
            ImGui::SetNextWindowSize(ImVec2(384*s,512*s));
            if(localRealmDialogueFocus_) ImGui::SetNextWindowFocus();
            ImGui::Begin("##LocalOriginalDialogue",nullptr,ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
            // Programmatic focus alone does not enable ImGui's gamepad
            // activation cursor. Cross must work before the first D-pad move.
            if ((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) &&
                (io.BackendFlags & ImGuiBackendFlags_HasGamepad)) ImGui::SetNavCursorVisible(true);
            ImGui::SetWindowFontScale(s);
            auto* d=ImGui::GetWindowDrawList();
            auto point=[&](float x,float y){return ImVec2(origin.x+x*s,origin.y+y*s);};
            d->AddRectFilled(point(21,75),point(324,432),IM_COL32(220,194,142,255));
            const char* pieces[] = {"TopLeft","TopRight","BotLeft","BotRight"};
            for(int i=0;i<4;++i) {
                const std::string path=std::string("Interface/QuestFrame/UI-QuestGreeting-")+pieces[i]+".blp";
                const auto texture=art(path.c_str());
                const float x=(i%2)*256.f,y=(i/2)*256.f,w=i%2?128.f:256.f;
                if(texture)d->AddImage(texture,point(x,y),point(x+w,y+256));
            }
            ImGui::SetCursorPos(ImVec2(77*s,23*s));
            ImGui::PushTextWrapPos(315*s);
            ImGui::TextColored(ImVec4(1,.84f,.38f,1),"%s",npc.name.c_str());
            ImGui::PopTextWrapPos();
            const auto quests=localRealm_->questsForNpc(npc.entry);
            // 2.40: the creature's gossip page as the authority holds it.
            const auto& gossip=self.gossip;
            const bool gossipHere=gossip.open()&&gossip.npcGuid==npc.guid;
            if(gossipHere&&gossip.revision!=localRealmGossipRevision_) {
                localRealmGossipRevision_=gossip.revision;localRealmGossipConfirm_=0;localRealmDialogueFocus_=true;
                if(gossip.offeredQuestId)localRealmDialogueQuest_=gossip.offeredQuestId;
            }
            const game::LocalQuestDefinition* selected=nullptr;
            for(const auto& q:quests) if(q.id==localRealmDialogueQuest_ && game::localQuestOffered(self,npc,q)) selected=&q;
            // A quest a script offered from this creature (OFFER_QUEST) has its
            // details page here too, acceptable from it.
            const game::LocalQuestDefinition* offeredDef=gossipHere&&gossip.offeredQuestId&&localRealmDialogueQuest_==gossip.offeredQuestId&&
                !game::localQuestProgress(self,gossip.offeredQuestId)?content.quest(gossip.offeredQuestId):nullptr;
            if(!selected&&offeredDef)selected=offeredDef;
            if(!selected) localRealmDialogueQuest_=0;
            const auto* progress=selected?game::localQuestProgress(self,selected->id):nullptr;
            ImGui::SetCursorPos(ImVec2(33*s,85*s));
            ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.16f,.10f,.055f,1));
            ImGui::BeginChild("DialogueText",ImVec2(280*s,314*s),false);
            // Detail pages contain text only. Scroll them without moving focus
            // away from Accept/Back (D-pad remains ordinary button navigation).
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1))
                ImGui::SetScrollY(std::max(0.f, ImGui::GetScrollY()-120.f*s));
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1))
                ImGui::SetScrollY(ImGui::GetScrollY()+120.f*s);
            if(selected) {
                ImGui::TextWrapped("%s",selected->title.c_str());
                ImGui::Separator();
                std::string description=selected->description;
                for(size_t p=0;(p=description.find("$N",p))!=std::string::npos;p+=self.name.size()) description.replace(p,2,self.name);
                for(size_t p=0;(p=description.find("$B",p))!=std::string::npos;++p) description.replace(p,2,"\n");
                ImGui::TextWrapped("%s",description.c_str());
                ImGui::Spacing();ImGui::TextUnformatted("Quest Objectives");
                for(size_t i=0;i<selected->objectives.size();++i) {
                    const auto& o=selected->objectives[i];
                    const auto done=progress && i<progress->progress.size()?progress->progress[i]:0;
                    ImGui::TextWrapped("%s: %u / %u",objectiveName(content,o).c_str(),done,o.count);
                }
                ImGui::Spacing();ImGui::TextUnformatted("Rewards");
                ImGui::Text("%u XP | %u Copper",selected->xp,selected->money);
                for(size_t i=0;i<game::localQuestRewardCount(*selected);++i) {
                    const auto reward=game::localQuestRewardAt(*selected,i);
                    ImGui::TextWrapped("%s x%u",itemName(content,reward.itemId).c_str(),reward.count);
                }
                if(!selected->rewardChoices.empty())ImGui::TextUnformatted("Choose one reward:");
                for(size_t i=0;i<selected->rewardChoices.size();++i) {
                    const auto reward=selected->rewardChoices[i];
                    const auto label=itemName(content,reward.itemId)+" x"+std::to_string(reward.count);
                    ImGui::PushID(int(i));ImGui::BeginDisabled(!progress || progress->status!=game::LocalQuestStatus::Complete);
                    if(ImGui::Button(label.c_str()) && localRealm_->turnInQuest(selected->id,npc.guid,uint32_t(i+1))) {
                        localRealmDialogueQuest_=0;localRealmDialogueFocus_=true;
                    }
                    ImGui::EndDisabled();ImGui::PopID();
                }
            } else {
                // The npc_text of the gossip page (its variant kept by the
                // page's revision), else the greeting.
                std::string greeting;
                if(gossipHere&&!gossip.questMenu&&gossip.textId&&gossip.textId!=game::kLocalGossipDefaultText) {
                    game::LocalGossipText text;
                    if(localRealm_->gossipText(gossip.textId,text))greeting=game::localGossipPageText(self,text,gossip.revision);
                }
                if(greeting.empty())greeting=game::localNpcGreeting(self,npc,content);
                ImGui::TextWrapped("%s",greeting.c_str());
                ImGui::Spacing();
                bool offered=false;
                for(const auto& q:quests) {
                    if(!game::localQuestOffered(self,npc,q))continue;
                    const auto* active=game::localQuestProgress(self,q.id);
                    const std::string title=std::string(active?"? ":"! ")+q.title+"##"+std::to_string(q.id);
                    const bool clicked=ImGui::Selectable(title.c_str(),false,0,ImVec2(0,26*s));
                    if(!offered && localRealmDialogueFocus_) {
                        ImGui::SetKeyboardFocusHere(-1);localRealmDialogueFocus_=false;
                    }
                    if(clicked) {localRealmDialogueQuest_=q.id;localRealmDialogueFocus_=true;}
                    offered=true;
                }
                // 2.40: the gossip options the authority admitted (their
                // conditions, the creature's flags), after the quests as the
                // GossipFrame lists them. A gossip option goes to the
                // authority (its GOSSIP_SELECT rows, the action menu, the box
                // price); a service option opens the window this fallback
                // has for it. Flights, the bank and the stable have no window
                // here and are left to their own rows or the original UI.
                if(gossipHere&&!gossip.questMenu) {
                    static const char* icons[]={"[?] ","[$] ","[>] ","[T] ","[*] ","[*] ","[$] ","[...] ","[t] ","[x] ","[.] "};
                    const game::LocalGossipShownOption* confirming=nullptr;
                    for(const auto& o:gossip.options)if(o.id==localRealmGossipConfirm_)confirming=&o;
                    if(confirming) {
                        ImGui::Spacing();
                        ImGui::TextWrapped("%s",confirming->boxText.empty()?"Are you sure?":confirming->boxText.c_str());
                        if(confirming->boxMoney)ImGui::Text("Cost: %u copper",confirming->boxMoney);
                        ImGui::PushID(int(confirming->id));
                        ImGui::BeginDisabled(self.money<confirming->boxMoney);
                        if(ImGui::Button("Accept")){localRealm_->gossipSelect(npc.guid,gossip.menuId,confirming->id);localRealmGossipConfirm_=0;}
                        ImGui::EndDisabled();ImGui::SameLine();
                        if(ImGui::Button("Cancel"))localRealmGossipConfirm_=0;
                        ImGui::PopID();
                        if(localRealmDialogueFocus_){ImGui::SetKeyboardFocusHere(-1);localRealmDialogueFocus_=false;}
                        offered=true;
                    } else for(const auto& o:gossip.options) {
                        if(o.type==4||o.type==9||o.type==14)continue;
                        const std::string label=std::string(o.icon<11?icons[o.icon]:"[?] ")+o.text+"##gossip"+std::to_string(o.id);
                        const bool clicked=ImGui::Selectable(label.c_str(),false,0,ImVec2(0,26*s));
                        if(!offered&&localRealmDialogueFocus_){ImGui::SetKeyboardFocusHere(-1);localRealmDialogueFocus_=false;}
                        offered=true;
                        if(!clicked)continue;
                        switch(o.type) {
                            case 3:localRealmVendorOpen_=true;break;
                            case 5:localRealmTrainerOpen_=true;break;
                            case 8:localRealm_->setHome(npc.guid);break;
                            case 13:localRealmAuctionOpen_=true;break;
                            case 16:localRealm_->resetTalents();break;
                            default:break;
                        }
                        if(o.boxMoney||!o.boxText.empty())localRealmGossipConfirm_=o.id;
                        else localRealm_->gossipSelect(npc.guid,gossip.menuId,o.id);
                    }
                }
                // A flight master sells flights the same way a quest giver
                // offers quests: one row per destination this character has
                // already discovered. The list comes from the host, so a guest
                // cannot invent a route, and the price is the one the client's
                // own TaxiPath row states.
                if (npc.flightMaster) {
                    if(std::find(self.knownTaxiNodes.begin(),self.knownTaxiNodes.end(),npc.taxiNodeId)==self.knownTaxiNodes.end())
                        if(ImGui::Selectable("Discover this flight point"))localRealm_->discoverTaxi(npc.guid);
                    const auto destinations = localRealm_->flightDestinations();
                    if (destinations.empty()) {
                        ImGui::Spacing();
                        ImGui::TextWrapped("You have not discovered any flight paths from here yet.");
                    } else {
                        ImGui::Spacing();
                        ImGui::TextUnformatted("Flight Paths");
                        const auto& travel = localRealm_->travel();
                        for (uint32_t nodeId : destinations) {
                            const auto* node = travel.node(nodeId);
                            if (!node) continue;
                            const auto* route = travel.directPath(npc.taxiNodeId, nodeId);
                            const uint32_t cost = route ? route->cost : 0;
                            char row[192];
                            std::snprintf(row, sizeof(row), "%s  -  %u copper##taxi%u",
                                          node->name.c_str(), cost, nodeId);
                            if (ImGui::Selectable(row, false, 0, ImVec2(0, 26 * s))) {
                                if (localRealm_->takeFlight(nodeId)) localRealmDialogueNpc_ = 0;
                            }
                            if (!offered && localRealmDialogueFocus_) {
                                ImGui::SetKeyboardFocusHere(-1);
                                localRealmDialogueFocus_ = false;
                            }
                            offered = true;
                        }
                    }
                }
                // The services this character offers, each a row that opens the
                // window for it. The flags come from the authority - the host
                // resolved them against the catalog's npcflag when it spawned
                // the NPC - so a guest offers exactly what the host will honour
                // rather than deciding for itself.
                //
                // The window is opened without ending the conversation: closing
                // it returns the player to the person they were talking to,
                // which is what the original client does.
                const auto serviceRow = [&](bool offered, const char* label, bool& open) {
                    if (!offered) return;
                    ImGui::Spacing();
                    if (ImGui::Selectable(label, false, 0, ImVec2(0, 26 * s))) open = true;
                    // The first selectable row on the page takes the pad's
                    // focus, whichever kind it is - a quest, a flight or a
                    // service. Without this the page opens with nothing
                    // selected and Cross does nothing.
                    if (!offered && localRealmDialogueFocus_) {
                        ImGui::SetKeyboardFocusHere(-1);
                        localRealmDialogueFocus_ = false;
                    }
                    offered = true;
                };
                serviceRow(npc.auctioneer, "Browse the auction house##auction", localRealmAuctionOpen_);
                serviceRow(npc.vendor || npc.repairer,
                           npc.vendor ? "Show me your wares##vendor" : "Repair my equipment##vendor",
                           localRealmVendorOpen_);
                serviceRow(npc.classTrainer || npc.professionTrainer,
                           npc.classTrainer ? "Train me##trainer" : "Teach me a profession##trainer",
                           localRealmTrainerOpen_);
                if (npc.innkeeper) {
                    ImGui::Spacing();
                    if (ImGui::Selectable("Make this inn my home##innkeeper", false, 0, ImVec2(0, 26 * s)))
                        localRealm_->setHome(npc.guid);
                }
                // A civilian still has a greeting even when no quest is offered.
            }
            ImGui::EndChild();ImGui::PopStyleColor();
            ImGui::SetCursorPos(ImVec2(33*s,418*s));
            if(selected) {
                const bool complete=progress && progress->status==game::LocalQuestStatus::Complete;
                const bool accept=!progress && self.quests.size()<game::LocalGameplay::MaxQuests;
                (void)offeredDef;
                ImGui::BeginDisabled((!complete&&!accept) || (complete&&!selected->rewardChoices.empty()));
                if(ImGui::Button(complete?"Complete Quest":progress?"In Progress":"Accept",ImVec2(138*s,24*s))) {
                    const bool okay=complete?localRealm_->turnInQuest(selected->id,npc.guid):localRealm_->acceptQuest(selected->id,npc.guid);
                    if(okay) {localRealmDialogueQuest_=0;localRealmDialogueFocus_=true;}
                }
                if(localRealmDialogueFocus_ && (complete||accept) && localRealmDialogueQuest_) {
                    ImGui::SetKeyboardFocusHere(-1);localRealmDialogueFocus_=false;
                }
                ImGui::EndDisabled();ImGui::SameLine();
                if(ImGui::Button("Back",ImVec2(130*s,24*s))) {
                    localRealmDialogueQuest_=0;localRealmDialogueFocus_=true;
                }
                if(localRealmDialogueFocus_ && !complete && !accept && localRealmDialogueQuest_) {
                    ImGui::SetKeyboardFocusHere(-1);localRealmDialogueFocus_=false;
                }
            } else {
                if(ImGui::Button("Goodbye",ImVec2(138*s,24*s)))localRealmDialogueNpc_=0;
                if(localRealmDialogueFocus_ && !localRealmDialogueQuest_) {
                    ImGui::SetKeyboardFocusHere(-1);localRealmDialogueFocus_=false;
                }
            }
            ImGui::SetCursorPos(ImVec2(30*s,464*s));
            ImGui::TextWrapped("D-pad: select | L1/R1: scroll | Cross: confirm | Circle: back");
            ImGui::End();
        }
    }

    if (localRealmNpcPanelOpen_) {
        const float width = std::min(430.0f*scale, io.DisplaySize.x*.45f);
        beginPanel("Nearby Characters and Dialogue", ImVec2(io.DisplaySize.x-width-margin, margin),
                   ImVec2(width, io.DisplaySize.y-165*scale), &localRealmNpcPanelOpen_);
        ImGui::TextUnformatted("Select a character (R3: Cursor, Cross: Confirm)");
        if (ImGui::BeginChild("nearby", ImVec2(0,135*scale), ImGuiChildFlags_Borders)) {
            for (const auto* npc : nearby) {
                char label[256];
                std::snprintf(label,sizeof(label),"%s  %.0f m##%llu", npc->name.c_str(),
                              distanceTo(self,*npc),static_cast<unsigned long long>(npc->guid));
                if (ImGui::Selectable(label,npc->guid==localRealmTarget_)) select(npc->guid);
            }
            if (nearby.empty()) ImGui::TextWrapped("No characters nearby. Move closer to an inhabited area.");
        }
        ImGui::EndChild();
        for (const auto& portal : localRealm_->availablePortals()) {
            ImGui::PushID(static_cast<int>(portal.id));
            ImGui::Separator();
            ImGui::TextWrapped("Portal: %s", portal.name.c_str());
            ImGui::BeginDisabled(self.dead);
            const bool grouped=localRealm_->partyView().partyId!=0;
            if (ImGui::Button(portal.instanceMap ? (grouped ? "Enter Party Instance" : "Enter Shared Instance") : "Use Portal"))
                enterLocalRealmPortal(portal.id, false);
            if (portal.instanceMap && !grouped) {
                ImGui::SameLine();
                if (ImGui::Button("Private Instance")) enterLocalRealmPortal(portal.id, true);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        // Zeppelins and ships. A transport is a moving platform rather than a
        // character, so there is nobody to talk to: the way aboard is to stand
        // on the deck while it is docked and say so. The authority checks the
        // distance and whether the hull is actually at its stop, which is why
        // this offers the button rather than deciding.
        for (const auto& hull : localRealm_->transports()) {
            if (hull.mapId != self.mapId || self.instanceId) continue;
            const float dx = hull.x - self.x, dy = hull.y - self.y, dz = hull.z - self.z;
            if (dx*dx + dy*dy + dz*dz > 60.0f * 60.0f) continue;
            // The hull's own state carries no name - it is a position at a
            // moment - so the route it belongs to supplies it.
            const char* hullName = "Transport";
            for (const auto& route : localRealm_->travel().transportRoutes())
                if (route.entry == hull.entry && !route.name.empty()) hullName = route.name.c_str();
            ImGui::PushID(static_cast<int>(hull.entry));
            ImGui::Separator();
            ImGui::TextWrapped("%s%s", hullName, hull.docked ? " (docked)" : " (under way)");
            if (self.transportEntry == hull.entry) {
                if (ImGui::Button("Step Off")) localRealm_->leaveTransport();
            } else {
                ImGui::BeginDisabled(self.dead || self.transportEntry != 0);
                if (ImGui::Button("Board")) localRealm_->boardTransport(hull.entry);
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        const auto* npc = target();
        if (npc) {
            ImGui::TextWrapped("%s",npc->name.c_str());
            if (ImGui::Button(npc->dead ? "Loot" : "Talk")) interact();
            if (distanceTo(self,*npc)>8) ImGui::TextWrapped("Move closer to talk or loot.");
        }
        ImGui::End();
    }
    // Service windows; entrance triggers are processed in updateLocalRealm.
    renderLocalAuctionHouse(scale);
    renderLocalVendorPanel(scale);
    renderLocalTrainerPanel(scale);
    if (localRealmJournalOpen_) {
        const float width = std::min(420.0f*scale,io.DisplaySize.x*.44f);
        beginPanel("Quest Log",ImVec2(io.DisplaySize.x-width-margin,margin),
                   ImVec2(width,io.DisplaySize.y-165*scale),&localRealmJournalOpen_);
        ImGui::Text("Active: %zu / %zu | Completed: %zu", self.quests.size(),
                    game::LocalGameplay::MaxQuests, self.completedQuestIds.size());
        if (self.quests.size() >= game::LocalGameplay::MaxQuests)
            ImGui::TextWrapped("Quest log full. Turn in or abandon a quest to make room.");
        bool any = false;
        for (const auto& progress : self.quests) {
            const auto* quest = content.quest(progress.id);
            any = true;
            ImGui::PushID(static_cast<int>(progress.id));
            ImGui::Separator();
            const auto title = quest ? quest->title : "Quest " + std::to_string(progress.id);
            ImGui::TextWrapped("%s%s", progress.status==game::LocalQuestStatus::Complete ? "Complete: " : "", title.c_str());
            if (quest) {
                ImGui::TextWrapped("%s",quest->description.c_str());
                for (size_t i=0;i<quest->objectives.size();++i) {
                    const auto& objective = quest->objectives[i];
                    const uint32_t done = i<progress.progress.size() ? progress.progress[i] : 0;
                    ImGui::TextWrapped("%s: %u / %u",objectiveName(content,objective).c_str(),done,objective.count);
                }
                if (const auto* turnIn=content.npc(quest->turnInEntry)) ImGui::TextWrapped("Turn in to: %s",turnIn->name.c_str());
            }
            if (ImGui::Button("Abandon Quest")) ImGui::OpenPopup("Abandon this quest?");
            if (ImGui::BeginPopupModal("Abandon this quest?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("%s", title.c_str());
                ImGui::TextUnformatted("Quest progress will be reset. Items will be kept.");
                if (ImGui::Button("Abandon")) {
                    localRealm_->abandonQuest(progress.id);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Keep")) ImGui::CloseCurrentPopup();
                ImGui::SetItemDefaultFocus();
                if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight,false) ||
                    ImGui::IsKeyPressed(ImGuiKey_Escape,false)) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (!any) ImGui::TextWrapped("No active quests. Talk to the marked characters in your area.");
        ImGui::End();
    }
    if (localRealmInventoryOpen_) {
        const float width=std::min(440.0f*scale,io.DisplaySize.x*.45f);
        beginPanel("Inventory and Equipment",ImVec2((io.DisplaySize.x-width)*.5f,margin),
                   ImVec2(width,io.DisplaySize.y-165*scale),&localRealmInventoryOpen_);
        ImGui::TextUnformatted("Equipment");
        ImGui::BeginChild("WornItems", ImVec2(0, 210*scale), true);
        for (size_t i=0;i<self.equipment.size();++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::TextWrapped("%s: %s",game::kLocalEquipmentSlotNames[i],self.equipment[i] ? itemName(content,self.equipment[i]).c_str() : "Empty");
            if (self.equipment[i]) {
                ImGui::BeginDisabled(self.dead);
                if (ImGui::SmallButton("Unequip")) localRealm_->unequipItem(static_cast<uint8_t>(i));
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::Text("Inventory: %zu / %zu slots",self.inventory.size(),game::LocalGameplay::MaxInventory);
        ImGui::TextDisabled("Equipped copies remain in inventory.");
        for (size_t inventoryIndex = 0; inventoryIndex < self.inventory.size(); ++inventoryIndex) {
            const auto& stack = self.inventory[inventoryIndex];
            const auto* item=content.item(stack.itemId);
            if (!item) continue;
            ImGui::PushID(static_cast<int>(inventoryIndex));
            ImGui::Separator();
            ImGui::TextWrapped("%s  x%u",item->name.c_str(),stack.count);
            if (item->attack || item->armor || item->maxHealth)
                ImGui::Text("Attack +%u  Armor +%u  Health +%u",item->attack,item->armor,item->maxHealth);
            if (item->heal || item->mana) ImGui::Text("Healing %u  Resource %u",item->heal,item->mana);
            ImGui::BeginDisabled(self.dead);
            const uint32_t equipMask = game::localEquipmentSlotMask(item->inventoryType, item->slot);
            if (equipMask) {
                const auto equippedCopies = std::count(self.equipment.begin(), self.equipment.end(), item->id);
                uint32_t ownedCopies = 0;
                for (const auto& owned : self.inventory) if (owned.itemId == item->id) ownedCopies += owned.count;
                ImGui::BeginDisabled(uint32_t(equippedCopies) >= ownedCopies);
                if (ImGui::Button(equippedCopies ? "Equip another copy" : "Equip")) localRealm_->equipItem(item->id);
                ImGui::EndDisabled();
                if (equippedCopies) ImGui::TextDisabled("Equipped copies: %u", unsigned(equippedCopies));
                if ((equipMask & (equipMask - 1)) && ImGui::BeginCombo("Slot", "Choose slot")) {
                    for (size_t slot = 0; slot < self.equipment.size(); ++slot) if (equipMask & game::localEquipmentSlotBit(slot)) {
                        if (ImGui::Selectable(game::kLocalEquipmentSlotNames[slot], self.equipment[slot] == item->id))
                            localRealm_->equipItem(item->id, static_cast<uint8_t>(slot));
                    }
                    ImGui::EndCombo();
                }
            } else if (item->heal || item->mana) {
                if (ImGui::Button("Use")) localRealm_->useItem(item->id);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Learned Abilities");
        if(!content.spellDiagnostic.empty())ImGui::TextWrapped("%s",content.spellDiagnostic.c_str());
        for (auto id:self.knownSpells) {
            const auto* spell=content.spell(id);if(!spell)continue;
            ImGui::PushID(int(id));spellIcon(*spell,24);
            const uint32_t cooldown=ui::localActionCooldownMs(self,content,id);
            ImGui::TextWrapped("%s | cost %u | %.1fs cast | %.1fs cooldown",spell->name.c_str(),game::localSpellResourceCost(self,content,*spell),game::localSpellCastTime(self,content,*spell)/1000.f,cooldown/1000.f);
            if(!spell->unsupportedReason.empty())ImGui::TextWrapped("Unavailable: %s",spell->unsupportedReason.c_str());
            else {
                ImGui::BeginDisabled(self.dead||self.castingSpellId||cooldown);
                if(ImGui::Button("Cast"))localRealm_->castSpell(id,(spell->heal||spell->periodicHeal)?self.guid:localRealmTarget_);
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::End();
    }
    if (self.dead) {
        // A passive hint must not capture keyboard/gamepad navigation: the
        // released ghost needs to walk back from the graveyard immediately.
        const char* hint = !self.ghost ? "You have died. Releasing your spirit..." :
            localRealm_->canReclaimCorpse() ? "Square / 1: Reclaim your corpse" :
            "Return to your corpse. Press Square / 1 when nearby to revive.";
        const ImVec2 size=ImGui::CalcTextSize(hint);
        const ImVec2 at((io.DisplaySize.x-size.x)*.5f,io.DisplaySize.y*.28f);
        auto* draw=ImGui::GetForegroundDrawList();
        draw->AddText(ImVec2(at.x+1,at.y+1),IM_COL32(0,0,0,220),hint);
        draw->AddText(at,IM_COL32(190,220,255,255),hint);
    }
    if (localRealmMenuOpen_) {
        const float width=430*scale;
        ImGui::SetNextWindowFocus();
        beginPanel("Game Menu",ImVec2((io.DisplaySize.x-width)*.5f,io.DisplaySize.y*.29f),ImVec2(width,300*scale),&localRealmMenuOpen_);
        if (ImGui::Button("Resume",ImVec2(-1,32*scale))) localRealmMenuOpen_=false;
        ImGui::SetItemDefaultFocus();
        if (ImGui::Button("Save Game",ImVec2(-1,32*scale))) localRealm_->save();
        if (self.instanceId && self.hasInstanceReturn && ImGui::Button("Leave Instance",ImVec2(-1,32*scale))) localRealm_->leaveInstance();
        ImGui::Text("Map %u | Instance %u", self.mapId, self.instanceId);
        if (ImGui::Button("Save and Return to Main Menu",ImVec2(-1,32*scale))) logoutToLogin();
        ImGui::TextWrapped("The host saves the shared world and its characters. The world continues running while this menu is open.");
        for (const auto& player:localRealm_->players())
            ImGui::Text("%s - Level %u - %u/%u HP",player.name.c_str(),player.level,player.health,player.maxHealth);
        ImGui::End();
    }
    localRealmPopupOpen_ = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
#ifdef WOWEE_PS4
    platform::ps4::setInputMenuNavigation(platform::ps4::MenuOwner::LocalRealm,
        localRealmMenuOpen_ || localRealmDialogueNpc_ || localRealmNpcPanelOpen_ ||
        localRealmInventoryOpen_ || localRealmJournalOpen_);
#endif
}

} // namespace wowee::core
