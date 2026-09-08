#pragma once

#include <cstdint>
#include <vector>

namespace wowee::game {

/// One stance, form or presence a class can take.
///
/// FrameXML's stance bar asks three questions about these and they have to
/// agree with each other: how many there are, what the i-th one looks like,
/// and what casting the i-th one does. Each was answered from its own list.
///
///   GetNumShapeshiftForms counted the known spells out of eight druid forms
///   GetShapeshiftFormInfo  described a fixed six, in a different order
///   CastShapeshiftForm     cast that same fixed six
///
/// So a druid who had learned Aquatic Form or Flight Form - both in the count
/// and in neither of the others - was counted a form the bar could not draw,
/// and every index past the first mismatch described one form and cast
/// another. The bar is built by walking 1..GetNumShapeshiftForms() and calling
/// GetShapeshiftFormInfo on each, so the two have to be the same list.
struct ShapeshiftForm {
    uint32_t spellId;   ///< What casting this form actually casts.
    uint8_t formId;     ///< The shapeshift form field's value while it is active.
    const char* name;
    const char* icon;
};

/// Every form the given class has, in the order the bar shows them.
///
/// Unfiltered: this is what the class can ever have, not what a character has
/// learned. `knownShapeshiftForms` is what a binding wants.
std::vector<ShapeshiftForm> allShapeshiftForms(uint8_t classId);

/// The forms of `classId` whose spell the player knows, in bar order.
///
/// The filter is not cosmetic. A count fixed per class puts a button on the
/// bar for something the character cannot use - a level 14 priest was offered
/// Shadowform, which is learned at 40 - and an unfiltered index makes the
/// second button describe a form the player has never learned.
template <typename KnownSpells>
std::vector<ShapeshiftForm> knownShapeshiftForms(uint8_t classId, const KnownSpells& known) {
    std::vector<ShapeshiftForm> out;
    for (const ShapeshiftForm& form : allShapeshiftForms(classId)) {
        if (known.count(form.spellId)) out.push_back(form);
    }
    return out;
}

}  // namespace wowee::game
