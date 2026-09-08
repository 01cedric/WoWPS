// The three windows a player opens by talking to the character who runs the
// service: the auction house, a merchant's stock, and a trainer's list.
//
// They are here rather than in application_local_ui.cpp because that file is
// the heads-up display - unit frames, nameplates, the action bar, the panels
// bound to a key - and these three are opened by a conversation instead. They
// also share a shape the HUD panels do not: each is only usable while the
// player is standing at the character who provides it, so each closes itself
// the moment the authority stops offering the service. That check is the whole
// reason they are written together.
//
// Nothing here decides anything. Every price, every stock list and every
// trainable ability is asked of the realm, which is the authority in
// singleplayer and the host over LAN; a guest's window is the host's answer
// with a network round trip in front of it. The buttons send commands and
// report what comes back - they never move an item or a coin themselves.

#include "core/application.hpp"
#include "game/local_realm.hpp"
#include "game/local_bots.hpp"
#include "game/local_services.hpp"
#include "game/item_text.hpp"
#include "ui/ui_colors.hpp"
#include "ui/ui_texture_load.hpp"
#include "core/logger.hpp"
#include "core/window.hpp"
#include "pipeline/asset_manager.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <string>

namespace wowee::core {
namespace {

/// One panel, positioned and sized the way the overlay's own panels are. Kept
/// local rather than shared with application_local_ui.cpp: that file's version
/// closes over its frame state, and copying five lines is cheaper than making
/// two files depend on each other's locals.
bool beginServicePanel(const char* title, ImVec2 position, ImVec2 size, float scale, bool* open) {
    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.9f);
    const bool visible = ImGui::Begin(title, open,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
    ImGui::SetWindowFontScale(scale);
    return visible;
}

/// Gold, silver and copper on one line, after a label. Money in this world is
/// base-100 copper and showing it as a raw count of copper is how a 4g 30s item
/// reads as "43000" - a number no player converts in their head.
void labelledPrice(const char* label, uint32_t copper) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ui::renderCoinsFromCopper(copper);
}

std::string itemLabel(const game::LocalWorldContent& content, uint32_t id) {
    const auto* item = content.item(id);
    return item ? item->name : "Item " + std::to_string(id);
}

} // namespace

