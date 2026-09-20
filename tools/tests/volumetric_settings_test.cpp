#include "ui/settings_schema.hpp"
#include "ui/graphics_defaults.hpp"
#include "ui/graphics_choices.hpp"
#include "ui/graphics_presets.hpp"
#include "rendering/render_setting_bridge.hpp"
#include "rendering/volumetric_fog_intensity.hpp"
#include "rendering/bloom_settings.hpp"
#include <limits>
#include "rendering/ps4_scene_extent.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    using namespace wowee;
    std::size_t count = 0;
    const auto* schema = ui::clientSettingsSchema(count);
    const ui::SettingDesc* volume = nullptr;
    const ui::SettingDesc* aa = nullptr;
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::strcmp(schema[i].key, "volumetricquality")) {
            require(volume == nullptr, "only one volumetric control");
            volume = &schema[i];
        }
        if (!std::strcmp(schema[i].key, "antialiasing")) aa = &schema[i];
    }
    require(volume && aa, "public settings expose sunlight and multisampling");
    require(volume->kind == ui::SettingKind::Enum && volume->minValue == 0 &&
            volume->maxValue == 2 && volume->defaultValue == rendering::kDefaultVolumetricQuality,
            "rays share the platform default and bounded three-level quality");
    require(std::strcmp(volume->choices, "Off|Low|High") == 0,
            "saved quality indices retain their meanings");
    require(aa->defaultValue == ui::kDefaultAntiAliasing,
            "restore defaults and initial settings select the same multisampling");
#ifdef WOWEE_PS4
    require(rendering::kDefaultVolumetricQuality == 1, "new PS4 profiles enable Low shafts");
    for (const auto& preset : ui::kGraphicsPresets) {
        require(preset.antiAliasing == 0, "console quality presets preserve scene resolution and rays");
        require(preset.viewDistance >= rendering::ps4budget::kMinViewDistance &&
                preset.viewDistance <= rendering::ps4budget::kMaxViewDistance,
                "console presets stay within the real streaming range");
    }
    require(ui::kDefaultAntiAliasing == 0 &&
            ui::msaaSamplesForChoice(static_cast<int>(aa->defaultValue)) == VK_SAMPLE_COUNT_1_BIT,
            "fresh PS4 profile permits internal resolution and volumetrics");
#else
    require(ui::kDefaultAntiAliasing == 1 &&
            ui::msaaSamplesForChoice(static_cast<int>(aa->defaultValue)) == VK_SAMPLE_COUNT_2_BIT,
            "desktop multisampling default remains 2x");
#endif
    // Exercise the production choice decoder with retained, non-default values.
    require(ui::msaaSamplesForChoice(2) == VK_SAMPLE_COUNT_4_BIT &&
            ui::msaaSamplesForChoice(3) == VK_SAMPLE_COUNT_8_BIT,
            "saved explicit multisampling choices are not rewritten");
    const auto scene = rendering::fitPs4SceneExtent({1920, 1080}, 720);
    require(scene.width == 1280 && scene.height == 720,
            "720p world target keeps the native 16:9 aspect");
    const auto native = rendering::fitPs4SceneExtent({1920, 1080}, 1080);
    require(native.width == 1920 && native.height == 1080,
            "1080p selection really renders native world pixels");

    auto& sinks = rendering::renderSettingSinks();
    require(!sinks.setVolumetricQuality, "no dangling sink before renderer startup");
    int applied = -1;
    sinks.setVolumetricQuality = [&](int value) { applied = value; };
    for (int quality = 0; quality <= 2; ++quality) {
        // Numeric text is the actual shared format consumed by CVar/settings
        // serialization. This is a format contract, not a full Lua/save test.
        const auto text = ui::settingNumberText(quality);
        std::istringstream stored(text);
        int restored = -1;
        stored >> restored;
        require(!stored.fail() && restored == quality, "quality text round trip");
        rendering::renderSettingSinks().setVolumetricQuality(restored);
        require(applied == quality, "public bridge delivers every quality including Off");
    }
    const auto find = [&](const char* key) -> const ui::SettingDesc* {
        for (std::size_t i = 0; i < count; ++i) if (!std::strcmp(schema[i].key,key)) return &schema[i];
        return nullptr;
    };
    for (const char* key : {"volumetricraysenabled", "volumetricfogenabled", "bloomenabled"}) {
        const auto* d = find(key);
        require(d && d->kind == ui::SettingKind::Bool && d->defaultValue == 1,
                "independent enabled flags with retained intensity");
    }
    for (const char* key : {"volumetricfogintensity", "bloomintensity"}) {
        const auto* d = find(key);
        require(d && d->minValue == 0 && d->maxValue == 1 && d->step == .05f,
                "bounded density/glow schema");
        require(std::string(d->category) == "Lighting", "lighting controls have their own reachable page");
        require(!ui::settingEnabled(*d, [](const std::string&){return "0";}), "toggle disables its slider");
        require(ui::settingEnabled(*d, [](const std::string&){return "1";}), "toggle enables its slider");
    }
    require(find("shadowquality") && find("shadowquality")->minValue == 0 && std::string(find("shadowquality")->choices).starts_with("Off|"),
            "existing shadow toggle remains available");
    require(rendering::clampVolumetricFogIntensity(-1)==0 && rendering::clampVolumetricFogIntensity(2)==1,
            "fog range clamps");
    require(rendering::clampVolumetricFogIntensity(std::numeric_limits<float>::infinity())==.35f &&
            rendering::clampBloomIntensity(std::numeric_limits<float>::quiet_NaN())==.25f, "finite defaults");
    float density=.65f, glow=.45f; bool rays=true,fog=true,bloom=true;
    sinks.setVolumetricRaysEnabled=[&](bool v){rays=v;};
    sinks.setVolumetricFogEnabled=[&](bool v){fog=v;};
    sinks.setBloomEnabled=[&](bool v){bloom=v;};
    sinks.setVolumetricFogIntensity=[&](float v){density=rendering::clampVolumetricFogIntensity(v);};
    sinks.setBloomIntensity=[&](float v){glow=rendering::clampBloomIntensity(v);};
    sinks.setVolumetricRaysEnabled(false);
    require(!rays && fog && bloom && density==.65f && glow==.45f, "ray Off independent");
    sinks.setVolumetricFogEnabled(false);sinks.setBloomEnabled(false);
    require(!fog && !bloom && density==.65f && glow==.45f, "Off preserves intensities");
    sinks.setVolumetricFogEnabled(true);sinks.setBloomEnabled(true);
    require(fog && bloom && !rays, "independent re-enable");
    sinks.setVolumetricFogIntensity(std::stof(ui::settingNumberText(density)));
    sinks.setBloomIntensity(std::stof(ui::settingNumberText(glow)));
    require(density==.65f && glow==.45f, "fractional serialized roundtrip");
    std::puts("PASS the reference independent ray/fog/bloom schema, defaults, finite bounds, bridge lifetime and preserved intensities");
    sinks = {};
    require(!rendering::renderSettingSinks().setVolumetricQuality,
            "renderer shutdown clears sunlight callback");
    std::puts("PASS: sunlight schema, platform defaults, retained MSAA indices, scene extent, bridge lifecycle");
}
