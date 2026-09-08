#include "core/input.hpp"
#ifdef WOWEE_PS4
#include "platform/ps4/ps4_platform.hpp"
#endif

namespace wowee {
namespace core {

Input& Input::getInstance() {
    static Input instance;
    return instance;
}

void Input::update() {
    // Copy current state to previous
    previousKeyState = currentKeyState;
    previousMouseState = currentMouseState;
    previousMousePosition = mousePosition;

#ifdef WOWEE_PS4
    // No keyboard or mouse on the console. The pad layer
    // (src/platform/ps4/input_ps4.cpp) holds scancodes on the player's behalf
    // - left stick W/S/Q/E, face buttons the action bar, and so on - and moves
    // an emulated cursor. Merged over the virtual keys exactly as the hardware
    // state is on the desktop, so the on-screen stick's hook still works.
    for (int i = 0; i < NUM_KEYS; ++i) {
        currentKeyState[i] = platform::ps4::isVirtualKeyHeld(i) || virtualKeyState[i];
    }

    const platform::ps4::MouseState& pad = platform::ps4::mouseState();
    mousePosition = glm::vec2(pad.x, pad.y);
    currentMouseState.fill(false);
    currentMouseState[SDL_BUTTON_LEFT] = pad.left;
    currentMouseState[SDL_BUTTON_MIDDLE] = pad.middle;
    currentMouseState[SDL_BUTTON_RIGHT] = pad.right;
    mouseLocked = pad.relativeMode;
#else
    // Get current keyboard state
    const Uint8* keyState = SDL_GetKeyboardState(nullptr);
    for (int i = 0; i < NUM_KEYS; ++i) {
        currentKeyState[i] = keyState[i] || virtualKeyState[i];
    }

    // Get current mouse state
    int mouseX, mouseY;
    Uint32 mouseState = SDL_GetMouseState(&mouseX, &mouseY);
    mousePosition = glm::vec2(static_cast<float>(mouseX), static_cast<float>(mouseY));

    // SDL_BUTTON(x) is defined as (1 << (x-1)), so button indices are 1-based.
    // SDL_BUTTON(0) is undefined behavior (negative shift). Start at 1.
    currentMouseState[0] = false;
    for (int i = 1; i < NUM_MOUSE_BUTTONS; ++i) {
        currentMouseState[i] = (mouseState & SDL_BUTTON(i)) != 0;
    }
#endif

    // Calculate mouse delta
    mouseDelta = mousePosition - previousMousePosition;
}

void Input::setVirtualKey(SDL_Scancode key, bool held) {
    if (key < 0 || key >= NUM_KEYS) return;
    virtualKeyState[key] = held;
}

void Input::clearVirtualKeys() {
    virtualKeyState.fill(false);
}

bool Input::isKeyPressed(SDL_Scancode key) const {
    if (key < 0 || key >= NUM_KEYS) return false;
    return currentKeyState[key];
}

bool Input::isKeyJustPressed(SDL_Scancode key) const {
    if (key < 0 || key >= NUM_KEYS) return false;
    return currentKeyState[key] && !previousKeyState[key];
}
bool Input::isMouseButtonPressed(int button) const {
    if (button < 0 || button >= NUM_MOUSE_BUTTONS) return false;
    return currentMouseState[button];
}

bool Input::isMouseButtonJustPressed(int button) const {
    if (button < 0 || button >= NUM_MOUSE_BUTTONS) return false;
    return currentMouseState[button] && !previousMouseState[button];
}

bool Input::isMouseButtonJustReleased(int button) const {
    if (button < 0 || button >= NUM_MOUSE_BUTTONS) return false;
    return !currentMouseState[button] && previousMouseState[button];
}
} // namespace core
} // namespace wowee
