#pragma once

// A live head-and-shoulders view of a unit, for the interface's portraits.
//
// WoW's portraits are not pictures on disk: they are the character itself,
// rendered small. The same offscreen pass the character-select screen uses
// already frames a face when zoomed all the way in, so this is that pass kept
// running while in the world, at a size a portrait needs.
//
// Four units: the player, the target, the pet and the focus. They differ only
// in whose appearance is loaded - a player from race, appearance bytes and
// what they are visibly wearing, anything else from the model its display id
// names. The party frames want the same thing and are left out on cost: each
// view uses its requested target size. Face sources are square and rendered
// at most 30 times per second; unchanged images are reused between updates.

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace wowee {
namespace game { class GameHandler; struct EquipmentItem; }
namespace pipeline { class AssetManager; }
namespace rendering { class CharacterPreview; class Renderer; }

namespace ui {

/// What a face portrait's camera aims at, and from how far.
///
/// The preview looks along one axis at a model standing at the origin and
/// turned to face it, so a framing is three numbers: the height on the model
/// the camera is aimed at, how far in front of it the camera sits, and the
/// vertical field of view. What decides them is the whole of this fault.
struct PortraitFraming {
    float focusZ = 0.0f;
    float distance = 0.0f;
    float fovDegrees = 30.0f;
    /// Which of the model's measurements answered. Reported so a portrait that
    /// comes out wrong says which source it was framed from rather than
    /// leaving three candidates to be told apart by eye.
    enum class Source : std::uint8_t { PortraitCamera, HeadAttachment, Bounds };
    Source source = Source::Bounds;
};

/// The model's own measurements. Every one of them is in the M2 the client
/// already reads, and none of them is a number anybody here chose per race.
struct PortraitModel {
    /// The model's vertical extent, which is what framing has used until now.
    float boundMinZ = 0.0f;
    float boundMaxZ = 2.0f;
    /// The Helm attachment (M2 attachment id 11): its pivot height, and how
    /// far in front of the model's centre it sits along the axis the portrait
    /// looks down. Negative `headZ` means the model declares none.
    ///
    /// This is the head, per race, in the artist's own numbers - it is where
    /// the client hangs a helmet, so every character model has one and it is
    /// correct by construction. A fraction of the bounding box is not: an orc
    /// male is hunched, and his shoulders and the hump of his back reach
    /// higher than his head does, so the top of the box is not the top of the
    /// head and a fixed fraction of it lands on his chest.
    float headZ = -1.0f;
    float headForward = 0.0f;
    /// The M2's own portrait camera - camera type 0, which the loader already
    /// reads and keeps in M2Model::cameras. Where a model carries one this is
    /// the authored answer and nothing else needs consulting. `fov` is what
    /// the M2 stores: the diagonal angle, in radians.
    bool  hasPortraitCamera = false;
    float cameraTargetZ = 0.0f;
    float cameraDistance = 0.0f;
    float cameraFovRadians = 0.0f;
};

/// Frame a face portrait from what the model says about itself.
///
/// Three sources, in the order of how much they know. The model's own portrait
/// camera is the artist's answer and is taken as it stands. Failing that the
/// head attachment says where the head is, and the portrait is a window about
/// two head-heights tall centred a little below the crown - head and
/// shoulders, which is what a portrait is. Only a model that declares neither
/// falls back to the fraction of the bounding box this used to apply to
/// everything, which is right for a figure whose head is the top of it and
/// wrong for every race whose build is not a human's.
///
/// Pure, so the rule can be stated against a race's measurements rather than
/// against a screenshot.
[[nodiscard]] inline PortraitFraming portraitFraming(const PortraitModel& m) {
    PortraitFraming out;
    const auto finite = [](float v) { return v == v && v > -1e18f && v < 1e18f; };
    const bool validBounds=finite(m.boundMinZ) && finite(m.boundMaxZ) &&
        finite(m.boundMaxZ-m.boundMinZ) && m.boundMaxZ>m.boundMinZ;
    const float minZ=validBounds?m.boundMinZ:0.f;
    const float maxZ=validBounds?m.boundMaxZ:2.f;
    const float height=(maxZ-minZ)>0.1f?(maxZ-minZ):0.1f;

    if (m.hasPortraitCamera && finite(m.cameraTargetZ) && finite(m.cameraDistance) &&
        finite(m.cameraFovRadians) && m.cameraDistance > 0.0f && m.cameraFovRadians > 0.001f && m.cameraFovRadians < 3.14059265f) {
        out.source = PortraitFraming::Source::PortraitCamera;
        out.focusZ = m.cameraTargetZ;
        out.distance = m.cameraDistance;
        // The M2 stores the diagonal angle and a camera wants the vertical
        // one. A portrait's target is square - UnitPortrait::ensure squares it
        // for exactly this framing - and on a square the diagonal is the
        // vertical scaled by root two, so that is the conversion.
        const float halfDiagonal = m.cameraFovRadians * 0.5f;
        const float halfVertical = std::atan(std::tan(halfDiagonal) / 1.41421356f);
        out.fovDegrees = halfVertical * 2.0f * 57.2957795f;
        return out;
    }

    if (finite(m.headZ) && validBounds && m.headZ >= minZ && m.headZ < maxZ) {
        out.source = PortraitFraming::Source::HeadAttachment;
        // What stands above the helm pivot is the head - and, on a character
        // model, whatever else the file carries up there. The bounds are taken
        // over every vertex in the M2, and a character M2 holds every hairstyle
        // and every shoulder geoset the race has whether this character wears
        // them or not, so the top of the box is the tallest hair in the file
        // and not the top of this head. That is also what a hunched race's back
        // and shoulders do to it.
        //
        // So the head is capped against the body under the pivot: nothing has a
        // head a third as tall as the rest of it, and with that cap geometry
        // nobody selected can no longer drag the aim off the face. Erring wide
        // is the right way for a fallback to be wrong - the face stays centred
        // and stays in the picture, where a fraction of the box is not aimed at
        // it at all.
        const float above = maxZ - m.headZ;
        const float cap = (m.headZ - minZ) * 0.30f;
        const float head = (cap > 0.0f && above > cap) ? cap : above;
        out.focusZ = m.headZ + head * 0.35f;
        const float window = head * 2.0f;
        // window = 2 * distance * tan(fov/2), inverted for the distance. The
        // head leans toward the camera on hunched races. Add that forward
        // offset to the camera's distance from the model origin so its actual
        // distance from the face still fits the requested window.
        const float halfFov = out.fovDegrees * 0.5f / 57.2957795f;
        out.distance = window * 0.5f / std::tan(halfFov) + (finite(m.headForward)?m.headForward:0.f);
        if (!(out.distance > 0.35f)) out.distance = 0.35f;
        return out;
    }

    out.source = PortraitFraming::Source::Bounds;
    out.focusZ = minZ + height * 0.82f;
    out.distance = height * 0.70f > 1.15f ? height * 0.70f : 1.15f;
    return out;
}

class UnitPortrait {
public:
    /// How much of the character to show. A portrait is the face in a circle;
    /// the paperdoll wants the whole figure in a tall rectangle. The offscreen
    /// pass is the same either way - only the framing differs, which is why
    /// this is one class and not two.
    enum class Framing { Face, FullBody };

    UnitPortrait();
    ~UnitPortrait();

    /// Builds the offscreen view on first use and keeps it in step with the
    /// player's appearance and equipment afterwards. Safe to call every frame;
    /// it reloads the model only when something about it actually changed.
    void update(game::GameHandler& gameHandler, pipeline::AssetManager* assets,
                rendering::Renderer* renderer, float deltaTime);

    /// Answers whether there is actually a model to show. A load can fail -
    /// a race this client has no model path for, an M2 the install is missing
    /// - and the view then keeps whatever it had, which for a frame that has
    /// just changed unit means the previous one's face. The caller blanks the
    /// texture instead.
    ///
    /// Show another player, from the appearance the world already reads for
    /// them: race, gender, the packed appearance bytes and facial features.
    ///
    /// Dressed in what they are visibly wearing, which for a portrait framed
    /// on the head is the helm and the shoulders - the two pieces that change
    /// a face most. An empty list leaves the model as it is rather than
    /// stripping it, because "nothing known yet" and "wearing nothing" arrive
    /// looking the same and only one of them should undress anybody.
    bool updatePlayer(uint8_t race, uint8_t gender, uint32_t appearanceBytes,
                      uint8_t facialFeatures,
                      const std::vector<game::EquipmentItem>& equipment,
                      pipeline::AssetManager* assets,
                      rendering::Renderer* renderer, float deltaTime);

    /// Show a creature instead, by the model its display id names.
    ///
    /// Kept apart from update() rather than folded into it: a player is built
    /// from race, appearance bytes and equipment, and a creature is a path and
    /// nothing else. The two share the offscreen view and the framing and
    /// agree on nothing else, so one function taking both would be two
    /// functions sharing a name.
    ///
    /// Reloads when the path or display skin bindings change.
    bool updateCreature(const std::string& m2Path,
                        const std::vector<std::pair<uint32_t, std::string>>& skins,
                        pipeline::AssetManager* assets,
                        rendering::Renderer* renderer, float deltaTime);

    /// A pre-composited skin to put on the character once it is built,
    /// replacing the one made from CharSections.
    ///
    /// CreateDisplayInfoExtra carries one for nearly every humanoid NPC, and
    /// it is the whole appearance already baked - skin, face, hair and the
    /// armour they wear. Set before the update that should use it; it is part
    /// of what decides a rebuild, so changing it is enough to apply it.
    void setBakedSkin(const std::string& path) { pendingBake_ = path; }

    /// Set before the first update, since framing is applied when the model
    /// loads and the model loads once.
    void setFraming(Framing framing) { framing_ = framing; }

    /// How big the offscreen image is, in pixels. Set before the first update,
    /// because the view is built once and keeps the size it was built at.
    ///
    /// Worth setting: this is a full character pass every frame, and the
    /// paperdoll's 640x800 is enormously oversized for a face drawn into a
    /// circle fifty pixels across.
    void setTargetSize(int width, int height) {
        targetWidth_ = width;
        targetHeight_ = height;
    }

    /// Turn the figure by this much, in radians. What the paperdoll's rotate
    /// buttons drive.
    void rotate(float yawDelta);

    /// The rendered portrait, or zero until the first composite has run. The
    /// value is a VkDescriptorSet, carried as an integer so this header does
    /// not drag Vulkan into the widget tree.
    [[nodiscard]] uint64_t textureId() const;

    void shutdown(rendering::Renderer* renderer);

private:
    bool ensure(pipeline::AssetManager*,rendering::Renderer*,float dt);
    void advance(float dt,bool changed);
    void frame();
    void failed();
    bool valid_=false,loadedFemaleModel_=false;
    float retryRemaining_=0,pendingDelta_=0;
    std::vector<std::pair<uint32_t,std::string>> loadedSkins_;
    std::unique_ptr<rendering::CharacterPreview> preview_;
    bool initialized_ = false;
    Framing framing_ = Framing::Face;
    int targetWidth_ = 640;
    int targetHeight_ = 800;
    bool registered_ = false;

    // What the loaded model was built from, so a reload happens only on a real
    // change rather than every frame.
    /// The creature model currently loaded, empty while a player is loaded.
    /// Also the guard against reloading: a portrait rebuilt every frame looks
    /// like one that flickers, and the two are indistinguishable from outside.
    std::string loadedCreaturePath_;
    /// The bake asked for, and the one already on the model.
    std::string pendingBake_;
    std::string loadedBake_;

    uint64_t loadedGuid_ = 0;
    /// Race and gender as well, because another player is identified by these
    /// rather than by a guid that this only ever sees one of at a time.
    uint8_t  loadedRace_ = 0xFF;
    uint8_t  loadedGender_ = 0xFF;
    uint32_t loadedAppearance_ = 0;
    uint8_t  loadedFacialFeatures_ = 0;
    // uint64_t, not size_t: the hash is sixty-four bits and storing it in a
    // pointer-sized field would truncate on a 32-bit build, where two outfits
    // sharing the low half would stop the portrait redrawing.
    uint64_t loadedEquipHash_ = 0;
};

} // namespace ui
} // namespace wowee
