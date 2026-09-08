#pragma once

/**
 * wowee_expansion_names.hpp - the four expansions, as the .w* sidecars spell them.
 *
 * Three of the formats gate an entry on an expansion - character features,
 * loading screens and the dungeon finder - and each declares its own enum for
 * it with the same four values. That much is deliberate: a format is meant to
 * be readable from its own header alone.
 *
 * The *words* are not each format's own. They are what the JSON sidecar
 * carries, so an exported file and the importer that reads it back have to
 * agree on them exactly, and there were six places to keep in step: a name
 * function in each of the three formats and a parse lambda in each of their
 * three CLI handlers. A word changed in one of the six would export files that
 * no longer import.
 */

#include <cstdint>
#include <string_view>

namespace wowee {
namespace pipeline {

/// Expansion gate values, as all three formats number them.
enum : uint8_t {
    kExpansionClassic = 0,
    kExpansionTBC = 1,
    kExpansionWotLK = 2,
    kExpansionTurtleWoW = 3,
};

/// The word a sidecar carries for this value.
///
/// "unknown" for anything else, rather than a guess: a file being written
/// should say so plainly where a value has gone wrong, and "unknown" does not
/// parse back to a real expansion either, so the mistake survives a round trip
/// instead of being quietly turned into Classic.
inline const char* expansionName(uint8_t e) {
    switch (e) {
        case kExpansionClassic:   return "classic";
        case kExpansionTBC:       return "tbc";
        case kExpansionWotLK:     return "wotlk";
        case kExpansionTurtleWoW: return "turtle";
        default:                  return "unknown";
    }
}

/// There is deliberately no factionName beside this, and the reason is worth
/// writing down because the invitation to add one is obvious: nine of these
/// formats have a switch answering "alliance", and they look like nine copies.
///
/// They are not. The formats number their factions five different ways -
/// Alliance is 0 in chars, auction, channels and guilds; 1 in mounts, maps,
/// events and achievements; and companions starts at AllianceOnly = 1 with no
/// zero at all. Maps has a fourth value, Contested, that the others do not.
/// A shared value-to-word function would answer confidently and wrongly for
/// most of them.
///
/// What made the expansion words shareable is that all three formats that use
/// them number identically, 0 to 3. That is a property of those three, not a
/// rule about .w* formats, and it was checked rather than assumed.

/// The value behind that word. Anything unrecognised reads as Classic.
///
/// Not symmetrical with the above, and deliberately: a sidecar with a word this
/// build does not know is still a file worth loading, and Classic is the gate
/// that admits everything.
inline uint8_t expansionFromName(std::string_view name) {
    if (name == "tbc")    return kExpansionTBC;
    if (name == "wotlk")  return kExpansionWotLK;
    if (name == "turtle") return kExpansionTurtleWoW;
    return kExpansionClassic;
}

}  // namespace pipeline
}  // namespace wowee
