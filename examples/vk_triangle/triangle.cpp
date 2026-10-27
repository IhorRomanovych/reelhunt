#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>

namespace {

[[noreturn]] void fail(VkResult r, const std::string& ctx) {
    std::cerr << "[FAIL] " << ctx << " (" << static_cast<int>(r) << ")\n";
    exit(1);
}

std::string exeDir() {
    char buf[4096];
    int n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n <= 0) return {};
    std::string path(buf, n);
    size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) return "";
    return path.substr(0, pos + 1);
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "[FAIL] open " << path << "\n";
        exit(1);
    }
    f.seekg(0, std::ios::end);
    std::size_t n = static_cast<std::size_t>(f.tellg());
    std::vector<uint8_t> buf(n);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(n));
    return buf;
}

bool formatSupportsColorAttachment(VkPhysicalDevice phys, VkFormat f) {
    VkFormatProperties fp{};
    (void)vkGetPhysicalDeviceFormatProperties(phys, f, &fp);
    return (fp.linearTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0 ||
           (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
}

} // namespace

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }
    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        std::cerr << "SDL_Vulkan_LoadLibrary: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_Window* window = nullptr;
    window = SDL_CreateWindow("Reelhunt",
            800, 600,
            SDL_WINDOW_VULKAN);
    if (!window) {
        std::cerr << "SDL_CreateWindow: " << SDL_GetError() << "\n";
        return 1;
    }

    int iw = 800, ih = 600;
    SDL_GetWindowSize(window, &iw, &ih);
    uint32_t w = static_cast<uint32_t>(iw);
    uint32_t h = static_cast<uint32_t>(ih);
    std::cout << "window " << w << "x" << h << "\n";

    // Get the instance extensions required for the Vulkan surface.
    uint32_t extCount = 0;
    const char *const * extsRaw = SDL_Vulkan_GetInstanceExtensions(&extCount);
    std::vector<const char*> exts;
    if (extsRaw) {
        exts.resize(extCount);
        for (uint32_t i = 0u; i < extCount; ++i) {
            exts[i] = extsRaw[i];
        }
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Reelhunt";
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo instCI{};
    instCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instCI.pApplicationInfo = &appInfo;
    instCI.enabledExtensionCount = static_cast<uint32_t>(extCount);
    instCI.ppEnabledExtensionNames = extCount ? exts.data() : nullptr;

    VkInstance instance{};
    fail(vkCreateInstance(&instCI, nullptr, &instance), "vkCreateInstance");

    VkSurfaceKHR surface{};
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
        std::cerr << "SDL_Vulkan_CreateSurface: " << SDL_GetError() << "\n";
        return 1;
    }

    uint32_t pdCount = 0;
    fail(vkEnumeratePhysicalDevices(nullptr, &pdCount, nullptr), "vkEnumeratePhysicalDevices");
    VkPhysicalDevice physical{};
    fail(vkEnumeratePhysicalDevices(nullptr, &pdCount, &physical), "vkEnumeratePhysicalDevices2");

    uint32_t qCount = 0;
    (void)vkGetPhysicalDeviceQueueFamilyProperties(physical, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qCount);
    (void)vkGetPhysicalDeviceQueueFamilyProperties(physical, &qCount, qf.data());
    uint32_t gfxFamily = 0;
    for (uint32_t i = 0; i < qCount; ++i) {
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            gfxFamily = i;
            break;
        }
    }

    uint32_t devExtCount = 0;
    fail(vkEnumerateDeviceExtensionProperties(physical, nullptr, &devExtCount, nullptr), "devExtEnum1");
    std::vector<VkExtensionProperties> devExt(devExtCount);
    fail(vkEnumerateDeviceExtensionProperties(physical, nullptr, &devExtCount, devExt.data()), "devExtEnum2");
    std::vector<const char*> enabledExt;
    for (auto& e : devExt) {
        if (std::strcmp(e.extensionName, "VK_KHR_swapchain") == 0) {
            enabledExt.push_back("VK_KHR_swapchain");
        }
    }

    float prio[1] = {1.0f};
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = gfxFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = prio;

    VkDeviceCreateInfo devCI{};
    devCI.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devCI.pQueueCreateInfos = &qci;
    devCI.queueCreateInfoCount = 1;
    devCI.ppEnabledExtensionNames = enabledExt.data();
    devCI.enabledExtensionCount = static_cast<uint32_t>(enabledExt.size());

    VkDevice device{};
    fail(vkCreateDevice(physical, &devCI, nullptr, &device), "vkCreateDevice");
    VkQueue queue{};
    vkGetDeviceQueue(device, gfxFamily, 0, &queue);

    VkFormat fmt = VK_FORMAT_B8G8R8A8_UNORM;
    if (!formatSupportsColorAttachment(physical, fmt)) {
        fmt = VK_FORMAT_R8G8B8A8_UNORM;
    }
    if (!formatSupportsColorAttachment(physical, fmt)) {
        fmt = VK_FORMAT_B8G8R8_UNORM;
    }
    std::cout << "format = " << static_cast<int>(fmt) << "\n";

    uint32_t* familyIdx = &gfxFamily;
    VkSwapchainCreateInfoKHR swapCI{};
    swapCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapCI.surface = surface;
    swapCI.minImageCount = 2;
    swapCI.imageFormat = fmt;
    swapCI.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapCI.imageExtent.width = w;
    swapCI.imageExtent.height = h;
    swapCI.imageArrayLayers = 1;
    swapCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapCI.queueFamilyIndexCount = 1;
    swapCI.pQueueFamilyIndices = familyIdx;
    swapCI.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapCI.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapCI.clipped = 1;

    VkSwapchainKHR swapchain{};
    fail(vkCreateSwapchainKHR(device, &swapCI, nullptr, &swapchain), "vkCreateSwapchainKHR");

    uint32_t imgCount = 0;
    fail(vkGetSwapchainImagesKHR(device, swapchain, &imgCount, nullptr), "getImages1");
    std::vector<VkImage> images(imgCount);
    fail(vkGetSwapchainImagesKHR(device, swapchain, &imgCount, images.data()), "getImages2");

    std::vector<VkImageView> views(imgCount);
    for (uint32_t i = 0; i < imgCount; ++i) {
        VkImageViewCreateInfo vCI{};
        vCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vCI.image = images[i];
        vCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vCI.format = fmt;
        vCI.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        vCI.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        vCI.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        vCI.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        vCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vCI.subresourceRange.baseMipLevel = 0;
        vCI.subresourceRange.levelCount = 1;
        vCI.subresourceRange.baseArrayLayer = 0;
        vCI.subresourceRange.layerCount = 1;
        fail(vkCreateImageView(device, &vCI, nullptr, &views[i]), "vkCreateImageView");
    }

    VkAttachmentDescription att{};
    att.format = fmt;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference attRef{};
    attRef.attachment = 0;
    attRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &attRef;

    VkRenderPassCreateInfo rpCI{};
    rpCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpCI.attachmentCount = 1;
    rpCI.pAttachments = &att;
    rpCI.subpassCount = 1;
    rpCI.pSubpasses = &subpass;
    VkRenderPass renderPass{};
    fail(vkCreateRenderPass(device, &rpCI, nullptr, &renderPass), "vkCreateRenderPass");

    std::vector<VkFramebuffer> framebuffers(imgCount);
    for (uint32_t i = 0; i < imgCount; ++i) {
        VkFramebufferCreateInfo fbCI{};
        fbCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCI.renderPass = renderPass;
        fbCI.attachmentCount = 1;
        fbCI.pAttachments = &views[i];
        fbCI.width = w;
        fbCI.height = h;
        fbCI.layers = 1;
        fail(vkCreateFramebuffer(device, &fbCI, nullptr, &framebuffers[i]), "vkCreateFramebuffer");
    }

    const float verts[] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
    };
    std::vector<float> vdata(std::begin(verts), std::end(verts));

    VkBufferCreateInfo vbCI{};
    vbCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vbCI.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbCI.size = vdata.size() * sizeof(float);

    VkBuffer vertexBuffer{};
    fail(vkCreateBuffer(device, &vbCI, nullptr, &vertexBuffer), "vkCreateBuffer");

    VkMemoryRequirements memReq{};
    (void)vkGetBufferMemoryRequirements(device, vertexBuffer, &memReq);
    VkPhysicalDeviceMemoryProperties memProps{};
    (void)vkGetPhysicalDeviceMemoryProperties(physical, &memProps);
    uint32_t memType = 0;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if (memProps.memoryTypes[i].propertyFlags & memReq.memoryTypeBits) {
            memType = i;
            break;
        }
    }

    VkMemoryAllocateInfo mAI{};
    mAI.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mAI.allocationSize = memReq.size;
    mAI.memoryTypeIndex = memType;
    VkDeviceMemory vbufMem{};
    fail(vkAllocateMemory(device, &mAI, nullptr, &vbufMem), "vkAllocateMemory");
    fail(vkBindBufferMemory(device, vertexBuffer, vbufMem, 0), "vkBindBufferMemory");

    std::vector<uint8_t> vertSpv = readFile(exeDir() + "triangle.vert.spv");
    std::vector<uint8_t> fragSpv = readFile(exeDir() + "triangle.frag.spv");

    VkShaderModuleCreateInfo vertCI{};
    vertCI.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertCI.pCode = reinterpret_cast<const uint32_t*>(vertSpv.data());
    vertCI.codeSize = vertSpv.size();
    VkShaderModule vert{};
    fail(vkCreateShaderModule(device, &vertCI, nullptr, &vert), "vkCreateShaderModuleVertex");

    VkShaderModuleCreateInfo fragCI{};
    fragCI.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragCI.pCode = reinterpret_cast<const uint32_t*>(fragSpv.data());
    fragCI.codeSize = fragSpv.size();
    VkShaderModule frag{};
    fail(vkCreateShaderModule(device, &fragCI, nullptr, &frag), "vkCreateShaderModuleFragment");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription vib{};
    vib.binding = 0;
    vib.stride = 3 * sizeof(float);
    vib.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription via{};
    via.location = 0;
    via.binding = 0;
    via.format = VK_FORMAT_R32G32B32_SFLOAT;
    via.offset = 0;

    VkPipelineVertexInputStateCreateInfo vis{};
    vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vis.vertexBindingDescriptionCount = 1;
    vis.pVertexBindingDescriptions = &vib;
    vis.vertexAttributeDescriptionCount = 1;
    vis.pVertexAttributeDescriptions = &via;

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = static_cast<float>(w);
    vp.height = static_cast<float>(h);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    VkPipelineViewportStateCreateInfo vs{};
    vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vs.viewportCount = 1;
    vs.pViewports = &vp;
    vs.scissorCount = 1;
    VkRect2D sc{};
    sc.offset.x = 0;
    sc.offset.y = 0;
    sc.extent.width = static_cast<uint32_t>(w);
    sc.extent.height = static_cast<uint32_t>(h);
    vs.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.rasterizerDiscardEnable = 0;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.depthClampEnable = 0;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    ms.sampleShadingEnable = 0;
    ms.pSampleMask = nullptr;
    ms.alphaToCoverageEnable = 0;
    ms.alphaToOneEnable = 0;

    VkPipelineColorBlendAttachmentState cbA{};
    cbA.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cbA.blendEnable = 0;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.logicOp = VK_LOGIC_OP_COPY;
    cb.attachmentCount = 1;
    cb.pAttachments = &cbA;

    VkGraphicsPipelineCreateInfo gpc{};
    gpc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpc.stageCount = 2;
    gpc.pStages = stages;
    gpc.pVertexInputState = &vis;
    gpc.pViewportState = &vs;
    gpc.pRasterizationState = &rs;
    gpc.pMultisampleState = &ms;
    gpc.pDepthStencilState = nullptr;
    gpc.pColorBlendState = &cb;
    gpc.renderPass = renderPass;
    gpc.subpass = 0;

    VkPipeline pipeline{};
    fail(vkCreateGraphicsPipelines(device, nullptr, 1, &gpc, nullptr, &pipeline), "vkCreateGraphicsPipelines");

    VkCommandPoolCreateInfo cpCI{};
    cpCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpCI.queueFamilyIndex = gfxFamily;
    VkCommandPool commandPool{};
    fail(vkCreateCommandPool(device, &cpCI, nullptr, &commandPool), "vkCreateCommandPool");

    VkCommandBufferAllocateInfo cBAI{};
    cBAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cBAI.commandPool = commandPool;
    cBAI.commandBufferCount = 1;
    VkCommandBuffer commandBuffer{};
    fail(vkAllocateCommandBuffers(device, &cBAI, &commandBuffer), "vkAllocateCommandBuffers");

    VkSemaphoreCreateInfo semCI{};
    semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore semaphore{};
    fail(vkCreateSemaphore(device, &semCI, nullptr, &semaphore), "vkCreateSemaphore");

    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence{};
    fail(vkCreateFence(device, &fenceCI, nullptr, &fence), "vkCreateFence");

    bool running = true;
    uint32_t imageIndex = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        VkResult ar = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, nullptr, nullptr, &imageIndex);
        if (ar == VK_TIMEOUT) {
            continue;
        }
        if (ar != VK_SUCCESS) {
            fail(ar, "vkAcquireNextImageKHR");
        }

        VkCommandBufferBeginInfo beginCB{};
        beginCB.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginCB.flags = 0;
        fail(vkBeginCommandBuffer(commandBuffer, &beginCB), "vkBeginCommandBuffer");

        VkMemoryBarrier memBarrier{};
        memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memBarrier.srcAccessMask = 0;
        memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                1, &memBarrier,
                0, nullptr,
                0, nullptr);

        VkClearValue clearValue{};
        clearValue.color.float32[0] = 0.05f;
        clearValue.color.float32[1] = 0.05f;
        clearValue.color.float32[2] = 0.1f;
        clearValue.color.float32[3] = 1.0f;
        VkRenderPassBeginInfo rpB{};
        rpB.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpB.renderPass = renderPass;
        rpB.framebuffer = framebuffers[imageIndex];
        rpB.renderArea.offset.x = 0;
        rpB.renderArea.offset.y = 0;
        rpB.renderArea.extent.width = static_cast<uint32_t>(w);
        rpB.renderArea.extent.height = static_cast<uint32_t>(h);
        rpB.clearValueCount = 1;
        rpB.pClearValues = &clearValue;
        vkCmdBeginRenderPass(commandBuffer, &rpB, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdSetViewport(commandBuffer, 0, 1, &vp);
        vkCmdSetScissor(commandBuffer, 0, 1, &sc);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        const VkDeviceSize vertexOffset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);
        fail(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commandBuffer;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &semaphore;
        fail(vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit");
        fail(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
        fail(vkResetFences(device, 1, &fence), "vkResetFences");

        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &semaphore;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &imageIndex;
        fail(vkQueuePresentKHR(queue, &present), "vkQueuePresentKHR");
    }

    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    for (uint32_t i = 0; i < imgCount; ++i) {
        vkDestroyFramebuffer(device, framebuffers[i], nullptr);
        vkDestroyImageView(device, views[i], nullptr);
    }
    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, vbufMem, nullptr);
    vkDestroyFence(device, fence, nullptr);
    vkDestroySemaphore(device, semaphore, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
