#pragma once
// input_ps4.hpp - private helpers of src/platform/ps4/input_ps4.cpp.
//
// The public contract is include/platform/ps4/ps4_platform.hpp. What is here
// is the extra surface the two other users of the pad layer need and nothing
// else does: the ImGui backend (src/platform/ps4/imgui_impl_ps4.cpp) tells the
// pad layer what the interface is doing and reads the raw pad for gamepad
// navigation; the screens that draw their own text fields (ui/paper_ui.cpp)
// describe the field that has focus so the on-screen keyboard can open with
// its contents. Everything is main-thread only.

#include "platform/ps4/ps4_platform.hpp"
#include "platform/ps4/sdl_scancode_compat.h"

#include <orbis/Pad.h>

#include <cstdint>
#include <functional>
#include <string>

namespace wowee {
namespace platform {
namespace ps4 {

// ---- the pad mapping --------------------------------------------------------
//
// The table and the rule that picks a layer are here, rather than beside the
// code that reads them, for one reason: every other line of input_ps4.cpp needs
// the OpenOrbis SDK, so a mapping left in there could only ever be checked on
// the console - which is the one place a wrong slot is discovered by a player
// rather than by a test. These two declarations need nothing but the button
// bits, which tests/ps4_pad_stub supplies off-console.
//
// One layer is active at a time, chosen by which shoulder or trigger is held
// (text focus overrides them all). Within a layer each pad button does one
// thing: holds a scancode, is a mouse button, turns the wheel, or asks for
// something the layer handles itself. Face buttons in a modifier layer are
// keys, so the mouse buttons let go while a modifier is held.
//
// Slots on the action bar are the number row: 1-4 with R2, 5-8 with L2, 9 0
// - = with R1. Square alone is slot 1, so the most-used ability is a single
// button. L1 holds the character keys: jump, sit, reset camera, screenshot.
enum class Layer : uint8_t { Base, L1, R1, L2, R2, Text, Count };

enum class Action : uint8_t {
    Key,            // hold a scancode
    MouseLeft,
    MouseRight,
    WheelUp,
    WheelDown,
    Keyboard,       // open the on-screen keyboard for the focused field
    CursorToggle,   // right stick: cursor <-> look
    WorldMap,       // the original interface's world map (the bridge acts on it)
};

struct PadBinding {
    uint32_t button;      // ORBIS_PAD_BUTTON_*
    Layer layer;
    Action action;
    SDL_Scancode key;     // for Action::Key
    const char* note;
};

inline constexpr PadBinding kBindings[] = {
    // Base: the pad on its own.
    {ORBIS_PAD_BUTTON_CROSS,     Layer::Base, Action::MouseLeft,    SDL_SCANCODE_UNKNOWN,      "left click"},
    {ORBIS_PAD_BUTTON_CIRCLE,    Layer::Base, Action::MouseRight,   SDL_SCANCODE_UNKNOWN,      "right click: interact"},
    {ORBIS_PAD_BUTTON_SQUARE,    Layer::Base, Action::Key,          SDL_SCANCODE_1,            "action slot 1"},
    {ORBIS_PAD_BUTTON_TRIANGLE,  Layer::Base, Action::Key,          SDL_SCANCODE_TAB,          "target nearest NPC / mob"},
    {ORBIS_PAD_BUTTON_L3,        Layer::Base, Action::Key,          SDL_SCANCODE_NUMLOCKCLEAR,  "toggle autorun"},
    {ORBIS_PAD_BUTTON_R3,        Layer::Base, Action::CursorToggle, SDL_SCANCODE_UNKNOWN,      "right stick: cursor / look"},
    {ORBIS_PAD_BUTTON_OPTIONS,   Layer::Base, Action::Key,          SDL_SCANCODE_ESCAPE,       "game menu / close"},
    // The button in the middle of the pad. Not the PS button, which is what
    // "the big middle button" usually names: OrbisPadButton has no bit for it
    // and scePadReadState never reports one, because the system takes that
    // press to raise its own menu before the title is asked - a binding there
    // would be a button that never fires. The touchpad click is the other
    // middle button, it is much the largest thing on the pad, and it arrives.
    //
    // L3 remains autorun; touchpad click opens the world map.
    //
    // Action::WorldMap does nothing here on purpose. While a menu owns the pad
    // this whole table is skipped, so a row that opened the map could never
    // close it; the bridge reads the raw button instead (kPadWorldMapButton in
    // addons/local_framexml.hpp) and this row is where that is written down.
    {ORBIS_PAD_BUTTON_TOUCH_PAD, Layer::Base, Action::WorldMap,     SDL_SCANCODE_UNKNOWN,      "world map"},
    {ORBIS_PAD_BUTTON_UP,        Layer::Base, Action::WheelUp,      SDL_SCANCODE_UNKNOWN,      "zoom in"},
    {ORBIS_PAD_BUTTON_DOWN,      Layer::Base, Action::WheelDown,    SDL_SCANCODE_UNKNOWN,      "zoom out"},
    {ORBIS_PAD_BUTTON_LEFT,      Layer::Base, Action::Key,          SDL_SCANCODE_A,            "turn left"},
    {ORBIS_PAD_BUTTON_RIGHT,     Layer::Base, Action::Key,          SDL_SCANCODE_D,            "turn right"},

    // R2 held: action slots 1-4.
    {ORBIS_PAD_BUTTON_SQUARE,    Layer::R2, Action::Key, SDL_SCANCODE_1, "action slot 1"},
    {ORBIS_PAD_BUTTON_TRIANGLE,  Layer::R2, Action::Key, SDL_SCANCODE_2, "action slot 2"},
    {ORBIS_PAD_BUTTON_CIRCLE,    Layer::R2, Action::Key, SDL_SCANCODE_3, "action slot 3"},
    {ORBIS_PAD_BUTTON_CROSS,     Layer::R2, Action::Key, SDL_SCANCODE_4, "action slot 4"},
    {ORBIS_PAD_BUTTON_OPTIONS,   Layer::R2, Action::Key, SDL_SCANCODE_ESCAPE, "game menu / close"},

    // L2 held: action slots 5-8.
    {ORBIS_PAD_BUTTON_SQUARE,    Layer::L2, Action::Key, SDL_SCANCODE_5, "action slot 5"},
    {ORBIS_PAD_BUTTON_TRIANGLE,  Layer::L2, Action::Key, SDL_SCANCODE_6, "action slot 6"},
    {ORBIS_PAD_BUTTON_CIRCLE,    Layer::L2, Action::Key, SDL_SCANCODE_7, "action slot 7"},
    {ORBIS_PAD_BUTTON_CROSS,     Layer::L2, Action::Key, SDL_SCANCODE_8, "action slot 8"},
    {ORBIS_PAD_BUTTON_OPTIONS,   Layer::L2, Action::Key, SDL_SCANCODE_ESCAPE, "game menu / close"},

    // R1 held: action slots 9-12.
    {ORBIS_PAD_BUTTON_SQUARE,    Layer::R1, Action::Key, SDL_SCANCODE_9,      "action slot 9"},
    {ORBIS_PAD_BUTTON_TRIANGLE,  Layer::R1, Action::Key, SDL_SCANCODE_0,      "action slot 10"},
    {ORBIS_PAD_BUTTON_CIRCLE,    Layer::R1, Action::Key, SDL_SCANCODE_MINUS,  "action slot 11"},
    {ORBIS_PAD_BUTTON_CROSS,     Layer::R1, Action::Key, SDL_SCANCODE_EQUALS, "action slot 12"},
    {ORBIS_PAD_BUTTON_OPTIONS,   Layer::R1, Action::Key, SDL_SCANCODE_ESCAPE, "game menu / close"},

    // L1 held: the character.
    {ORBIS_PAD_BUTTON_CROSS,     Layer::L1, Action::Key, SDL_SCANCODE_SPACE,       "jump / swim up"},
    {ORBIS_PAD_BUTTON_SQUARE,    Layer::L1, Action::Key, SDL_SCANCODE_X,           "sit / stand"},
    {ORBIS_PAD_BUTTON_TRIANGLE,  Layer::L1, Action::Key, SDL_SCANCODE_R,           "reset camera"},
    {ORBIS_PAD_BUTTON_CIRCLE,    Layer::L1, Action::Key, SDL_SCANCODE_PRINTSCREEN, "screenshot"},
    {ORBIS_PAD_BUTTON_OPTIONS,   Layer::L1, Action::Key, SDL_SCANCODE_ESCAPE,      "game menu / close"},
    {ORBIS_PAD_BUTTON_UP,        Layer::L1, Action::Key, SDL_SCANCODE_UP,          "walk forward (arrow)"},
    {ORBIS_PAD_BUTTON_DOWN,      Layer::L1, Action::Key, SDL_SCANCODE_DOWN,        "walk back (arrow)"},
    {ORBIS_PAD_BUTTON_LEFT,      Layer::L1, Action::Key, SDL_SCANCODE_Q,           "strafe left"},
    {ORBIS_PAD_BUTTON_RIGHT,     Layer::L1, Action::Key, SDL_SCANCODE_E,           "strafe right"},

    // Text focus: a text field has the keyboard.
    {ORBIS_PAD_BUTTON_CROSS,     Layer::Text, Action::MouseLeft,  SDL_SCANCODE_UNKNOWN,   "left click"},
    {ORBIS_PAD_BUTTON_CIRCLE,    Layer::Text, Action::MouseRight, SDL_SCANCODE_UNKNOWN,   "right click"},
    {ORBIS_PAD_BUTTON_TRIANGLE,  Layer::Text, Action::Keyboard,   SDL_SCANCODE_UNKNOWN,   "on-screen keyboard"},
    {ORBIS_PAD_BUTTON_SQUARE,    Layer::Text, Action::Key,        SDL_SCANCODE_BACKSPACE, "backspace"},
    {ORBIS_PAD_BUTTON_OPTIONS,   Layer::Text, Action::Key,        SDL_SCANCODE_ESCAPE,    "close the box"},
    {ORBIS_PAD_BUTTON_TOUCH_PAD, Layer::Text, Action::Key,        SDL_SCANCODE_RETURN,    "send"},
    {ORBIS_PAD_BUTTON_UP,        Layer::Text, Action::Key,        SDL_SCANCODE_UP,        "chat history back"},
    {ORBIS_PAD_BUTTON_DOWN,      Layer::Text, Action::Key,        SDL_SCANCODE_DOWN,      "chat history forward"},
    {ORBIS_PAD_BUTTON_LEFT,      Layer::Text, Action::Key,        SDL_SCANCODE_LEFT,      "caret left"},
    {ORBIS_PAD_BUTTON_RIGHT,     Layer::Text, Action::Key,        SDL_SCANCODE_RIGHT,     "caret right"},
    {ORBIS_PAD_BUTTON_R1,        Layer::Text, Action::Key,        SDL_SCANCODE_TAB,       "next field"},
};

/// Which layer a set of held buttons selects. Pure, so the precedence - text
/// focus first, then L1, R1, L2, R2 - can be stated once and asserted.
constexpr Layer padLayerFor(uint32_t buttons, bool textFocus, bool inWorld,
                            bool actionBarNavigation) {
    if (textFocus) return Layer::Text;
    // The original interface's own hotbars take the raw shoulder presses while
    // the player is in the world, so the modifier layers must not also claim
    // them and synthesise a second set of slot keys underneath.
    if (inWorld && actionBarNavigation) return Layer::Base;
    if (buttons & ORBIS_PAD_BUTTON_L1) return Layer::L1;
    if (buttons & ORBIS_PAD_BUTTON_R1) return Layer::R1;
    if (buttons & ORBIS_PAD_BUTTON_L2) return Layer::L2;
    if (buttons & ORBIS_PAD_BUTTON_R2) return Layer::R2;
    return Layer::Base;
}

// ---- what the interface tells the pad layer ---------------------------------

/// The display the cursor lives on, in client pixels. Set from the window
/// size by the ImGui backend every frame; 1920x1080 until then.
void setInputDisplaySize(int width, int height);
void getInputDisplaySize(int* width, int* height);

/// In the world the right stick is mouse-look and the D-pad zooms and turns.
/// On the login, realm and character screens the stick moves the cursor.
void setInputInWorld(bool inWorld);
/// Local original-UI hotbars consume raw Square/Triangle/shoulder presses.
/// Stops synthesizing the legacy modifier action slots at the same time.
void setInputActionBars(bool enabled);

/// A text field has the keyboard (ImGui's WantTextInput, or a FrameXML edit
/// box). The face buttons stop being action keys: Triangle asks for the
/// on-screen keyboard, Square is backspace, the D-pad moves the caret and
/// walks the chat history, and the left stick stops walking.
void setInputTextFocus(bool focused);
/// Whether a field has it now. Read by the world-map button, which takes the
/// touchpad off the raw pad rather than out of the table above and so has to
/// stand down where the table already would: with a box open the touchpad is
/// Send, and opening the map on the press that sent a message is not a gesture
/// anybody made.
bool inputTextFocus();

/// An ImGui window has keyboard focus: the D-pad belongs to ImGui's gamepad
/// navigation rather than to zoom and turn.
void setInputUiFocus(bool focused);
enum class MenuOwner : unsigned { LocalRealm = 1, Settings = 2, FrameXml = 4 };
void setInputMenuNavigation(MenuOwner owner, bool open);
bool inputMenuNavigation();

/// True once for each press of the keyboard button (Triangle with text
/// focus). The ImGui backend answers by opening the keyboard for the field.
bool takeKeyboardRequest();

// ---- what the pad layer tells the interface ---------------------------------

/// The pad after the last pumpInput. Buttons are ORBIS_PAD_BUTTON_* bits;
/// sticks are -1..1 after the dead zone, +y down as the pad reports it.
struct PadState {
    bool connected = false;
    uint32_t buttons = 0;    ///< held
    uint32_t pressed = 0;    ///< went down this frame
    uint32_t released = 0;   ///< went up this frame
    float lx = 0.0f, ly = 0.0f;
    float rx = 0.0f, ry = 0.0f;
    float l2 = 0.0f, r2 = 0.0f;   ///< triggers, 0..1
};
const PadState& padState();

/// Whether a pad was opened. The ImGui backend advertises a gamepad on it.
bool padConnected();

// ---- synthesised input ---------------------------------------------------------

/// Press or release a key the way the pad mapping does: the emulated keyboard
/// state changes and an SDL_KEYDOWN/SDL_KEYUP lands on the event queue. The
/// ImGui backend delivers an on-screen keyboard result through these.
void pushKeyEvent(SDL_Scancode scancode, bool down);

/// Typed text: SDL_TEXTINPUT events, split at UTF-8 boundaries so no event
/// carries a partial character.
void pushTextInput(const std::string& utf8);

// ---- the on-screen keyboard ----------------------------------------------------

/// A text field that has focus this frame, described by the screen drawing
/// it. Noted every frame the field is focused; the ImGui backend takes it
/// after the frame and opens the keyboard with the field's contents when the
/// focus is new.
struct TextFieldHint {
    std::string title;      ///< over the keyboard; also what identifies the field
    std::string text;       ///< current contents, offered for editing
    bool password = false;
    bool numeric = false;
};
void noteTextField(const TextFieldHint& hint);
/// The hint noted since the last call. Cleared by the call.
bool takeTextFieldHint(TextFieldHint& out);

/// showKeyboard with the options the contract's signature has no room for.
struct KeyboardOptions {
    std::string title;
    std::string initialText;
    bool password = false;
    bool numeric = false;
    /// The dialog's confirm button says Send rather than OK (chat).
    bool sendLabel = false;
    int maxLength = 255;
};
void showKeyboardEx(const KeyboardOptions& options,
                    std::function<void(const std::string&, bool accepted)> done);

/// Draw the shared in-game text modal after UI composition, before ImGui::Render.
void renderKeyboard();

/// Exclusive ownership for an in-game text modal, including release debounce.
bool keyboardCapturesInput();
bool applicationKeyboardOpen();
void setApplicationKeyboardOpen(bool open);

// ---- the mouse ---------------------------------------------------------------------

/// What SDL_SetRelativeMouseMode does on the desktop: the cursor is hidden
/// while on, and put back where it was when the mode is left. The camera
/// controller turns it on for the length of a right-button drag.
void setRelativeMouseMode(bool enabled);

}  // namespace ps4
}  // namespace platform
}  // namespace wowee
