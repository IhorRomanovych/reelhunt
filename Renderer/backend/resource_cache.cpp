// Renderer/backend/resource_cache.cpp - GPU meshes, textures and material sets. See resource_cache.h.
#include "Renderer/backend/resource_cache.h"

#include "Renderer/backend/vk_pipeline.h"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace Renderer::Backend {

namespace {

constexpr uint32 kSetsPerPool = 256u;
constexpr uint32 kMaxTextureSize = 8192u; // larger means a corrupt header, not a texture

uint32 mipCount(uint32 w, uint32 h)
{
    uint32 n = 1u;
    while (w > 1u || h > 1u)
    {
        w = std::max(1u, w >> 1);
        h = std::max(1u, h >> 1);
        ++n;
    }
    return n;
}

uint32 packColor(const Math::Vector4& c)
{
    auto u8 = [](float32 v) { return static_cast<uint32>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return u8(c.x) | (u8(c.y) << 8) | (u8(c.z) << 16) | (u8(c.w) << 24);
}

// Fills a mip chain by successive linear blits, leaving every level SHADER_READ_ONLY.
void generateMips(VkCommandBuffer cb, VkImage image, uint32 w, uint32 h, uint32 mips)
{
    for (uint32 i = 1; i < mips; ++i)
    {
        imageBarrier(cb, image, VK_IMAGE_ASPECT_COLOR_BIT,
                     {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT},
                     i - 1u);
        const int32 sw = static_cast<int32>(std::max(1u, w >> (i - 1u))), sh = static_cast<int32>(std::max(1u, h >> (i - 1u)));
        const int32 dw = static_cast<int32>(std::max(1u, w >> i)), dh = static_cast<int32>(std::max(1u, h >> i));
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1u, 0u, 1u};
        blit.srcOffsets[1] = {sw, sh, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0u, 1u};
        blit.dstOffsets[1] = {dw, dh, 1};
        vkCmdBlitImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_LINEAR);
        imageBarrier(cb, image, VK_IMAGE_ASPECT_COLOR_BIT,
                     {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                      VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT},
                     i - 1u);
    }
    imageBarrier(cb, image, VK_IMAGE_ASPECT_COLOR_BIT,
                 {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT},
                 mips - 1u);
}

} // namespace

void ResourceCache::create(const GpuContext& gpu)
{
    gpu_ = &gpu;
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.anisotropyEnable = gpu.anisotropy && gpu.maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    si.maxAnisotropy = std::min(8.0f, gpu.maxAnisotropy);
    si.maxLod = VK_LOD_CLAMP_NONE;
    vkCheck(vkCreateSampler(gpu.device, &si, nullptr, &sampler_), "vkCreateSampler(material)");
    materialLayout_ = createSetLayout(gpu.device, {binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)});

    if (defaultTexture_.create(4u, 4u))
    {
        const std::vector<uint8_t> white(4u * 4u * 4u, 255u);
        defaultTexture_.setPixelsRGBA8(white);
        uploadTexture(defaultTexture_, default_);
    }
}

void ResourceCache::destroy()
{
    if (!gpu_)
        return;
    for (auto& [mesh, gm] : meshes_)
    {
        Backend::destroy(*gpu_, gm.vertices);
        Backend::destroy(*gpu_, gm.indices);
    }
    meshes_.clear();
    for (auto& [handle, gt] : textures_)
        Backend::destroy(*gpu_, gt.image); // the sets go with their pools
    textures_.clear();
    Backend::destroy(*gpu_, default_.image);
    for (VkDescriptorPool p : pools_)
        vkDestroyDescriptorPool(gpu_->device, p, nullptr);
    pools_.clear();
    vkDestroyDescriptorSetLayout(gpu_->device, materialLayout_, nullptr);
    vkDestroySampler(gpu_->device, sampler_, nullptr);
    gpu_ = nullptr;
}

const GpuMesh& ResourceCache::mesh(const Mesh& mesh)
{
    if (auto it = meshes_.find(&mesh); it != meshes_.end())
        return it->second;

    GpuMesh gm;
    gm.skinned = mesh.isSkinned();
    const auto& verts = mesh.geometry().vertices();
    const uint32 stride = gm.skinned ? 56u : 36u;
    std::vector<uint8_t> bytes(verts.size() * stride);
    for (std::size_t i = 0; i < verts.size(); ++i)
    {
        uint8_t* dst = bytes.data() + i * stride;
        const Vertex& v = verts[i];
        const float32 f[8] = {v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y, v.normal.z, v.uv.x, v.uv.y};
        const uint32 color = packColor(v.color);
        std::memcpy(dst, f, sizeof(f));
        std::memcpy(dst + 32u, &color, 4u);
        if (gm.skinned)
        {
            std::memcpy(dst + 36u, mesh.skinIndices()[i].data(), 4u);
            std::memcpy(dst + 40u, mesh.skinWeights()[i].data(), 16u);
        }
    }
    std::vector<uint32> indices;
    indices.reserve(mesh.geometry().triangleCount() * 3u);
    for (const Triangle& t : mesh.geometry().triangles())
        indices.insert(indices.end(), {t.a, t.b, t.c});
    gm.indexCount = static_cast<uint32>(indices.size());
    gm.vertices = createBuffer(*gpu_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, bytes.size(), bytes.data());
    gm.indices = createBuffer(*gpu_, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.size() * 4u, indices.data());
    return meshes_.emplace(&mesh, gm).first->second;
}

