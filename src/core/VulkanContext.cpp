#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include "VulkanContext.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace engine::core {

static const std::vector<const char*> kValidationLayers = { "VK_LAYER_KHRONOS_validation" };

// Validation is on in Debug builds, and can be forced in any build with SDF_VALIDATION=1.
static bool wantValidation() {
    const char* env = std::getenv("SDF_VALIDATION");
    if (env) return std::strcmp(env, "0") != 0;
#ifdef NDEBUG
    return false;
#else
    return true;
#endif
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {
    const char* tag = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "ERROR" : "WARN";
    std::cerr << "[Vulkan " << tag << "] " << data->pMessage << std::endl;
    return VK_FALSE;
}

VulkanContext::VulkanContext(Window& window) : window(window) {
    static bool dispatcherInitialized = false;
    if (!dispatcherInitialized) {
        auto getProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(glfwGetInstanceProcAddress(nullptr, "vkGetInstanceProcAddr"));
        VULKAN_HPP_DEFAULT_DISPATCHER.init(getProc);
        dispatcherInitialized = true;
    }

    validationEnabled = wantValidation();

    createInstance();
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance.get());

    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device.get());

    createSwapchain();
    resourceManager = std::make_unique<renderer::ResourceManager>(device.get(), physicalDevice);

    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
}

VulkanContext::~VulkanContext() {
    if (device) device->waitIdle();
    if (debugMessenger) {
        instance->destroyDebugUtilsMessengerEXT(debugMessenger);
    }
}

void VulkanContext::createInstance() {
    if (validationEnabled && !checkValidationLayerSupport()) {
        std::cerr << "Validation layers requested but not installed; continuing without them." << std::endl;
        validationEnabled = false;
    }

    vk::ApplicationInfo appInfo{};
    appInfo.pApplicationName = "SDF Playground";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
    appInfo.pEngineName = "SDF Playground";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 2, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    auto extensions = getRequiredExtensions();

    vk::InstanceCreateInfo createInfo{};
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    if (validationEnabled) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
        createInfo.ppEnabledLayerNames = kValidationLayers.data();
    }

    instance = vk::createInstanceUnique(createInfo);
}

void VulkanContext::setupDebugMessenger() {
    if (!validationEnabled) return;

    // Plain C struct: the C++ callback typedef differs between vulkan.hpp versions.
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;

    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    auto create = VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateDebugUtilsMessengerEXT;
    if (create && create(instance.get(), &info, nullptr, &messenger) == VK_SUCCESS) {
        debugMessenger = messenger;
    }
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

    // Prefer a discrete GPU, then any suitable device.
    for (int pass = 0; pass < 2 && !physicalDevice; ++pass) {
        for (const auto& dev : devices) {
            if (!isDeviceSuitable(dev)) continue;
            bool discrete = dev.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
            if (pass == 1 || discrete) {
                physicalDevice = dev;
                break;
            }
        }
    }

    if (!physicalDevice) {
        throw std::runtime_error("failed to find a GPU with Vulkan 1.3 + dynamic rendering!");
    }
    std::cout << "GPU: " << physicalDevice.getProperties().deviceName.data() << std::endl;
}

void VulkanContext::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

    float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.queueFamilyIndex = indices.graphicsFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    // ImGui uses dynamic rendering, which must be explicitly enabled even on 1.3+.
    vk::PhysicalDeviceVulkan13Features features13{};
    features13.dynamicRendering = VK_TRUE;

    vk::PhysicalDeviceFeatures2 features2{};
    features2.pNext = &features13;

    const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    vk::DeviceCreateInfo createInfo{};
    createInfo.pNext = &features2;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    device = physicalDevice.createDeviceUnique(createInfo);
    graphicsQueue = device->getQueue(indices.graphicsFamily, 0);
    queueFamilyIndex = indices.graphicsFamily;
}

VulkanContext::QueueFamilyIndices VulkanContext::findQueueFamilies(vk::PhysicalDevice dev) {
    QueueFamilyIndices indices;
    auto queueFamilies = dev.getQueueFamilyProperties();

    for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
        const auto& family = queueFamilies[i];
        bool graphicsCompute = (family.queueFlags & vk::QueueFlagBits::eGraphics) &&
                               (family.queueFlags & vk::QueueFlagBits::eCompute);
        bool present = surface && dev.getSurfaceSupportKHR(i, surface.get());
        if (graphicsCompute && present) {
            indices.graphicsFamily = i;
            indices.hasGraphicsFamily = true;
            break;
        }
    }
    return indices;
}

bool VulkanContext::isDeviceSuitable(vk::PhysicalDevice dev) {
    if (!findQueueFamilies(dev).isComplete()) return false;
    if (dev.getProperties().apiVersion < VK_API_VERSION_1_3) return false;

    auto chain = dev.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
    if (!chain.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering) return false;

    // Brick atlas needs linear filtering of R16F 3D textures.
    auto fmt = dev.getFormatProperties(vk::Format::eR16Sfloat);
    return static_cast<bool>(fmt.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear);
}

std::vector<const char*> VulkanContext::getRequiredExtensions() {
    auto extensions = Window::getRequiredExtensions();
    if (validationEnabled) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    return extensions;
}

