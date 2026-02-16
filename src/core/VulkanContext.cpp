#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include "VulkanContext.hpp"
#include <iostream>
#include <set>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace engine::core {

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

VulkanContext::VulkanContext(Window& window) : window(window) {
    // Initialize the default dynamic dispatcher
    static bool dispatcherInitialized = false;
    if (!dispatcherInitialized) {
        auto vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(glfwGetInstanceProcAddress(nullptr, "vkGetInstanceProcAddr"));
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
        dispatcherInitialized = true;
    }

    createInstance();
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance.get());

    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device.get());

    int width, height;
    window.getFramebufferSize(width, height);
    swapChain = std::make_unique<renderer::Swapchain>(device.get(), physicalDevice, surface.get(), width, height);
    
    resourceManager = std::make_unique<renderer::ResourceManager>(device.get(), physicalDevice);
    
    // Initial size: 64x64x64 bricks = 512x512x512 voxels
    brickAtlas = std::make_unique<renderer::BrickAtlas>(*resourceManager, 64, 64, 64);
    
    // Spatial index for a 128x128x128 grid
    sparseMap = std::make_unique<renderer::SparseMap>(*resourceManager, 128, 128, 128);

    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
}

VulkanContext::~VulkanContext() {
    // Unique handles will cleanup automatically
}

void VulkanContext::createInstance() {
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("Validation layers requested, but not available!");
    }

    vk::ApplicationInfo appInfo{};
    appInfo.pApplicationName = "SDF Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    // Targeting Vulkan 1.4+
    appInfo.apiVersion = VK_API_VERSION_1_4;

    vk::InstanceCreateInfo createInfo{};
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    instance = vk::createInstanceUnique(createInfo);
}

void VulkanContext::createSurface() {
    VkSurfaceKHR rawSurface;
    if (glfwCreateWindowSurface(instance.get(), window.getGLFWwindow(), nullptr, &rawSurface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
    surface = vk::UniqueSurfaceKHR(rawSurface, instance.get());
}

void VulkanContext::pickPhysicalDevice() {
    auto devices = instance->enumeratePhysicalDevices();
    if (devices.empty()) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    for (const auto& dev : devices) {
        if (isDeviceSuitable(dev)) {
            physicalDevice = dev;
            break;
        }
    }

    if (!physicalDevice) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

void VulkanContext::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

    float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.queueFamilyIndex = indices.graphicsFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    vk::PhysicalDeviceFeatures deviceFeatures{};
    // Basic features for now, Vulkan 1.4 implies many features are core
    
    vk::DeviceCreateInfo createInfo{};
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;

    // Device extensions could go here (e.g. VK_KHR_SWAPCHAIN_EXTENSION_NAME)
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    device = physicalDevice.createDeviceUnique(createInfo);
    graphicsQueue = device->getQueue(indices.graphicsFamily, 0);
    queueFamilyIndex = indices.graphicsFamily;
}

VulkanContext::QueueFamilyIndices VulkanContext::findQueueFamilies(vk::PhysicalDevice dev) {
    QueueFamilyIndices indices;
    auto queueFamilies = dev.getQueueFamilyProperties();

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphicsFamily = i;
            indices.hasGraphicsFamily = true;
        }

        if (indices.isComplete()) {
            break;
        }
        i++;
    }

    return indices;
}

bool VulkanContext::isDeviceSuitable(vk::PhysicalDevice dev) {
    QueueFamilyIndices indices = findQueueFamilies(dev);
    
    auto props = dev.getProperties();
    // Ensure it supports Vulkan 1.4 API version (major.minor)
    bool supportsVersion = props.apiVersion >= VK_API_VERSION_1_4;

    return indices.isComplete() && supportsVersion;
}

std::vector<const char*> VulkanContext::getRequiredExtensions() {
    auto extensions = Window::getRequiredExtensions();
    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    return extensions;
}

bool VulkanContext::checkValidationLayerSupport() {
    auto availableLayers = vk::enumerateInstanceLayerProperties();

    for (const char* layerName : validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) return false;
    }

    return true;
}

