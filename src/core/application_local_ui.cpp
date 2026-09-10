#include "core/application.hpp"
#include "ui/local_nameplate_policy.hpp"
#include "ui/local_pad_focus.hpp"
#include "addons/addon_manager.hpp"
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
    const auto* npc = content.npc(objective.entry);
    return npc ? npc->name : "Target " + std::to_string(objective.entry);
}
}

void Application::renderLocalRealmOverlay() {
    if (!localRealmEntered_ || !localRealm_ || state != AppState::IN_GAME || !renderer) return;
    const auto* livePlayer = localRealm_->localPlayer();
    if (!livePlayer) return;
    // Action methods may mutate snapshots; retain a stable view for this frame.
    const auto self = *livePlayer;
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
    if(localFrameXml_.ready() && targetPad.connected && !io.WantTextInput &&
       !platform::ps4::keyboardCapturesInput() && (targetPad.pressed&ORBIS_PAD_BUTTON_TRIANGLE))toggleTarget=true;
#endif
    if(toggleTarget){
        const auto next=ui::toggledLocalTarget(localRealmTarget_,game::nearestLivingLocalTarget(self,npcs));
        if(!next){localRealm_->stopAttack();if(gameHandler)gameHandler->clearFocus();}
        select(next);
        LOG_INFO("[PAD_TARGET] ",next?"selected nearest target":"cleared target");
    }
    auto interactWith = [&](const game::LocalRealmNpc& npc) {
        if (npc.dead) { localRealm_->loot(npc.guid); return; }
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
    if(keys && game::nearbyLocalMailbox(content,self))
        ImGui::GetForegroundDrawList()->AddText(ImVec2(24.f,io.DisplaySize.y*.58f),IM_COL32(255,220,130,255),"Square: Mailbox");
    auto primaryAction = [&](bool contextual) {
        if(contextual && gameHandler)if(const auto* mailbox=game::nearbyLocalMailbox(content,self)) {
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
    if(keys && localFrameXml_.ready() && !localFrameXml_.padBarFocused() && actionPad.connected &&
       !platform::ps4::keyboardCapturesInput() && (actionPad.pressed&ORBIS_PAD_BUTTON_SQUARE) &&
       !(actionPad.pressed&ORBIS_PAD_BUTTON_TRIANGLE) && !ui::interfaceConsumedKey(ImGuiKey_GamepadFaceLeft))primaryAction(true);
#endif
    if (pressed(ImGuiKey_1)
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
    if (pressed(ImGuiKey_2)) cast(false);
    if (pressed(ImGuiKey_3)) cast(true);
    if (pressed(ImGuiKey_4)) interact();
    if (pressed(ImGuiKey_5)) {
        if(localFrameXml_.ready()) addonManager_->runInterfaceCommand("ToggleBackpack()");
        else localRealmInventoryOpen_ = !localRealmInventoryOpen_;
    }
    if (pressed(ImGuiKey_6)) {
        if(localFrameXml_.ready()) addonManager_->runInterfaceCommand("ToggleFrame(QuestLogFrame)");
        else localRealmJournalOpen_ = !localRealmJournalOpen_;
    }
    if (pressed(ImGuiKey_7)) {
        for (const auto& stack : self.inventory) {
            const auto* item = content.item(stack.itemId);
            if (item && item->heal && stack.count) { localRealm_->useItem(item->id); break; }
        }
    }
    if (pressed(ImGuiKey_8)) { if (self.dead) localRealm_->respawn(); else localRealm_->stopAttack(); }
    if (pressed(ImGuiKey_9)) localRealmNpcPanelOpen_ = !localRealmNpcPanelOpen_;
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
    actionButton("attack","Interface/Icons/Ability_MeleeDamage.blp","S","Talk / Attack / Loot — Square | Attack — R2 + Square",self.dead,[&]{primaryAction(true);});
    for(bool healing:{false,true}) {
        const auto* spell=selectedSpell(healing);
        const auto path=spell?localRealmSpellIconPaths_.find(spell->iconId):localRealmSpellIconPaths_.end();
        const char* icon=path!=localRealmSpellIconPaths_.end()?path->second.c_str():"Interface/Icons/INV_Misc_QuestionMark.blp";
        const uint32_t cooldownMs=spell?ui::localActionCooldownMs(self,spell->id):0;
        const bool disabled=self.dead||!spell||!spell->unsupportedReason.empty()||self.castingSpellId||cooldownMs;
        actionButton(healing?"heal":"spell",icon,healing?"R2 O":"R2 T",spell?spell->name.c_str():"No learned spell",disabled,[&]{cast(healing);},cooldownMs);
    }
    actionButton("interact","Interface/Icons/INV_Misc_Note_01.blp","R2 X","Talk / Loot — R2 + Cross",false,interact);
    actionButton("bags","Interface/Buttons/Button-Backpack-Up.blp","L2 S","Inventory / Equipment — L2 + Square",false,[&]{localRealmInventoryOpen_=!localRealmInventoryOpen_;});
    actionButton("quests","Interface/Icons/INV_Misc_Book_09.blp","L2 T","Quest log — L2 + Triangle",false,[&]{localRealmJournalOpen_=!localRealmJournalOpen_;});
    actionButton("potion","Interface/Icons/INV_Potion_54.blp","L2 O","Use healing item — L2 + Circle",self.dead,[&]{for(const auto& stack:self.inventory){const auto* item=content.item(stack.itemId);if(item&&item->heal&&stack.count){localRealm_->useItem(item->id);break;}}});
    actionButton("stop","Interface/Icons/Ability_Vanish.blp","L2 X",self.dead?"Revive — L2 + Cross":"Stop attack — L2 + Cross",false,[&]{if(self.dead)localRealm_->respawn();else localRealm_->stopAttack();});
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
            const game::LocalQuestDefinition* selected=nullptr;
            for(const auto& q:quests) if(q.id==localRealmDialogueQuest_ && game::localQuestOffered(self,npc,q)) selected=&q;
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
                ImGui::TextWrapped("%s",game::localNpcGreeting(self,npc,content).c_str());
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
            const uint32_t cooldown=ui::localActionCooldownMs(self,id);
            ImGui::TextWrapped("%s | cost %u + %u%% | %.1fs cast | %.1fs cooldown",spell->name.c_str(),spell->mana,spell->manaPercent,spell->castTimeMs/1000.f,cooldown/1000.f);
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
        const float width=430*scale;
        beginPanel("You Died",ImVec2((io.DisplaySize.x-width)*.5f,io.DisplaySize.y*.36f),ImVec2(width,130*scale));
        ImGui::TextWrapped("Revive at the starting location and continue your adventure.");
        if (ImGui::Button("Revive [L2 + Cross / 8]")) localRealm_->respawn();
        ImGui::End();
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