bool VulkanContext::checkValidationLayerSupport() {
    auto availableLayers = vk::enumerateInstanceLayerProperties();
    for (const char* layerName : kValidationLayers) {
        bool found = false;
        for (const auto& props : availableLayers) {
            if (std::strcmp(layerName, props.layerName) == 0) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

void VulkanContext::createSwapchain() {
    int width = 0, height = 0;
    window.getFramebufferSize(width, height);
    swapChain = std::make_unique<renderer::Swapchain>(device.get(), physicalDevice, surface.get(), width, height);
    createRenderFinishedSemaphores();
}

void VulkanContext::recreateSwapchain() {
    int width = 0, height = 0;
    window.getFramebufferSize(width, height);
    while (width == 0 || height == 0) { // minimized
        glfwWaitEvents();
        window.getFramebufferSize(width, height);
    }
    device->waitIdle();
    swapChain.reset();
    createSwapchain();
    swapchainDirty = false;
    ++swapchainGeneration;
}

void VulkanContext::createSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    vk::FenceCreateInfo fenceInfo{};
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        imageAvailableSemaphores[i] = device->createSemaphoreUnique({});
        inFlightFences[i] = device->createFenceUnique(fenceInfo);
    }
}

void VulkanContext::createRenderFinishedSemaphores() {
    // One per swapchain image: a semaphore waited on by present may only be reused
    // once that image has been re-acquired.
    renderFinishedSemaphores.clear();
    for (size_t i = 0; i < swapChain->getImages().size(); ++i) {
        renderFinishedSemaphores.push_back(device->createSemaphoreUnique({}));
    }
}

void VulkanContext::createCommandPool() {
    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    commandPool = device->createCommandPoolUnique(poolInfo);
}

void VulkanContext::createCommandBuffers() {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = commandPool.get();
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    commandBuffers = device->allocateCommandBuffersUnique(allocInfo);
}

bool VulkanContext::beginFrame() {
    if (swapchainDirty) {
        recreateSwapchain();
        return false;
    }

    (void)device->waitForFences(1, &inFlightFences[currentFrame].get(), VK_TRUE, UINT64_MAX);

    vk::Result result;
    try {
        auto acquire = device->acquireNextImageKHR(swapChain->getSwapchain(), UINT64_MAX,
                                                   imageAvailableSemaphores[currentFrame].get(), nullptr);
        result = acquire.result;
        imageIndex = acquire.value;
    } catch (const vk::OutOfDateKHRError&) {
        recreateSwapchain();
        return false;
    }
    if (result == vk::Result::eSuboptimalKHR) swapchainDirty = true; // still usable this frame

    // Only reset the fence once we know work will be submitted, otherwise the next wait deadlocks.
    (void)device->resetFences(1, &inFlightFences[currentFrame].get());

    commandBuffers[currentFrame]->reset();
    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    commandBuffers[currentFrame]->begin(beginInfo);
    return true;
}

void VulkanContext::endFrameBlit(vk::Image sourceImage, vk::Extent2D sourceExtent) {
    auto cmd = commandBuffers[currentFrame].get();
    vk::Image target = swapChain->getImages()[imageIndex];

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = target;
    barrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
    barrier.srcAccessMask = vk::AccessFlagBits::eNone;
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

    // The swapchain image is acquired with a semaphore waited at COLOR_ATTACHMENT_OUTPUT,
    // so the layout transition must be ordered after that stage.
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer,
                        {}, nullptr, nullptr, barrier);

    auto extent = swapChain->getExtent();
    vk::ImageBlit blit{};
    blit.srcOffsets[1] = vk::Offset3D{ static_cast<int32_t>(sourceExtent.width), static_cast<int32_t>(sourceExtent.height), 1 };
    blit.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
    blit.dstOffsets[1] = vk::Offset3D{ static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1 };
    blit.dstSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };

    cmd.blitImage(sourceImage, vk::ImageLayout::eTransferSrcOptimal,
                  target, vk::ImageLayout::eTransferDstOptimal,
                  1, &blit, vk::Filter::eLinear);

    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        {}, nullptr, nullptr, barrier);
}

void VulkanContext::endFramePresent() {
    auto cmd = commandBuffers[currentFrame].get();

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
    barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapChain->getImages()[imageIndex];
    barrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
    barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eNone;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe,
                        {}, nullptr, nullptr, barrier);

    cmd.end();

    vk::Semaphore waitSemaphore = imageAvailableSemaphores[currentFrame].get();
    vk::Semaphore signalSemaphore = renderFinishedSemaphores[imageIndex].get();
    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

    vk::SubmitInfo submitInfo{};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &waitSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &signalSemaphore;
    graphicsQueue.submit(submitInfo, inFlightFences[currentFrame].get());

    vk::SwapchainKHR sc = swapChain->getSwapchain();
    vk::PresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &signalSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &sc;
    presentInfo.pImageIndices = &imageIndex;

    try {
        if (graphicsQueue.presentKHR(presentInfo) == vk::Result::eSuboptimalKHR) swapchainDirty = true;
    } catch (const vk::OutOfDateKHRError&) {
        swapchainDirty = true;
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanContext::immediateSubmit(const std::function<void(vk::CommandBuffer)>& func) {
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
