// imgui_impl_ps4.cpp - Dear ImGui platform backend for WoWee on PS4.
// See imgui_impl_ps4.h.

#include "imgui_impl_ps4.h"

#include "platform/ps4/ps4_platform.hpp"
#include "platform/ps4/input_ps4.hpp"
#include "core/logger.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // GetActiveID: which text field has the focus

#include <orbis/Pad.h>

#include <chrono>
#include <cstdio>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;
using wowee::platform::ps4::TextFieldHint;

struct BackendData {
    bool initialised = false;
    Clock::time_point lastFrame{};
    bool framedOnce = false;
    int displayW = 1920;
    int displayH = 1080;

    // Gamepad navigation keys sent last frame, so a held D-pad is not
    // re-announced every frame.
    bool navDown[8] = {};   // up, down, left, right
    bool uiFocusSent = false;

    // The on-screen keyboard.
    bool wasWantingText = false;
    std::string focusedField;     // identifies the field that had focus last frame
    bool inWorld = false;
};

BackendData g_bd;

// The SDL scancode of a key the pad mapping produced -> ImGuiKey. Only the
// keys that can come out of input_ps4.cpp's mapping and the letters, since
// ImGuiKey bindings (ui/keybinding_manager.cpp) read letters.
ImGuiKey scancodeToImGuiKey(SDL_Scancode sc) {
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
        return static_cast<ImGuiKey>(ImGuiKey_A + (sc - SDL_SCANCODE_A));
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
        return static_cast<ImGuiKey>(ImGuiKey_1 + (sc - SDL_SCANCODE_1));
    }
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) {
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (sc - SDL_SCANCODE_F1));
    }
    switch (sc) {
        case SDL_SCANCODE_0: return ImGuiKey_0;
        case SDL_SCANCODE_TAB: return ImGuiKey_Tab;
        case SDL_SCANCODE_LEFT: return ImGuiKey_LeftArrow;
        case SDL_SCANCODE_RIGHT: return ImGuiKey_RightArrow;
        case SDL_SCANCODE_UP: return ImGuiKey_UpArrow;
        case SDL_SCANCODE_DOWN: return ImGuiKey_DownArrow;
        case SDL_SCANCODE_PAGEUP: return ImGuiKey_PageUp;
        case SDL_SCANCODE_PAGEDOWN: return ImGuiKey_PageDown;
        case SDL_SCANCODE_HOME: return ImGuiKey_Home;
        case SDL_SCANCODE_END: return ImGuiKey_End;
        case SDL_SCANCODE_INSERT: return ImGuiKey_Insert;
        case SDL_SCANCODE_DELETE: return ImGuiKey_Delete;
        case SDL_SCANCODE_BACKSPACE: return ImGuiKey_Backspace;
        case SDL_SCANCODE_SPACE: return ImGuiKey_Space;
        case SDL_SCANCODE_RETURN: return ImGuiKey_Enter;
        case SDL_SCANCODE_KP_ENTER: return ImGuiKey_KeypadEnter;
        case SDL_SCANCODE_ESCAPE: return ImGuiKey_Escape;
        case SDL_SCANCODE_MINUS: return ImGuiKey_Minus;
        case SDL_SCANCODE_EQUALS: return ImGuiKey_Equal;
        case SDL_SCANCODE_GRAVE: return ImGuiKey_GraveAccent;
        case SDL_SCANCODE_COMMA: return ImGuiKey_Comma;
        case SDL_SCANCODE_PERIOD: return ImGuiKey_Period;
        case SDL_SCANCODE_SLASH: return ImGuiKey_Slash;
        case SDL_SCANCODE_SEMICOLON: return ImGuiKey_Semicolon;
        case SDL_SCANCODE_APOSTROPHE: return ImGuiKey_Apostrophe;
        case SDL_SCANCODE_LEFTBRACKET: return ImGuiKey_LeftBracket;
        case SDL_SCANCODE_RIGHTBRACKET: return ImGuiKey_RightBracket;
        case SDL_SCANCODE_BACKSLASH: return ImGuiKey_Backslash;
        case SDL_SCANCODE_NUMLOCKCLEAR: return ImGuiKey_NumLock;
        case SDL_SCANCODE_PRINTSCREEN: return ImGuiKey_PrintScreen;
        case SDL_SCANCODE_LCTRL: return ImGuiKey_LeftCtrl;
        case SDL_SCANCODE_RCTRL: return ImGuiKey_RightCtrl;
        case SDL_SCANCODE_LSHIFT: return ImGuiKey_LeftShift;
        case SDL_SCANCODE_RSHIFT: return ImGuiKey_RightShift;
        case SDL_SCANCODE_LALT: return ImGuiKey_LeftAlt;
        case SDL_SCANCODE_RALT: return ImGuiKey_RightAlt;
        case SDL_SCANCODE_LGUI: return ImGuiKey_LeftSuper;
        case SDL_SCANCODE_RGUI: return ImGuiKey_RightSuper;
        default: return ImGuiKey_None;
    }
}

