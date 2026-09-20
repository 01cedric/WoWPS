#include "game/local_ui_changes.hpp"
#include <cassert>
#include <iostream>
using namespace wowee::game;
int main(){LocalRealmPlayer p;p.inventory={{117,4,0}};LocalUiChanges changes;changes.observe(p,0,0,0);assert(changes.observe(p,0,0,0)==0);
 p.inventory[0].bagSlot=17;assert(changes.observe(p,0,0,0)==LocalUiChanges::Bags);assert(changes.observe(p,0,0,0)==0);
 p.ridingSkill=75;assert(changes.observe(p,0,0,0)==LocalUiChanges::Professions);assert(changes.observe(p,0,0,0)==0);
 p.knownTaxiNodes={1};assert(changes.observe(p,0,0,0)==LocalUiChanges::Travel);p.flight.active=true;assert(changes.observe(p,0,0,0)==LocalUiChanges::Travel);p.flight.travelled=16;assert(changes.observe(p,0,0,0)==0);p.flight.active=false;assert(changes.observe(p,0,0,0)==LocalUiChanges::Travel);
 std::cout<<"PASS original UI notifications: physical-only bag moves, riding skill, discovered routes, flight start/end refresh once; flight motion does not rebuild dialogs\n";
}
