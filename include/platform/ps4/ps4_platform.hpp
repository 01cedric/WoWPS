#pragma once
// PS4 (OpenOrbis) platform layer for the WoWee client.
//
// Contract between the platform pieces (src/platform/ps4/*.cpp) and the
// upstream code that calls them from #ifdef WOWEE_PS4 branches. Everything
// here is main-thread only unless stated otherwise.
//
//   system_ps4.cpp   process/system services, modules, paths, logging, network
//   input_ps4.cpp    ScePad -> core::Input (keyboard/mouse emulation) + ImGui
//   audio_backend_ps4.cpp  miniaudio custom backend on sceAudioOut (see its header)

#include <cstddef>
#include <cstdint>
#include <string>
#include <functional>

struct ImGuiContext;

namespace wowee {
namespace platform {
namespace ps4 {

// ---- system_ps4.cpp -------------------------------------------------------

/// Load the system modules the client needs (SysModule, SystemService, Net,
/// UserService, Pad, AudioOut, Ime), initialise networking (sceNetInit +
/// pool), set the process environment (WOW_DATA_PATH, WOWEE_* tuning knobs)
/// and create the writable directories. Call first. Does NOT hide the splash
/// screen - see hideSplashScreen() below.
bool initSystem();
void shutdownSystem();

/// Hides the splash screen after a frame was successfully submitted for
/// presentation, or after opening a system progress/error dialog. Creating
/// the swapchain alone is not enough: the client may still be loading.
void hideSplashScreen();

/// Main-thread startup checkpoints, written without a userspace buffer to
/// logs/boot.log. The previous launch is retained as logs/boot_previous.log.
void reportBootStage(const char* stage);
// Main-thread scope: extend backend breadcrumbs through the first world frames.
void setFrameTraceEnabled(bool enabled);
bool frameTraceEnabled();
/// Async-signal-safe: append the fatal signal to the already-open boot log.
/// Does not call the core logger, allocate, acquire locks or format strings.
void reportCrashSignal(int signal);
/// Install the fatal-signal reporter (SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS)
/// through the kernel's own sigaction ABI, with an alternate signal stack for
/// the calling thread so a stack overflow still leaves a marker. Returns
/// false when the kernel refused the alternate stack or a handler.
bool installCrashReporter();
/// Give the calling thread an alternate signal stack and a name the crash
/// marker can print. Call once from any thread whose crash should be
/// attributable (the audio device thread, loaders). Safe to call repeatedly.
void registerCrashReportingThread(const char* name);
/// The stack size a thread created with no attributes would get from the
/// kernel. Logged at startup; every thread the client creates gets 2 MiB
/// through the pthread_create wrapper in threads_ps4.cpp regardless.
size_t defaultThreadStackBytes();
/// Show a bounded system error dialog on initialization failure. Independent
/// of the game renderer; the full diagnosis remains in the log files.
void showStartupError(const char* message);

/// Root the user fills with their own game client: /data/wow_ps
/// (MPQs under /data/wow_ps/Data). Also the default WOW_DATA_PATH.
std::string dataRoot();
/// Writable directory for settings, logs, caches and saved variables:
/// /data/wow_ps/wowps (created by initSystem).
std::string writableRoot();
/// Directory the package was launched from (read-only): /app0
std::string appRoot();

/// Resolve a host name with sceNetResolver (musl's getaddrinfo has no DNS on
/// the console). Returns the IPv4 address in network byte order, or 0.
uint32_t resolveIPv4(const std::string& host);

/// Kick the system watchdog / process the system event queue once per frame
/// (sceSystemServiceReceiveEvent) so suspend/resume and the PS button work.
void pumpSystemEvents();

// ---- input_ps4.cpp --------------------------------------------------------

/// Open the pad for the initial user.
bool initInput();
void shutdownInput();

/// Read the pad once per frame and:
///  - drive core::Input through its virtual-key API (left stick -> WASD,
///    L3 -> autorun, R3 -> mouse-look toggle, triggers/shoulders -> action
///    bar rows, D-pad -> targeting/UI focus, Options -> Escape, Touchpad ->
///    cursor and clicks, Share -> screenshot)
///  - move the emulated mouse cursor (right stick or touchpad) and report
///    mouse buttons (X = left click, Circle = right click)
///  - feed ImGui (mouse position/buttons/wheel, nav gamepad keys, text input
///    from the on-screen keyboard)
void pumpInput();

/// Emulated mouse state for core::Input (client pixels).
struct MouseState {
    float x = 0, y = 0;
    float dx = 0, dy = 0;
    bool left = false, right = false, middle = false;
    float wheel = 0;
    bool relativeMode = false;   // right-stick mouse-look active
};
const MouseState& mouseState();
/// Cursor mode is explicit on console; camera drag need not enable SDL relative mode.
bool inputCursorVisible();

/// Whether the given SDL scancode is currently held through the pad mapping.
/// (Input::update merges this with its own state on PS4.)
bool isVirtualKeyHeld(int sdlScancode);

/// Open the system on-screen keyboard (sceImeDialog) for a text field.
/// The callback receives the final UTF-8 text on the main thread; empty
/// string + accepted=false when cancelled.
void showKeyboard(const std::string& title, const std::string& initialText,
                  bool password, std::function<void(const std::string&, bool accepted)> done);
bool keyboardOpen();

} // namespace ps4
} // namespace platform
} // namespace wowee
