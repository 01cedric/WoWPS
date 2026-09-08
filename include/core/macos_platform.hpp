#pragma once

#ifdef __APPLE__

namespace wowee {
namespace core {

/**
 * Suppress the macOS press-and-hold accent popup for this process.
 *
 * SDL2 leaves text input enabled for the whole session (SDL_VideoInit calls
 * SDL_StartTextInput when there's no screen keyboard), so AppKit routes key
 * events through NSTextInputContext even during normal gameplay.  Holding a
 * key that takes diacritics - A, S, E, and friends - then opens the accent
 * chooser instead of repeating the key.
 *
 * Registering ApplePressAndHoldEnabled=NO restores plain key repeat.  The
 * value lands in NSUserDefaults' registration domain, so it applies only to
 * this process and never writes to the user's saved preferences.
 *
 * Must be called before SDL_Init brings up NSApplication.
 */
void disablePressAndHoldAccents();

} // namespace core
} // namespace wowee

#endif // __APPLE__
