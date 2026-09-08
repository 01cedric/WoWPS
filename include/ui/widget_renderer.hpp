#pragma once

// Draws a WidgetTree.
//
// Kept apart from the tree itself so the layout rules stay testable without a
// Vulkan device. This half is the part that needs one.
//
// Textures come from the game's own Interface\ art through the existing asset
// path - read the BLP, upload it, hand ImGui the descriptor set - which is the
// same route the action bar already takes for its backpack button. Nothing new
// is shipped; it is the player's own install being drawn.

#include "ui/texture_content_bounds.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>

struct ImDrawList;   // global, as ImGui declares it
struct ImFont;
struct ImVec2;

namespace wowee {
namespace pipeline { class AssetManager; }
namespace rendering { class VkContext; }

namespace ui {

class WidgetTree;
struct Widget;

class WidgetRenderer {
public:
    void initialize(pipeline::AssetManager* assets, rendering::VkContext* vkCtx);
    // Requires the outgoing widget tree and GPU draws to be retired first.
    void releaseSessionTextures();

    /// Lay the tree out for this screen and draw it. Safe to call with no
    /// device or assets - it simply does nothing, which is what the headless
    /// tests want.
    void render(WidgetTree& tree, float screenW, float screenH);

    /// The two halves of render(), for callers that need something to happen
    /// between them.
    ///
    /// Hit testing reads the rects layout() produces, so it has to run early -
    /// before the frame's clicks are resolved, or a frame that moved this frame
    /// is clicked where it used to be. Drawing has the opposite requirement:
    /// the interface sits over the world, and the world's overlays - the
    /// nameplates and the minimap's blips - go into the same ImGui background
    /// list this does, where whatever is added last is on top. Drawn from here
    /// the panels went down first and every nameplate in the world showed
    /// through the bags.
    void layout(WidgetTree& tree, float screenW, float screenH);
    void draw(WidgetTree& tree, float screenW, float screenH);

    /// What is on screen, and what should be but is not - the instrumentation
    /// the FrameXML transition is being carried out with. Draws nothing.
    void reportWidgetDiagnostics(WidgetTree& tree, const std::vector<const Widget*>& order,
                                 float s, float screenW, float screenH);

    /// Number of distinct textures resident. Cheap diagnostic; the cache never
    /// evicts, because Interface\ art is small, bounded and reused constantly.
    [[nodiscard]] size_t textureCount() const { return textures_.size(); }

    // What the interface costs, in milliseconds, on the frame just recorded.
    //
    // A PS4 log gave frameMeanMs=84 and renderMeanMs=67, with the world's own
    // timed passes - shadow, reflection, camera, terrain, WMO, M2, godray,
    // post - adding up to 4.7 of it. The other 62 were the widget tree, and
    // nothing measured it: the only evidence was that the game ran at 20-30 FPS
    // with the interface off and 8-11 with it on. Every figure below exists so
    // the next log names the half rather than leaving it to be inferred.
    //
    // Last-frame samples, not window means, matching the world renderer's own
    // lastXxxMs so they can sit on the same [WORLD_PERF] line.
    /// layout() end to end: the sizing pass plus the tree's own solve. Zero on
    /// a frame the gate skipped.
    [[nodiscard]] double lastLayoutMs() const { return lastLayoutMs_; }
    /// The sizing pass alone - tooltips, auto-sized labels, textures taking
    /// their file's dimensions. It grows with how many regions are in the tree
    /// rather than with how many are on screen, which is a different shape of
    /// cost from the solve and has to be visible as one: the reading that sent
    /// the three sweeps into one walk was fxSizingMs=10.804 of a 12.089 ms
    /// layout, beside fxSolveMs=1.249 and fxDrawMs=0.519.
    [[nodiscard]] double lastSizingMs() const { return lastSizingMs_; }
    /// draw() end to end: texture uploads, the diagnostics pass and the draw
    /// loop over everything the tree ordered.
    [[nodiscard]] double lastDrawMs() const { return lastDrawMs_; }
    /// Whether the last layout() returned early because nothing had asked for
    /// one. A reading that is never true on a slow frame means something is
    /// raising the tree's generation on every frame; layout() names the call
    /// site responsible in an [FX_GATE] line a few times a minute when that is
    /// happening, from WidgetTree::dirtySiteReport.
    [[nodiscard]] bool lastLayoutSkipped() const { return lastLayoutSkipped_; }

