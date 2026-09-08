#pragma once

#include <string>

namespace wowee {
namespace ui {

/// Resolve WoW's `|4singular:plural;` escape against the number before it.
///
/// The interface writes counted things as one string and leaves the ending to
/// the client: SECONDS is "|4Second:Seconds;" and CAMP_TIMER is
/// "%d %s until logout", so without this the logout prompt reads
/// "5 |4Second:Seconds; until logout" verbatim. Seventy-five strings in this
/// interface use the escape.
///
/// The number is the last one in the text *before* the escape, which is how
/// WoW decides: "1 second", "2 seconds". With no number before it the plural
/// is the safer reading - a bare SECONDS is a column heading, not a count.
///
/// Only `|4` is handled. `|1`, `|2` and `|3` are the declension and gender
/// forms the localised builds need and the English data never emits; a rule
/// for those would be guessing at grammar this text does not carry.
std::string resolvePluralEscapes(const std::string& text);

}  // namespace ui
}  // namespace wowee
