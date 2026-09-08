#pragma once
// imgui_impl_ps4.h - Dear ImGui platform backend for WoWee on PS4.
//
// Replaces imgui_impl_sdl2 on the console (the renderer side stays
// imgui_impl_vulkan, running over the real Vulkan ICD in
// ps4/third_party/ps4_vulkan). It feeds ImGuiIO from the pad layer in
// src/platform/ps4/input_ps4.cpp:
//
//   - display size and delta time;
//   - the emulated mouse: position every frame, buttons and wheel from the
//     SDL-style events the pad mapping queues, a software cursor;
//   - keys: the SDL_KEYDOWN/KEYUP events the pad mapping queues, translated
//     to ImGuiKey (with the modifier keys as ImGuiMod_*), so ImGui's own
//     shortcuts and the client's ImGuiKey bindings work off the pad;
//   - gamepad navigation (ImGuiBackendFlags_HasGamepad): the D-pad as
//     ImGuiKey_GamepadDpad* while an ImGui window has the focus;
//   - text: SDL_TEXTINPUT events as AddInputCharactersUTF8, and the
//     on-screen keyboard, opened when a text field takes the focus and
//     delivered back through the same events.
//
// Call order per frame, from UIManager:
//   ImGui_ImplPS4_ProcessEvent for every event the application drains,
//   ImGui_ImplPS4_NewFrame before ImGui::NewFrame,
//   ImGui_ImplPS4_SetInWorld from render (which knows the app state),
//   ImGui_ImplPS4_EndFrame after the interface is built, before ImGui::Render.

#include <SDL2/SDL.h>   // the PS4 compat shim: SDL_Event

bool ImGui_ImplPS4_Init(int displayWidth, int displayHeight);
void ImGui_ImplPS4_Shutdown();

/// Display size, delta time, mouse position, gamepad navigation. Before
/// ImGui::NewFrame.
void ImGui_ImplPS4_NewFrame(int displayWidth, int displayHeight);

/// One event from the application's queue: keys, text, mouse buttons and
/// wheel, mouse motion. Returns true when ImGui took something from it.
bool ImGui_ImplPS4_ProcessEvent(const SDL_Event* event);

/// Whether the client is in the world (the right stick looks) or on a menu
/// screen (the right stick is the cursor).
void ImGui_ImplPS4_SetInWorld(bool inWorld);

/// After the interface is built, before ImGui::Render: opens the on-screen
/// keyboard when a text field has just taken focus (or Triangle asked for
/// it), and tells the pad layer what the interface is doing.
/// `interfaceWantsText` is ui::interfaceTakingTypedInput(): a FrameXML edit
/// box has the keyboard, which ImGui's WantTextInput knows nothing about.
void ImGui_ImplPS4_EndFrame(bool interfaceWantsText);