    /// Whether the art at this path can be read and decoded at all, and how big
    /// it is. Public because it answers a question worth asking from outside:
    /// FrameXML naming art the assets do not carry is a blank where an icon
    /// should be, and nothing else reports it - the draw silently substitutes
    /// nothing and carries on.
    bool artResolves(const std::string& path, float& w, float& h) {
        return textureSize(path, w, h);
    }

private:
    // Safety refresh for legacy bindings that still write Widget fields
    // directly. Dirty generations normally make layout immediate; this keeps
    // those old paths correct without doing a full-tree solve every frame.
    double lastFullLayoutAt_ = -1.0;
    double lastTextDiagnostics_ = -5;
    size_t diagnosticWidgetCount_ = 0;
    double lastLayoutMs_ = 0.0;
    double lastSizingMs_ = 0.0;
    double lastDrawMs_ = 0.0;
    bool   lastLayoutSkipped_ = false;
    /// How the gate has been doing since it was last reported on, and when
    /// that was. A single frame's fxLayoutSkipped says whether one frame was
    /// skipped; what is worth knowing is the proportion, and who is responsible
    /// when it is zero. See the report in layout().
    size_t gateHeld_ = 0;
    size_t gateBroken_ = 0;
    double lastGateReport_ = 0.0;
    /// Descriptor set for an Interface\ path, loading it on first use. Returns
    /// VK_NULL_HANDLE for anything missing, and remembers the failure so a
    /// mistyped path is not re-read every frame.
    VkDescriptorSet texture(const std::string& path, bool add = false);
    /// Already-uploaded texture for a path, without triggering an upload.
    [[nodiscard]] VkDescriptorSet resident(const std::string& path, bool add = false) const;

    /// scale is pixels per interface unit. The rect arrives in pixels, but a
    /// backdrop's insets and edge size are authored in units like everything
    /// else, so they have to make the same trip or a border comes out the
    /// wrong thickness on any display that is not 768 pixels tall.
    /// Everything that has to be sized before the solve can place it, in one
    /// walk. See the definition, which carries what the three separate sweeps
    /// this replaced cost on the console.
    void sizeRegions(WidgetTree& tree);
    /// Give an unsized label the size of the text in it.
    ///
    /// A FontString with no <Size> takes the size of its string, as it does in
    /// WoW. Leaving it at zero lays it out to nothing and draws nothing, so the
    /// text is set, correct, and invisible - the player frame's level number
    /// read text="14" in a rect of 0x0.
    ///
    /// `font` is the fallback face, resolved once for the whole walk; the
    /// label's own is looked up here, and only for one that is really about to
    /// be measured.
    void sizeFontString(WidgetTree& tree, Widget& widget, ImFont* font);
    /// Gives a texture the dimensions of its own image on any axis nothing else
    /// decides. WoW's rule, and FrameXML depends on it - the friends list's
    /// status icon declares one anchor and no size whatsoever.
    void sizeTexture(WidgetTree& tree, Widget& widget);
    /// How big the picture is, without uploading it. No Vulkan context needed:
    /// asking a file its dimensions does not require a GPU, and requiring one
    /// would put this beyond the reach of the headless harness.
    bool textureSize(const std::string& path, float& w, float& h);
    /// The bytes of a texture, with the extension and folder fallbacks applied.
    /// Shared by the upload and the size query so the two cannot look in
    /// different places.
    std::vector<uint8_t> readTextureFile(const std::string& path,
                                         std::string& resolvedOut);

    /// Sizes a tooltip to the lines it holds, before layout runs. A tooltip has
    /// no size until it has something to say.
    void sizeTooltip(WidgetTree& tree, Widget& widget, ImFont* font);

