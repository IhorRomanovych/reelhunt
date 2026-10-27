// Renderer/backend/resource_cache.h - GPU copies of meshes and textures, created on first use
// and keyed by Mesh pointer / Texture handle; the material descriptor set of each texture.
#pragma once
#include "engine_types.h"

#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

#include "Renderer/backend/vk_common.h"
#include "Renderer/mesh.h"
#include "Renderer/texture.h"

namespace Renderer::Backend {

struct GpuMesh {
    Buffer vertices;
    Buffer indices;
    uint32 indexCount = 0u;
    bool skinned = false;
};

struct GpuTexture {
    Image image;
    VkDescriptorSet set = VK_NULL_HANDLE; // material set: combined image sampler @0
    VkDescriptorPool pool = VK_NULL_HANDLE;
};

class ResourceCache {
public:
    void create(const GpuContext& gpu);
    void destroy();

    // Layout of a material set: one combined image sampler at binding 0 (fragment stage).
    [[nodiscard]] VkDescriptorSetLayout materialLayout() const noexcept { return materialLayout_; }
    [[nodiscard]] VkSampler sampler() const noexcept { return sampler_; }
    [[nodiscard]] VkImageView defaultView() const noexcept { return default_.image.view; }

    const GpuMesh& mesh(const Mesh& mesh);
    // The texture's material set; the white default texture for null/invalid/empty ones.
    VkDescriptorSet materialSet(const Texture* tex);
    // Re-uploads `tex`'s pixels (a video frame) and returns its image view.
    VkImageView updateTexture(const Texture& tex);

    void removeMesh(const Mesh* mesh);
    void removeTexture(uint32 handle);

private:
    GpuTexture& texture(const Texture& tex);
    bool uploadTexture(const Texture& tex, GpuTexture& gt);
    VkDescriptorSet allocateSet(VkDescriptorPool& poolOut);
    void release(GpuTexture& gt);

    const GpuContext* gpu_ = nullptr;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialLayout_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> pools_; // a new pool is added when the last one is full
    std::unordered_map<const Mesh*, GpuMesh> meshes_;
    std::unordered_map<uint32, GpuTexture> textures_;
    Texture defaultTexture_;
    GpuTexture default_;
};

} // namespace Renderer::Backend