const char* clipboardGet(ImGuiContext*) {
    // A copy ImGui reads before the next call; SDL's contract is a malloc'd
    // string the caller frees, and here the platform owns the buffer.
    static std::string held;
    char* text = SDL_GetClipboardText();
    held = text ? text : "";
    SDL_free(text);
    return held.c_str();
}

void clipboardSet(ImGuiContext*, const char* text) {
    SDL_SetClipboardText(text ? text : "");
}

// What identifies the field with the focus this frame: the hint a screen
// noted (the login form's own fields), else ImGui's active item, else the
// FrameXML edit box. The keyboard opens when this changes, not only when text
// focus begins, so clicking from the account box to the password box brings
// it back up.
std::string focusedFieldKey(bool haveHint, const TextFieldHint& hint,
                            bool imguiWantsText, bool interfaceWantsText) {
    if (haveHint) return "field:" + hint.title;
    if (imguiWantsText) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "imgui:%08x", static_cast<unsigned>(ImGui::GetActiveID()));
        return buf;
    }
    if (interfaceWantsText) return "framexml";
    return std::string();
}

// Open the on-screen keyboard for the focused field and deliver its answer
// as input. A FrameXML edit box gets the text appended and Enter (the
// dialog's button says Send, and that is the chat line going out). Every
// other field gets select-all, backspace, then the text: the dialog offered
// the field's contents for editing, so what comes back replaces them. ImGui
// trickles the key events over successive frames on its own.
void openKeyboardFor(bool haveHint, const TextFieldHint& hint, bool interfaceWantsText) {
    using namespace wowee::platform::ps4;
    KeyboardOptions options;
    const bool frameXml = !haveHint && interfaceWantsText;
    if (haveHint) {
        options.title = hint.title;
        options.initialText = hint.text;
        options.password = hint.password;
        options.numeric = hint.numeric;
    } else if (frameXml) {
        options.title = "Chat";
        options.sendLabel = true;
    } else {
        // An ImGui InputText: its buffer is the state of the active item, so
        // the dialog can offer what is in it (the reply replaces it below).
        options.title = "";
        if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetActiveID())) {
            options.password = (state->Flags & ImGuiInputTextFlags_Password) != 0;
            if (!options.password && state->TextSrc && state->TextLen > 0) {
                options.initialText.assign(state->TextSrc, static_cast<size_t>(state->TextLen));
            }
        }
    }
    showKeyboardEx(options, [frameXml](const std::string& text, bool accepted) {
        if (!accepted) return;
        if (frameXml) {
            pushTextInput(text);
            pushKeyEvent(SDL_SCANCODE_RETURN, true);
            pushKeyEvent(SDL_SCANCODE_RETURN, false);
            return;
        }
        pushKeyEvent(SDL_SCANCODE_LCTRL, true);
        pushKeyEvent(SDL_SCANCODE_A, true);
        pushKeyEvent(SDL_SCANCODE_A, false);
        pushKeyEvent(SDL_SCANCODE_LCTRL, false);
        pushKeyEvent(SDL_SCANCODE_BACKSPACE, true);
        pushKeyEvent(SDL_SCANCODE_BACKSPACE, false);
        pushTextInput(text);
    });
}

}  // namespace