    /// interfaceFaceOrDefault, remembered for the pass in flight.
    ///
    /// That call looks a face up by name, and the lookup builds its own key: it
    /// takes the stem of the path, lower-cases it and returns a std::string, so
    /// a miss on the widget's own face followed by the fallback to frizqt__
    /// comes to four heap allocations. The draw loop asks it once per widget
    /// that paints text, and nearly every one of them names the same face -
    /// the interface has three, and most regions name none at all and take the
    /// default. Several thousand allocations a frame to answer the same
    /// question over and over.
    ///
    /// Held only for the length of one pass and dropped at its start, so a face
    /// registered between frames is picked up: the interface's own typeface is
    /// registered after the first frames have already been drawn, and a font
    /// pointer cached across that would be the fallback face for ever.
    ImFont* faceFor(const std::string& fontFace);
    void forgetFace() { facedValid_ = false; facedFont_ = nullptr; }
    std::string facedName_;
    ImFont* facedFont_ = nullptr;
    bool facedValid_ = false;
    /// Labels whose glyphs are wider than the rect they were given.
    void reportOverflowingText(WidgetTree& tree);
    /// Labels holding a coin amount with a letter on the end of it.
    void reportLetteredAmounts(WidgetTree& tree);

    /// Draw a string that may carry WoW's inline colour markup, as runs.
    /// wrapWidth of zero draws one line, which is what an auto-sized label
    /// and a tooltip row want; a positive one breaks the text to fit.
    void drawMarkupText(ImDrawList* dl, ImFont* font, float size, ImVec2 at,
                        uint32_t fallback, float alpha, const std::string& text,
                        float wrapWidth = 0.0f, bool nonSpaceWrap = false,
                        const char* justifyH = nullptr, bool forceColor = false,
                        WidgetTree* linkSink = nullptr, uint32_t linkOwner = 0);
    /// Screen height and interface scale of the pass in flight, so a link rect
    /// can be filed in the coordinates the click will arrive in.
    float linkScreenH_ = 0.0f;
    float linkScale_ = 1.0f;
    void drawBackdrop(ImDrawList* dl, const Widget& w, float scale,
                      float x0, float y0, float x1, float y1);
    void drawStatusBar(ImDrawList* dl, const Widget& w,
                       float x0, float y0, float x1, float y1);
    void drawSlider(ImDrawList* dl, const Widget& w,
                    float x0, float y0, float x1, float y1);
    /// One of a colour picker's four regions: the hue-saturation wheel, the
    /// brightness bar, or either thumb. None of them has art on disk - the
    /// wheel and the bar are generated from the colour `picker` holds, and the
    /// thumbs are placed by it rather than anchored, since where they belong is
    /// the answer rather than the question.
    void drawColorPicker(ImDrawList* dl, const WidgetTree& tree, const Widget& w,
                         const Widget& picker, float screenH,
                         float x0, float y0, float x1, float y1);
    void drawThumb(ImDrawList* dl, const Widget& w,
                   float x0, float y0, float x1, float y1);
    void drawCooldown(ImDrawList* dl, const Widget& w,
                      float x0, float y0, float x1, float y1);

    pipeline::AssetManager* assets_ = nullptr;
    rendering::VkContext* vkCtx_ = nullptr;
    std::unordered_map<std::string, VkDescriptorSet> textures_;
    /// The cached set for a path, or null when nothing is cached for it -
    /// which is different from a cached kMissing, and both callers care.
    ///
    /// Here rather than through cacheKey because cacheKey returns a string by
    /// value, so the ordinary case copied the path onto the heap purely to
    /// look it up. The draw pass does that twice for every texture on screen,
    /// every frame.
    [[nodiscard]] const VkDescriptorSet* cachedTexture(const std::string& path,
                                                       bool add) const;
    /// Image dimensions by path, including the ones that could not be read -
    /// stored as zero so a missing file is looked for once and not once a frame.
    std::unordered_map<std::string, std::pair<float, float>> textureSizes_;
    std::unordered_map<std::string, TextureContentBounds> microButtonContent_;
    /// Which incarnation of ImGui's backend the cache above belongs to.
    uint32_t uiTextureGenerationSeen_ = 0;
};

} // namespace ui
} // namespace wowee
