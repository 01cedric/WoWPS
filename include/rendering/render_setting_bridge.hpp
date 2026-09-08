#pragma once

/**
 * render_setting_bridge.hpp - the graphics CVars the interface may change while
 * the client is running.
 *
 * Renderer reads extShadowQuality and extGodrays out of the CVar file itself at
 * start-up, because it is built long before the interface exists and cannot
 * wait to be told. That covers the value the player logged in with and nothing
 * after it: SetCVar writes the store and saves it, and the renderer never looks
 * again, so moving either control did nothing at all until the next run.
 *
 * Both of these are read fresh every frame by the passes that use them, so
 * there is nothing to rebuild and nothing to synchronise - only a way for the
 * value to arrive. That is all this is.
 *
 * Sinks rather than a Renderer pointer, and its own header rather than
 * renderer.hpp, for the reason the gamma callbacks on LuaServices give: the Lua
 * API file has no business including a header that drags in Vulkan, and the one
 * thing it needs is these two numbers reaching the renderer. Unset sinks are
 * the normal state before a renderer exists and while the glue screen runs -
 * every caller checks, and a CVar set with nothing listening is simply stored
 * for the renderer's own start-up read to find.
 */

#include <functional>

namespace wowee {
namespace rendering {

struct RenderSettingSinks {
    /// extShadowQuality: 0 nothing casts, 1 terrain only, 2+ everything.
    /// Only the scope arrives live; the map's resolution is fixed at start-up
    /// because its images are, which is why the panel marks it as needing a
    /// restart. See Renderer::setShadowQuality.
    std::function<void(int)> setShadowQuality;

};

/// The live set. Filled by Renderer::initialize and cleared by its shutdown,
/// so a CVar written after the renderer is gone finds nothing rather than a
/// dangling capture.
inline RenderSettingSinks& renderSettingSinks() {
    static RenderSettingSinks sinks;
    return sinks;
}

}  // namespace rendering
}  // namespace wowee
