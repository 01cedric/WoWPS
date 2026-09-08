#pragma once

// How many textures the interface may upload in a single frame, across all of
// it.
//
// Every screen here uploads its icons one at a time through
// uploadImGuiTexture, and outside a batch that submits and *waits* on the
// shared immediate fence for each one. A wait costs whatever the GPU is
// already busy with, so while the terrain streams it can be tens of
// milliseconds apiece.
//
// Each screen used to hold its own allowance - four here, six there, five
// screens deep - and nothing knew what the others had spent. A frame where
// several were open could pay twenty-two of those waits back to back. A live
// log shows the result: the interface stage reaching 697ms, then the immediate
// fence and command buffer reported as still in use on every iteration, then
// the device lost. It reproduces with this client's own interface drawing and
// FrameXML switched off entirely, so it is not about which interface is on
// screen - it is about how many of these waits fit in one frame.
//
// One budget for all of them, so the worst case is a number rather than a sum.
// Going over is not a failure: the caller returns null *without caching it* and
// the icon arrives a frame or two later, which is what these screens already
// did with their own counters.

namespace wowee::ui {

/// True when this frame still has room to upload, and claims one if so.
/// Answers false once the frame's budget is spent.
bool claimUiTextureUpload();

} // namespace wowee::ui