bool ImGui_ImplPS4_Init(int displayWidth, int displayHeight) {
    using namespace wowee::platform::ps4;
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");

    g_bd = BackendData{};
    g_bd.initialised = true;
    g_bd.displayW = displayWidth > 0 ? displayWidth : 1920;
    g_bd.displayH = displayHeight > 0 ? displayHeight : 1080;
    setInputDisplaySize(g_bd.displayW, g_bd.displayH);

    io.BackendPlatformUserData = &g_bd;
    io.BackendPlatformName = "imgui_impl_ps4";
    if (padConnected()) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    // No OS cursor on the console: ImGui draws one, in whatever shape the
    // interface asks for (GameScreen's hand over an NPC, for one).
    io.MouseDrawCursor = true;

    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    platformIo.Platform_GetClipboardTextFn = clipboardGet;
    platformIo.Platform_SetClipboardTextFn = clipboardSet;

    LOG_INFO("ImGui PS4 backend: ", g_bd.displayW, "x", g_bd.displayH,
             padConnected() ? ", pad" : ", no pad");
    return true;
}

void ImGui_ImplPS4_Shutdown() {
    if (!g_bd.initialised) return;
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformUserData = nullptr;
    io.BackendPlatformName = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    g_bd = BackendData{};
}

void ImGui_ImplPS4_NewFrame(int displayWidth, int displayHeight) {
    using namespace wowee::platform::ps4;
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(g_bd.initialised && "ImGui_ImplPS4_Init not called");

    if (displayWidth > 0 && displayHeight > 0) {
        g_bd.displayW = displayWidth;
        g_bd.displayH = displayHeight;
        setInputDisplaySize(displayWidth, displayHeight);
    }
    io.DisplaySize = ImVec2(static_cast<float>(g_bd.displayW), static_cast<float>(g_bd.displayH));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    const Clock::time_point now = Clock::now();
    float dt = 1.0f / 60.0f;
    if (g_bd.framedOnce) {
        dt = std::chrono::duration<float>(now - g_bd.lastFrame).count();
    }
    g_bd.lastFrame = now;
    g_bd.framedOnce = true;
    io.DeltaTime = dt > 0.0f ? dt : 1.0f / 60.0f;

    // The cursor. Hidden while the right stick is looking, and while the
    // system keyboard is over everything.
    const MouseState& mouse = mouseState();
    io.AddMousePosEvent(mouse.x, mouse.y);
    io.MouseDrawCursor = inputCursorVisible() && (!keyboardOpen() || applicationKeyboardOpen());

    // Gamepad navigation: the D-pad, while an ImGui window has the focus and
    // no text field does. Off the interface the D-pad zooms and turns, and
    // the pad layer is told which it is.
    const bool uiFocus = !keyboardCapturesInput() && (inputMenuNavigation() || io.WantCaptureKeyboard) && !io.WantTextInput &&
                         (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0 &&
                         (io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;
    if (uiFocus != g_bd.uiFocusSent) {
        setInputUiFocus(uiFocus);
        g_bd.uiFocusSent = uiFocus;
    }
    const PadState& pad = padState();
    const struct { uint32_t button; ImGuiKey key; } navKeys[8] = {
        {ORBIS_PAD_BUTTON_UP, ImGuiKey_GamepadDpadUp},
        {ORBIS_PAD_BUTTON_DOWN, ImGuiKey_GamepadDpadDown},
        {ORBIS_PAD_BUTTON_LEFT, ImGuiKey_GamepadDpadLeft},
        {ORBIS_PAD_BUTTON_RIGHT, ImGuiKey_GamepadDpadRight},
        {ORBIS_PAD_BUTTON_CROSS, ImGuiKey_GamepadFaceDown},
        {ORBIS_PAD_BUTTON_CIRCLE, ImGuiKey_GamepadFaceRight},
        {ORBIS_PAD_BUTTON_L1, ImGuiKey_GamepadL1},
        {ORBIS_PAD_BUTTON_R1, ImGuiKey_GamepadR1},
    };
    for (int i = 0; i < 8; ++i) {
        const bool down = (i < 4 || inputMenuNavigation()) && uiFocus && pad.connected && (pad.buttons & navKeys[i].button) != 0;
        if (down != g_bd.navDown[i]) {
            io.AddKeyEvent(navKeys[i].key, down);
            g_bd.navDown[i] = down;
        }
    }
}

bool ImGui_ImplPS4_ProcessEvent(const SDL_Event* event) {
    if (!event || !g_bd.initialised) return false;
    ImGuiIO& io = ImGui::GetIO();
    switch (event->type) {
        case SDL_MOUSEMOTION:
            io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
            io.AddMousePosEvent(static_cast<float>(event->motion.x), static_cast<float>(event->motion.y));
            return true;
        case SDL_MOUSEWHEEL:
            io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
            io.AddMouseWheelEvent(event->wheel.preciseX, event->wheel.preciseY);
            return true;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            int button = -1;
            if (event->button.button == SDL_BUTTON_LEFT) button = 0;
            else if (event->button.button == SDL_BUTTON_RIGHT) button = 1;
            else if (event->button.button == SDL_BUTTON_MIDDLE) button = 2;
            if (button < 0) return false;
            io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
            io.AddMouseButtonEvent(button, event->type == SDL_MOUSEBUTTONDOWN);
            return true;
        }
        case SDL_TEXTINPUT:
            io.AddInputCharactersUTF8(event->text.text);
            return true;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            const bool down = (event->type == SDL_KEYDOWN);
            const SDL_Scancode sc = event->key.keysym.scancode;
            // The modifier keys are also the modifier flags ImGui's shortcuts
            // read, and a backend has to say both.
            switch (sc) {
                case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL:
                    io.AddKeyEvent(ImGuiMod_Ctrl, down); break;
                case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT:
                    io.AddKeyEvent(ImGuiMod_Shift, down); break;
                case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT:
                    io.AddKeyEvent(ImGuiMod_Alt, down); break;
                case SDL_SCANCODE_LGUI: case SDL_SCANCODE_RGUI:
                    io.AddKeyEvent(ImGuiMod_Super, down); break;
                default: break;
            }
            const ImGuiKey key = scancodeToImGuiKey(sc);
            if (key == ImGuiKey_None) return false;
            io.AddKeyEvent(key, down);
            io.SetKeyEventNativeData(key, static_cast<int>(event->key.keysym.sym),
                                     static_cast<int>(sc), static_cast<int>(sc));
            return true;
        }
        default:
            return false;
    }
}

