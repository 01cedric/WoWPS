#pragma once

#include <string>
#include <vector>

namespace wowee {
namespace ui {

/// Reading a macro body.
///
/// A macro is lines of text. Two panels needed to walk them: the action bar,
/// to work out which spell a macro casts so it can draw the right icon, and
/// the chat panel, to run them. Both wrote the same loop, right down to
/// stripping the carriage return that a macro edited on Windows carries.

/// The lines of a macro that are commands: neither blank, nor starting with #.
///
/// A `#` line is a directive rather than a command. `#showtooltip` is the one
/// that matters and it is read separately; anything else beginning with `#` is
/// a comment. Running one would put the literal text in the chat box and send
/// it as a say, which is how a macro's comment ends up on screen.
///
/// Leading whitespace goes, trailing does not: a slash command's arguments can
/// legitimately end in a space.
std::vector<std::string> macroCommandLines(const std::string& macroText);

/// The argument of the macro's `#showtooltip`, which names what its icon and
/// tooltip should show.
///
/// Three answers, and they are different:
///   an argument   show that spell or item
///   "__auto__"    the directive is there with nothing after it, so the icon
///                 follows whatever the macro would cast right now
///   empty         no directive at all, so the macro keeps its own icon
///
/// Collapsing the middle one into either of the others is why this is spelled
/// out: an empty `#showtooltip` is a request for the icon to track the macro,
/// not a request for nothing.
std::string macroShowtooltipArg(const std::string& macroText);

}  // namespace ui
}  // namespace wowee
