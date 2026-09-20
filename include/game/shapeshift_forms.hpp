#pragma once

#include <cstdint>
#include <vector>

namespace wowee::game {

/// One stance or form a class can take.
///
/// FrameXML's stance bar asks three questions about these and they have to
/// agree with each other: how many there are, what the i-th one looks like,
/// and what casting the i-th one does. Each was once answered from its own
/// list, so the bar described one form and cast another; the three now read a
/// single table, built by walking 1..GetNumShapeshiftForms() and calling
/// GetShapeshiftFormInfo on each.
///
/// the implementation removed the second disagreement, which was between this table and the
/// realm's own. It had 16 entries and **no shaman at all** while `kLocalForms`
/// (include/game/local_forms.hpp) had nine including Ghost Wolf, so a shaman got
/// `GetNumShapeshiftForms() == 0` and a fully implemented form had no button;
/// and the three death-knight "presences" were not shapeshift auras at all - no
/// aura-36 effect, and two of their three form ids do not exist in a 32-record
/// table. Ghost Wolf is added, the presences are removed, and two `static_assert`s
/// now refuse to let the two tables drift: every entry the realm models must name
/// the realm's class and form id, and every form the realm models must be on the
/// bar (Dire Bear excepted, which shares Bear's button).
///
/// The table is deliberately NOT just `kLocalForms`. Its two consumers want
/// different things: the local realm's stance bar filters it by what the
/// importer accepted, so a form this realm cannot cast never gets a button,
/// while the connected-server path does not - on a real server Moonkin Form,
/// Tree of Life, the flight forms, Stealth and Shadowform are real forms.
/// the source audit section 5.3 has the measurement.
struct ShapeshiftForm {
    uint32_t spellId;   ///< What casting this form actually casts.
    uint8_t formId;     ///< The shapeshift form field's value while it is active.
    const char* name;
    const char* icon;
};

/// Every form the given class has, in the order the bar shows them.
///
/// Unfiltered: this is what the class can ever have, not what a character has
/// learned. `knownShapeshiftForms` is what a binding wants. Every entry is a
/// form the realm accepts, so the stance-bar builder's acceptance filter can
/// no longer be the thing that removes a button.
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
        if(form.spellId==5487&&known.count(9634)){auto upgraded=form;upgraded.spellId=9634;upgraded.formId=8;upgraded.name="Dire Bear Form";out.push_back(upgraded);}
        else if (known.count(form.spellId)) out.push_back(form);
    }
    return out;
}

}  // namespace wowee::game