void ImGui_ImplPS4_SetInWorld(bool inWorld) {
    if (g_bd.inWorld == inWorld) return;
    g_bd.inWorld = inWorld;
    wowee::platform::ps4::setInputInWorld(inWorld);
}

void ImGui_ImplPS4_EndFrame(bool interfaceWantsText) {
    using namespace wowee::platform::ps4;
    if (!g_bd.initialised) return;
    ImGuiIO& io = ImGui::GetIO();

    TextFieldHint hint;
    const bool haveHint = takeTextFieldHint(hint);
    const bool imguiWantsText = io.WantTextInput;
    const bool wantsText = imguiWantsText || interfaceWantsText || haveHint;
    setInputTextFocus(wantsText);

    const std::string field = wantsText
        ? focusedFieldKey(haveHint, hint, imguiWantsText, interfaceWantsText)
        : std::string();
    const bool newField = wantsText && (!g_bd.wasWantingText || field != g_bd.focusedField);
    const bool asked = takeKeyboardRequest();

    // Character creation owns a letter-by-letter in-game keyboard. Never
    // open a second system IME over that field, including its closing frame.
    const bool characterName = haveHint && hint.title == "Character name";
    if (!characterName && (newField || (asked && wantsText)) && !keyboardCapturesInput()) {
        openKeyboardFor(haveHint, hint, interfaceWantsText);
    }

    // Keep the origin field latched while the modal temporarily takes focus.
    if (!keyboardOpen()) {
        g_bd.wasWantingText = wantsText;
        g_bd.focusedField = field;
    } else if (newField && !g_bd.wasWantingText) {
        g_bd.wasWantingText = wantsText; g_bd.focusedField = field;
    }
    renderKeyboard();
}