void VulkanContext::setupDebugMessenger() {
    if (!enableValidationLayers) return;
    // Implementation of debug messenger omitted for brevity in initial foundation
}

void VulkanContext::createSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    vk::SemaphoreCreateInfo semaphoreInfo{};
    vk::FenceCreateInfo fenceInfo{};
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        imageAvailableSemaphores[i] = device->createSemaphoreUnique(semaphoreInfo);
        renderFinishedSemaphores[i] = device->createSemaphoreUnique(semaphoreInfo);
        inFlightFences[i] = device->createFenceUnique(fenceInfo);
    }
}

void VulkanContext::createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;

    commandPool = device->createCommandPoolUnique(poolInfo);
}

void VulkanContext::createCommandBuffers() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = commandPool.get();
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

    commandBuffers = device->allocateCommandBuffersUnique(allocInfo);
}

void VulkanContext::beginFrame() {
    auto result = device->waitForFences(1, &inFlightFences[currentFrame].get(), VK_TRUE, UINT64_MAX);
    (void)result;

    device->resetFences(1, &inFlightFences[currentFrame].get());

    try {
        auto acquire = device->acquireNextImageKHR(swapChain->getSwapchain(), UINT64_MAX, imageAvailableSemaphores[currentFrame].get(), nullptr);
        imageIndex = acquire.value;
    } catch (const vk::OutOfDateKHRError&) {
        return;
    }

    commandBuffers[currentFrame]->reset();
    vk::CommandBufferBeginInfo beginInfo{};
    commandBuffers[currentFrame]->begin(beginInfo);
}

void VulkanContext::endFrameBlit(vk::Image sourceImage) {
    auto cmd = commandBuffers[currentFrame].get();

    // Transition swapchain image to TransferDst for blit
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapChain->getImages()[imageIndex];
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = vk::AccessFlagBits::eNone;
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eTransfer,
        {}, nullptr, nullptr, barrier
    );

    // Blit from sourceImage (storage result) to swapchain image
    auto extent = swapChain->getExtent();
    vk::ImageBlit blit{};
    blit.srcOffsets[1] = vk::Offset3D{ static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1 };
    blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    blit.srcSubresource.layerCount = 1;
    blit.dstOffsets[1] = vk::Offset3D{ static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1 };
    blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    blit.dstSubresource.layerCount = 1;

    cmd.blitImage(
        sourceImage, vk::ImageLayout::eTransferSrcOptimal,
        swapChain->getImages()[imageIndex], vk::ImageLayout::eTransferDstOptimal,
        1, &blit, vk::Filter::eLinear
    );

    // Transition swapchain to ColorAttachmentOptimal for ImGui overlay
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        {}, nullptr, nullptr, barrier
    );
}

void VulkanContext::endFramePresent() {
    auto cmd = commandBuffers[currentFrame].get();

    // Transition swapchain image to Present layout
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
    barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapChain->getImages()[imageIndex];
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eNone;

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::PipelineStageFlagBits::eBottomOfPipe,
        {}, nullptr, nullptr, barrier
    );

    cmd.end();

    vk::SubmitInfo submitInfo{};
    vk::Semaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame].get() };
    vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame].get();

    vk::Semaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame].get() };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    graphicsQueue.submit(submitInfo, inFlightFences[currentFrame].get());

    vk::PresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    
    vk::SwapchainKHR swapChains[] = { swapChain->getSwapchain() };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    try {
        auto resultPresent = graphicsQueue.presentKHR(presentInfo);
        (void)resultPresent;
    } catch (const vk::OutOfDateKHRError&) {
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanContext::immediateSubmit(std::function<void(vk::CommandBuffer)> func) {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = commandPool.get();
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = 1;

    auto cmdBuffers = device->allocateCommandBuffersUnique(allocInfo);
    auto cmd = cmdBuffers[0].get();

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(beginInfo);
    func(cmd);
    cmd.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    graphicsQueue.submit(submitInfo, nullptr);
    graphicsQueue.waitIdle();
}

} // namespace engine::core
