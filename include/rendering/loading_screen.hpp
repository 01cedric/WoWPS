#pragma once

#include <vulkan/vulkan.h>
#include "pipeline/client_transport_presentation.hpp"
#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;

namespace wowee {
namespace pipeline { class AssetManager; }
namespace rendering {

class VkContext;

// World-transition UI only. Login and character selection own separate 3D
// scenes; neither uses a loading painting as its background.
class LoadingScreen {
public:
    LoadingScreen() = default;
    ~LoadingScreen() noexcept;
    LoadingScreen(const LoadingScreen&) = delete;
    LoadingScreen& operator=(const LoadingScreen&) = delete;

    bool initialize();
    void shutdown() noexcept;
    bool setBackgroundFromAssets(pipeline::AssetManager* assets, uint32_t mapId);
    bool setTransportRouteFromAssets(pipeline::AssetManager* assets, uint32_t pathId, uint32_t legIndex);
    // Standalone between application frames. Within an existing ImGui frame
    // this draws an overlay and lets the caller submit that frame normally.
    void render();
    void renderOverlay();
    void resetProgress();
    void setProgress(float progress);
    void setStatus(const std::string& status);
    void setZoneName(const std::string& name) { zoneName = name; }
    void setVkContext(VkContext* ctx) { vkCtx = ctx; }
    void setSDLWindow(SDL_Window* win) { sdlWindow = win; }

private:
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
    };
    bool createDescriptorResources();
    bool loadAssetTexture(pipeline::AssetManager& assets, const std::string& path, Texture& out);
    bool uploadTexture(const unsigned char* pixels, uint32_t width, uint32_t height, Texture& out);
    static void destroyTexture(VkDevice device, const Texture& texture);

    VkContext* vkCtx = nullptr;
    SDL_Window* sdlWindow = nullptr;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE; // VkContext sampler cache owns it.
    Texture background, border, fill;
    std::vector<Texture> retiredTextures;
    pipeline::ClientTravelSpline travelRoute;
    bool routeArtworkAvailable = false;
    bool backgroundWide = false;
    bool ready = false;
    float loadProgress = 0.0f;
    std::string statusText;
    std::string zoneName;
};

} // namespace rendering
} // namespace wowee