// The auction house.
//
// An empty auction house is the single most obvious sign that a world has
// nobody else in it, which is why the playerbots stock it: what they gather
// while they wander is what appears here, priced from the item's own vendor
// value. Buying from them takes gold out of the world exactly as it would on a
// live realm.
//
// Three tabs, because the three things a player does here are different
// questions: what can I buy, what am I selling, and what do I want to put up.
void Application::renderLocalAuctionHouse(float scale) {
    if (!localRealmAuctionOpen_ || !localRealm_) return;
    const auto* live = localRealm_->localPlayer();
    if (!live) { localRealmAuctionOpen_ = false; return; }
    const auto self = *live;
    const auto& content = localRealm_->content();
    const auto& io = ImGui::GetIO();

    const float width = std::min(620.0f * scale, io.DisplaySize.x * 0.62f);
    const float height = std::min(520.0f * scale, io.DisplaySize.y - 140.0f * scale);
    if (!beginServicePanel("Auction House", ImVec2((io.DisplaySize.x - width) * 0.5f, 70.0f * scale),
                           ImVec2(width, height), scale, &localRealmAuctionOpen_)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Your money:");
    ImGui::SameLine();
    ui::renderCoinsFromCopper(self.money);
    ImGui::Separator();

    if (ImGui::BeginTabBar("##auctionTabs")) {
        // Browse. Everything on the board, including the player's own listings,
        // because that is what a browse window shows - the seller column says
        // whose it is.
        if (ImGui::BeginTabItem("Browse")) {
            localRealmAuctionTab_ = 0;
            const auto& board = localRealm_->auctions();
            if (board.empty()) {
                ImGui::TextWrapped("Nothing is up for auction right now. Listings appear as other "
                                   "characters gather things worth selling.");
            }
            ImGui::BeginChild("##auctionList", ImVec2(0, -78 * scale), ImGuiChildFlags_Borders);
            for (const auto& listing : board) {
                const auto* item = content.item(listing.itemId);
                ImGui::PushID(static_cast<int>(listing.id));
                const bool selected = listing.id == localRealmAuctionSelected_;
                char row[256];
                std::snprintf(row, sizeof(row), "%s%s##row",
                              itemLabel(content, listing.itemId).c_str(),
                              listing.count > 1 ? (" x" + std::to_string(listing.count)).c_str() : "");
                // The icon first, so the board reads the way the bags do. A
                // missing display entry simply draws no icon rather than a
                // placeholder that looks like a broken item.
                if (item && item->displayId) {
                    if (const auto icon = ui::itemIconTexture(item->displayId, assetManager.get(), window.get())) {
                        ImGui::Image((ImTextureID)(uintptr_t)icon, ImVec2(22 * scale, 22 * scale));
                        ImGui::SameLine();
                    }
                }
                if (ImGui::Selectable(row, selected, 0, ImVec2(0, 24 * scale))) {
                    localRealmAuctionSelected_ = listing.id;
                    // Opening bid, or one copper above the standing one - the
                    // two numbers a player would type themselves.
                    localRealmAuctionBid_ = int(listing.highestBid ? listing.highestBid + 1 : listing.bid);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", listing.sellerName.c_str());
                ImGui::SameLine();
                labelledPrice("Buyout", listing.buyout);
                if (listing.highestBid) {
                    ImGui::SameLine();
                    labelledPrice("| Bid", listing.highestBid);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("| %.0f min left", listing.remainingSeconds / 60.0f);
                ImGui::PopID();
            }
            ImGui::EndChild();

            // The selected listing's actions. Held below the list rather than
            // on each row: a board of buttons is a board of misclicks, and
            // buying is not undoable.
            const game::LocalAuction* chosen = nullptr;
            for (const auto& listing : board) if (listing.id == localRealmAuctionSelected_) chosen = &listing;
            if (!chosen) {
                localRealmAuctionSelected_ = 0;
                ImGui::TextDisabled("Select a listing to bid or buy.");
            } else {
                const bool mine = chosen->seller == self.guid;
                ImGui::BeginDisabled(mine || self.money < chosen->buyout);
                if (ImGui::Button("Buyout", ImVec2(120 * scale, 26 * scale)))
                    localRealm_->buyoutAuction(chosen->id);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150 * scale);
                ImGui::InputInt("##bid", &localRealmAuctionBid_, 100, 10000);
                // A bid below the minimum or above the buyout is not a bid the
                // authority would take, so the window does not offer it.
                const uint32_t minimum = chosen->highestBid ? chosen->highestBid + 1 : chosen->bid;
                localRealmAuctionBid_ = std::clamp(localRealmAuctionBid_, int(minimum), int(chosen->buyout));
                ImGui::SameLine();
                ImGui::BeginDisabled(mine || self.money < uint32_t(localRealmAuctionBid_));
                if (ImGui::Button("Place Bid", ImVec2(120 * scale, 26 * scale)))
                    localRealm_->bidAuction(chosen->id, uint32_t(localRealmAuctionBid_));
                ImGui::EndDisabled();
                if (mine) ImGui::TextDisabled("This is your own listing.");
                else if (self.money < minimum) ImGui::TextDisabled("You cannot afford the minimum bid.");
            }
            ImGui::EndTabItem();
        }

        // What the player is selling, and what has been bid on it.
        if (ImGui::BeginTabItem("My Auctions")) {
            localRealmAuctionTab_ = 1;
            bool any = false;
            for (const auto& listing : localRealm_->auctions()) {
                if (listing.seller != self.guid) continue;
                any = true;
                ImGui::Separator();
                ImGui::TextWrapped("%s x%u", itemLabel(content, listing.itemId).c_str(), listing.count);
                labelledPrice("Buyout", listing.buyout);
                if (listing.highestBid) labelledPrice("Highest bid", listing.highestBid);
                else ImGui::TextDisabled("No bids yet.");
                ImGui::TextDisabled("%.0f minutes left", listing.remainingSeconds / 60.0f);
            }
            if (!any) ImGui::TextWrapped("You have nothing up for auction. Use the Sell tab to list something.");
            ImGui::EndTabItem();
        }

        // Listing something. One stack at a time, from the bags, at the price
        // the board would give it - a player who wants a different price is
        // better served by a working auction house than by a price field this
        // ruleset would have to validate against nothing.
        if (ImGui::BeginTabItem("Sell")) {
            localRealmAuctionTab_ = 2;
            ImGui::TextWrapped("Choose a stack to list. It is offered for thirty minutes at the "
                               "price the auction house sets from the item's own value.");
            ImGui::BeginChild("##sellList", ImVec2(0, 0), ImGuiChildFlags_Borders);
            for (size_t index = 0; index < self.inventory.size(); ++index) {
                const auto& stack = self.inventory[index];
                const auto* item = content.item(stack.itemId);
                if (!item || !stack.count) continue;
                ImGui::PushID(static_cast<int>(index));
                if (item->displayId) {
                    if (const auto icon = ui::itemIconTexture(item->displayId, assetManager.get(), window.get())) {
                        ImGui::Image((ImTextureID)(uintptr_t)icon, ImVec2(22 * scale, 22 * scale));
                        ImGui::SameLine();
                    }
                }
                ImGui::Text("%s x%u", item->name.c_str(), stack.count);
                ImGui::SameLine();
                const uint32_t buyout = game::LocalAuctionPricing::buyoutFor(*item, stack.count, 0.5f);
                labelledPrice("~", buyout);
                ImGui::SameLine();
                // An equipped copy is reserved by the paperdoll; listing it
                // would sell the sword out of the player's hand.
                const bool equipped = std::find(self.equipment.begin(), self.equipment.end(), item->id) != self.equipment.end();
                ImGui::BeginDisabled(equipped);
                if (ImGui::SmallButton("List")) localRealm_->listAuction(item->id, stack.count);
                ImGui::EndDisabled();
                if (equipped) { ImGui::SameLine(); ImGui::TextDisabled("(equipped)"); }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// The merchant window: buy from the stock, sell from the bags, and repair.
//
// Repair shares this window because on a live realm it shares the character -
// a blacksmith who sells you a hammer is the one who mends your armour, and
// splitting them into two windows would be a distinction the player never made.
void Application::renderLocalVendorPanel(float scale) {
    if (!localRealmVendorOpen_ || !localRealm_) return;
    const auto* live = localRealm_->localPlayer();
    const auto* vendor = localRealm_->nearbyVendor();
    const auto* repairer = localRealm_->nearbyRepairer();
    // Walking away ends the conversation. The authority has already stopped
    // accepting these commands by now, so a window left open would be a shop
    // whose every button reports "too far away".
    if (!live || (!vendor && !repairer)) { localRealmVendorOpen_ = false; return; }
    const auto self = *live;
    const auto& content = localRealm_->content();
    const auto& io = ImGui::GetIO();

    const float width = std::min(560.0f * scale, io.DisplaySize.x * 0.55f);
    const float height = std::min(500.0f * scale, io.DisplaySize.y - 140.0f * scale);
    const char* title = vendor ? "Merchant" : "Repairs";
    if (!beginServicePanel(title, ImVec2(io.DisplaySize.x * 0.08f, 70.0f * scale),
                           ImVec2(width, height), scale, &localRealmVendorOpen_)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(vendor ? vendor->name.c_str() : repairer->name.c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted("| Your money:");
    ImGui::SameLine();
    ui::renderCoinsFromCopper(self.money);

    if (repairer) {
        ImGui::Separator();
        if (ImGui::Button("Repair All Equipment", ImVec2(200 * scale, 26 * scale)))
            localRealm_->repairEquipment();
        ImGui::SameLine();
        ImGui::TextDisabled("Gear in this world does not wear out, so this costs nothing.");
    }

    if (vendor && ImGui::BeginTabBar("##vendorTabs")) {
        if (ImGui::BeginTabItem("Buy")) {
            const auto stock = localRealm_->vendorStock();
            if (stock.empty()) ImGui::TextWrapped("This merchant has nothing in stock right now.");
            ImGui::BeginChild("##buyList", ImVec2(0, 0), ImGuiChildFlags_Borders);
            for (uint32_t itemId : stock) {
                const auto* item = content.item(itemId);
                if (!item) continue;
                ImGui::PushID(static_cast<int>(itemId));
                if (item->displayId) {
                    if (const auto icon = ui::itemIconTexture(item->displayId, assetManager.get(), window.get())) {
                        ImGui::Image((ImTextureID)(uintptr_t)icon, ImVec2(22 * scale, 22 * scale));
                        ImGui::SameLine();
                    }
                }
                ImGui::TextUnformatted(item->name.c_str());
                ImGui::SameLine();
                const uint16_t bundle = uint16_t(std::min<uint32_t>(65535, game::localVendorBuyCount(itemId)));
                const uint32_t price = localRealm_->vendorBuyPrice(itemId, bundle);
                labelledPrice("", price);
                ImGui::SameLine();
                if (bundle > 1) { ImGui::Text("x%u", unsigned(bundle)); ImGui::SameLine(); }
                // Existing partial stacks can accept goods even when all bag
                // slots are occupied. The authority validates actual capacity.
                ImGui::BeginDisabled(self.money < price);
                if (ImGui::SmallButton("Buy")) localRealm_->buyFromVendor(itemId, bundle);
                ImGui::EndDisabled();
                if (item->attack || item->armor || item->maxHealth)
                    ImGui::TextDisabled("   Attack +%u  Armor +%u  Health +%u",
                                        item->attack, item->armor, item->maxHealth);
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sell")) {
            ImGui::TextWrapped("Selling is final. The merchant pays the item's own value.");
            ImGui::BeginChild("##sellToVendor", ImVec2(0, 0), ImGuiChildFlags_Borders);
            for (size_t index = 0; index < self.inventory.size(); ++index) {
                const auto& stack = self.inventory[index];
                const auto* item = content.item(stack.itemId);
                if (!item || !stack.count) continue;
                ImGui::PushID(static_cast<int>(index));
                if (item->displayId) {
                    if (const auto icon = ui::itemIconTexture(item->displayId, assetManager.get(), window.get())) {
                        ImGui::Image((ImTextureID)(uintptr_t)icon, ImVec2(22 * scale, 22 * scale));
                        ImGui::SameLine();
                    }
                }
                ImGui::Text("%s x%u", item->name.c_str(), stack.count);
                ImGui::SameLine();
                labelledPrice("", localRealm_->vendorSellPrice(item->id, stack.count));
                ImGui::SameLine();
                const bool equipped = std::find(self.equipment.begin(), self.equipment.end(), item->id) != self.equipment.end();
                ImGui::BeginDisabled(equipped);
                if (ImGui::SmallButton("Sell")) {
                    localRealmVendorSell_ = item->id;
                    ImGui::OpenPopup("Sell this item?");
                }
                ImGui::EndDisabled();
                if (equipped) { ImGui::SameLine(); ImGui::TextDisabled("(equipped)"); }
                // The confirmation names the stack it was opened for, not
                // whichever row happens to be under the cursor when it is
                // answered - the two can differ once the list shifts.
                if (ImGui::BeginPopupModal("Sell this item?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextWrapped("Sell %s x%u? You will not get it back.",
                                       item->name.c_str(), stack.count);
                    if (ImGui::Button("Sell") && localRealmVendorSell_ == item->id) {
                        localRealm_->sellToVendor(item->id, stack.count);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Keep")) ImGui::CloseCurrentPopup();
                    ImGui::SetItemDefaultFocus();
                    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// The trainer window: class abilities on one side, professions on the other.
//
// One window for both because one NPC is rarely both, and the player asked a
// single character to teach them something - which of the two lists is filled
// follows from who they are standing at.
void Application::renderLocalTrainerPanel(float scale) {
    if (!localRealmTrainerOpen_ || !localRealm_) return;
    const auto* live = localRealm_->localPlayer();
    const auto* classTrainer = localRealm_->nearbyClassTrainer();
    const auto* professionTrainer = localRealm_->nearbyProfessionTrainer();
    if (!live || (!classTrainer && !professionTrainer)) { localRealmTrainerOpen_ = false; return; }
    const auto self = *live;
    const auto& content = localRealm_->content();
    const auto& io = ImGui::GetIO();

    const float width = std::min(520.0f * scale, io.DisplaySize.x * 0.52f);
    const float height = std::min(460.0f * scale, io.DisplaySize.y - 140.0f * scale);
    if (!beginServicePanel("Trainer", ImVec2(io.DisplaySize.x * 0.08f, 70.0f * scale),
                           ImVec2(width, height), scale, &localRealmTrainerOpen_)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(classTrainer ? classTrainer->name.c_str() : professionTrainer->name.c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted("| Your money:");
    ImGui::SameLine();
    ui::renderCoinsFromCopper(self.money);
    ImGui::Separator();

    if (classTrainer) {
        ImGui::TextUnformatted("Abilities");
        const auto teachable = localRealm_->trainableSpells();
        if (teachable.empty())
            ImGui::TextWrapped("There is nothing new for you here yet. Come back at a higher level.");
        ImGui::BeginChild("##trainSpells", ImVec2(0, professionTrainer ? 170 * scale : 0),
                          ImGuiChildFlags_Borders);
        for (uint32_t spellId : teachable) {
            const auto* spell = content.spell(spellId);
            if (!spell) continue;
            ImGui::PushID(static_cast<int>(spellId));
            const auto icon = ui::cachedIconTexture(spell->iconId, assetManager.get(), window.get(),
                                                    localRealmSpellIconPaths_, localRealmSpellIconCache_);
            if (icon) {
                ImGui::Image((ImTextureID)(uintptr_t)icon, ImVec2(22 * scale, 22 * scale));
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(spell->name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Learn")) localRealm_->learnSpell(spellId);
            if (!spell->unsupportedReason.empty())
                ImGui::TextDisabled("   %s", spell->unsupportedReason.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    if (professionTrainer) {
        if (classTrainer) ImGui::Separator();
        ImGui::TextUnformatted("Professions");
        // What this character already has, with its rank, so the window says
        // what a player would otherwise have to open a second panel to learn.
        for (const auto& profession : self.professions) {
            const char* name = "Profession";
            for (const auto& line : localRealm_->skillLines())
                if (line.id == profession.skillId && !line.name.empty()) name = line.name.c_str();
            ImGui::PushID(int(profession.skillId));
            ImGui::Text("%s  %u / %u", name, profession.current, profession.max);
            ImGui::SameLine();
            ImGui::BeginDisabled(profession.current < profession.max);
            if (ImGui::SmallButton("Train next rank"))
                localRealm_->trainProfessionRank(profession.skillId);
            ImGui::EndDisabled();
            if (profession.current < profession.max)
                ImGui::TextDisabled("   Raise your skill to %u before the next rank.", profession.max);
            ImGui::PopID();
        }
        // And what this particular trainer teaches. A trainer teaches one
        // subject, which is why this is a single row rather than a list.
        const uint32_t subject = professionTrainer->trainerSkill;
        const bool known = std::any_of(self.professions.begin(), self.professions.end(),
                                       [&](const auto& p) { return p.skillId == subject; });
        if (subject && !known) {
            const char* name = "this profession";
            for (const auto& line : localRealm_->skillLines())
                if (line.id == subject && !line.name.empty()) name = line.name.c_str();
            ImGui::Separator();
            ImGui::BeginDisabled(self.professions.size() >= game::LocalGameplay::MaxProfessions);
            if (ImGui::Button(("Learn " + std::string(name)).c_str(), ImVec2(-1, 26 * scale)))
                localRealm_->learnProfession(subject);
            ImGui::EndDisabled();
            if (self.professions.size() >= game::LocalGameplay::MaxProfessions)
                ImGui::TextDisabled("You already know as many professions as you can carry.");
        } else if (subject && known) {
            ImGui::TextDisabled("You already know what this trainer teaches.");
        }
    }
    ImGui::End();
}

} // namespace wowee::core