VkDescriptorSet ResourceCache::allocateSet(VkDescriptorPool& poolOut)
{
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &materialLayout_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!pools_.empty())
    {
        ai.descriptorPool = pools_.back();
        if (vkAllocateDescriptorSets(gpu_->device, &ai, &set) == VK_SUCCESS)
        {
            poolOut = pools_.back();
            return set;
        }
    }
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSetsPerPool};
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.maxSets = kSetsPerPool;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &size;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    vkCheck(vkCreateDescriptorPool(gpu_->device, &pi, nullptr, &pool), "vkCreateDescriptorPool(material)");
    pools_.push_back(pool);
    ai.descriptorPool = pool;
    vkCheck(vkAllocateDescriptorSets(gpu_->device, &ai, &set), "vkAllocateDescriptorSets(material)");
    poolOut = pool;
    return set;
}

bool ResourceCache::uploadTexture(const Texture& tex, GpuTexture& gt)
{
    const uint32 w = tex.width(), h = tex.height();
    if (w == 0u || h == 0u || w > kMaxTextureSize || h > kMaxTextureSize ||
        tex.rgba8().size() != static_cast<std::size_t>(w) * h * 4u)
        return false;
    const uint32 mips = mipCount(w, h);
    gt.image = createImage(*gpu_, {w, h, VK_FORMAT_R8G8B8A8_UNORM,
                                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                   VK_IMAGE_ASPECT_COLOR_BIT, 1u, mips, VK_IMAGE_VIEW_TYPE_2D});
    Buffer staging = createBuffer(*gpu_, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, tex.rgba8().size(), tex.rgba8().data());
    {
        OneShot os(*gpu_);
        imageBarrier(os.cmd(), gt.image.image, VK_IMAGE_ASPECT_COLOR_BIT,
                     {VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT},
                     0u, mips);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
        region.imageExtent = {w, h, 1u};
        vkCmdCopyBufferToImage(os.cmd(), staging.buffer, gt.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        generateMips(os.cmd(), gt.image.image, w, h, mips);
    }
    Backend::destroy(*gpu_, staging);

    gt.set = allocateSet(gt.pool);
    const VkDescriptorImageInfo info{sampler_, gt.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = gt.set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(gpu_->device, 1, &write, 0, nullptr);
    return true;
}

GpuTexture& ResourceCache::texture(const Texture& tex)
{
    if (auto it = textures_.find(tex.handle()); it != textures_.end())
        return it->second;
    GpuTexture gt;
    if (!uploadTexture(tex, gt))
        gt = GpuTexture{}; // an empty entry: draws with the default texture, never retried
    return textures_.emplace(tex.handle(), gt).first->second;
}

VkDescriptorSet ResourceCache::materialSet(const Texture* tex)
{
    if (tex && tex->valid())
        if (const GpuTexture& gt = texture(*tex); gt.set)
            return gt.set;
    return default_.set;
}

VkImageView ResourceCache::updateTexture(const Texture& tex)
{
    // Video frames keep their size, so the image is rewritten in place.
    auto it = textures_.find(tex.handle());
    if (it == textures_.end() || !it->second.image.image)
        return texture(tex).image.view ? texture(tex).image.view : default_.image.view;
    GpuTexture& gt = it->second;
    if (tex.rgba8().size() != static_cast<std::size_t>(gt.image.desc.width) * gt.image.desc.height * 4u)
        return gt.image.view;
    Buffer staging = createBuffer(*gpu_, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, tex.rgba8().size(), tex.rgba8().data());
    {
        OneShot os(*gpu_);
        const uint32 mips = gt.image.desc.mips;
        imageBarrier(os.cmd(), gt.image.image, VK_IMAGE_ASPECT_COLOR_BIT,
                     {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT},
                     0u, mips);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
        region.imageExtent = {gt.image.desc.width, gt.image.desc.height, 1u};
        vkCmdCopyBufferToImage(os.cmd(), staging.buffer, gt.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        generateMips(os.cmd(), gt.image.image, gt.image.desc.width, gt.image.desc.height, mips);
    }
    Backend::destroy(*gpu_, staging);
    return gt.image.view;
}

void ResourceCache::release(GpuTexture& gt)
{
    if (gt.set && gt.pool)
        vkFreeDescriptorSets(gpu_->device, gt.pool, 1, &gt.set);
    Backend::destroy(*gpu_, gt.image);
    gt = GpuTexture{};
}

void ResourceCache::removeMesh(const Mesh* mesh)
{
    auto it = meshes_.find(mesh);
    if (it == meshes_.end())
        return;
    Backend::destroy(*gpu_, it->second.vertices);
    Backend::destroy(*gpu_, it->second.indices);
    meshes_.erase(it);
}

void ResourceCache::removeTexture(uint32 handle)
{
    auto it = textures_.find(handle);
    if (it == textures_.end())
        return;
    release(it->second);
    textures_.erase(it);
}

} // namespace Renderer::Backend
