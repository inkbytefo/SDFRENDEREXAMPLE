#include "SDFRenderer.hpp"
#include <cstring>
#include <GLFW/glfw3.h>

namespace engine::renderer {

SDFRenderer::SDFRenderer(core::VulkanContext& context) : context(context) {
    descriptorManager = std::make_unique<DescriptorManager>(context.getDevice());

    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        { 0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute },  // Brick Atlas
        { 1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute },  // Sparse Map
        { 2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute },  // Out Image
        { 3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute }, // Edit Buffer
        { 4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute }  // Selection Buffer
    };
    descriptorSetLayout = descriptorManager->createLayout(bindings);
    descriptorSet = descriptorManager->allocateSet(descriptorSetLayout);

    // Create SDF Edit buffer (max 256 edits)
    editBuffer = context.getResourceManager().createBuffer(
        sizeof(core::SDFEdit) * 256,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    );

    // Create selection buffer (single int)
    selectionBuffer = context.getResourceManager().createBuffer(
        sizeof(int32_t),
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    );

    int32_t clearVal = -1;
    void* clearPtr = context.getDevice().mapMemory(selectionBuffer.memory.get(), 0, sizeof(int32_t));
    std::memcpy(clearPtr, &clearVal, sizeof(int32_t));
    context.getDevice().unmapMemory(selectionBuffer.memory.get());

    // Push constant range
    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    computePipeline = std::make_unique<ComputePipeline>(
        context.getDevice(), 
        "shaders/SDFCompute.spv", 
        std::vector<vk::DescriptorSetLayout>{ descriptorSetLayout },
        std::vector<vk::PushConstantRange>{ pushConstantRange }
    );

    auto extent = context.getSwapchain()->getExtent();
    outputWidth = extent.width;
    outputHeight = extent.height;

    outputImage = context.getResourceManager().createImage(
        outputWidth, outputHeight, 1,
        vk::Format::eR8G8B8A8Unorm,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::ImageViewType::e2D
    );

    pushConstants.camPosX = camPosX;
    pushConstants.camPosY = camPosY;
    pushConstants.camPosZ = camPosZ;
    pushConstants.resX = static_cast<float>(outputWidth);
    pushConstants.resY = static_cast<float>(outputHeight);
    pushConstants.time = 0.0f;
    pushConstants.editCount = 0.0f;
    pushConstants.mouseX = -1.0f;
    pushConstants.mouseY = -1.0f;
    pushConstants.renderMode = renderMode;
    pushConstants.showGround = showGround ? 1 : 0;

    createDescriptorSets();
}

SDFRenderer::~SDFRenderer() {}

void SDFRenderer::update(float deltaTime, const core::InputState& input, bool imguiCapture) {
    totalTime += deltaTime;
    pushConstants.time = totalTime;
    pushConstants.renderMode = renderMode;
    pushConstants.showGround = showGround ? 1 : 0;

    // Camera control: only when right-click is held and ImGui doesn't capture
    if (input.mouseCaptured && !imguiCapture) {
        // Mouse look
        float sensitivity = 0.003f;
        camYaw -= static_cast<float>(input.mouseDeltaX) * sensitivity;
        camPitch -= static_cast<float>(input.mouseDeltaY) * sensitivity;
        
        // Clamp pitch
        if (camPitch > 1.5f) camPitch = 1.5f;
        if (camPitch < -1.5f) camPitch = -1.5f;
    }

    // Scroll to change speed
    if (!imguiCapture) {
        camSpeed += static_cast<float>(input.scrollDelta) * 0.5f;
        if (camSpeed < 0.5f) camSpeed = 0.5f;
        if (camSpeed > 50.0f) camSpeed = 50.0f;
    }

    // Camera direction from yaw/pitch
    float dirX = cosf(camPitch) * sinf(camYaw);
    float dirY = sinf(camPitch);
    float dirZ = cosf(camPitch) * cosf(camYaw);

    // WASD movement (only when right-click held)
    if (input.mouseCaptured && !imguiCapture) {
        float speed = camSpeed * deltaTime;
        
        // Forward/back
        if (input.isKeyDown(GLFW_KEY_W)) {
            camPosX += dirX * speed;
            camPosY += dirY * speed;
            camPosZ += dirZ * speed;
        }
        if (input.isKeyDown(GLFW_KEY_S)) {
            camPosX -= dirX * speed;
            camPosY -= dirY * speed;
            camPosZ -= dirZ * speed;
        }
        
        // Strafe (right vector = cross(dir, up) -> (-dirZ, 0, dirX))
        float rightX = -dirZ;
        float rightZ = dirX;
        float rLen = sqrtf(rightX * rightX + rightZ * rightZ);
        if (rLen > 0.0001f) { rightX /= rLen; rightZ /= rLen; }
        
        if (input.isKeyDown(GLFW_KEY_D)) {
            camPosX += rightX * speed;
            camPosZ += rightZ * speed;
        }
        if (input.isKeyDown(GLFW_KEY_A)) {
            camPosX -= rightX * speed;
            camPosZ -= rightZ * speed;
        }
        
        // Up/down
        if (input.isKeyDown(GLFW_KEY_E) || input.isKeyDown(GLFW_KEY_SPACE)) {
            camPosY += speed;
        }
        if (input.isKeyDown(GLFW_KEY_Q) || input.isKeyDown(GLFW_KEY_LEFT_CONTROL)) {
            camPosY -= speed;
        }
    }

    pushConstants.camPosX = camPosX;
    pushConstants.camPosY = camPosY;
    pushConstants.camPosZ = camPosZ;
    pushConstants.camDirX = dirX;
    pushConstants.camDirY = dirY;
    pushConstants.camDirZ = dirZ;
    pushConstants.editCount = static_cast<float>(edits.size());

    // Reset picking if not explicitly requested this frame
    if (!pickingRequested) {
        pushConstants.mouseX = -1.0f;
        pushConstants.mouseY = -1.0f;
    }

    // Upload edits if changed
    if (editsDirty) {
        updateEditBuffer();
        editsDirty = false;
    }
}

void SDFRenderer::render(vk::CommandBuffer commandBuffer) {
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = outputImage.image.get();
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = vk::AccessFlagBits::eNone;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;

    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, nullptr, nullptr, barrier
    );

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline->getPipeline());
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipeline->getLayout(), 0, 1, &descriptorSet, 0, nullptr);
    
    commandBuffer.pushConstants(
        computePipeline->getLayout(),
        vk::ShaderStageFlagBits::eCompute,
        0, sizeof(PushConstants), &pushConstants
    );

    uint32_t groupX = (outputWidth + 7) / 8;
    uint32_t groupY = (outputHeight + 7) / 8;
    commandBuffer.dispatch(groupX, groupY, 1);

    // After dispatch, if we were picking, we need a barrier to ensure write is visible to host
    if (pickingRequested) {
        vk::BufferMemoryBarrier pickingBarrier{};
        pickingBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        pickingBarrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
        pickingBarrier.buffer = selectionBuffer.buffer.get();
        pickingBarrier.offset = 0;
        pickingBarrier.size = sizeof(int32_t);

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eHost,
            {}, nullptr, pickingBarrier, nullptr
        );
        pickingRequested = false; 
    }

    barrier.oldLayout = vk::ImageLayout::eGeneral;
    barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eTransfer,
        {}, nullptr, nullptr, barrier
    );
}

void SDFRenderer::updateEditBuffer() {
    if (edits.empty()) return;

    size_t uploadSize = sizeof(core::SDFEdit) * edits.size();
    if (uploadSize > sizeof(core::SDFEdit) * 256) {
        uploadSize = sizeof(core::SDFEdit) * 256;
    }

    void* data = context.getDevice().mapMemory(
        editBuffer.memory.get(), 0, uploadSize
    );
    std::memcpy(data, edits.data(), uploadSize);
    context.getDevice().unmapMemory(editBuffer.memory.get());
}

void SDFRenderer::createDescriptorSets() {
    vk::DescriptorImageInfo atlasInfo{};
    atlasInfo.imageView = context.getBrickAtlas().getAtlasView();
    atlasInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::DescriptorImageInfo mapInfo{};
    mapInfo.imageView = context.getSparseMap().getMapView();
    mapInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::DescriptorImageInfo outInfo{};
    outInfo.imageView = outputImage.view.get();
    outInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::DescriptorBufferInfo editBufInfo{};
    editBufInfo.buffer = editBuffer.buffer.get();
    editBufInfo.offset = 0;
    editBufInfo.range = sizeof(core::SDFEdit) * 256;

    vk::DescriptorBufferInfo selectBufInfo{};
    selectBufInfo.buffer = selectionBuffer.buffer.get();
    selectBufInfo.offset = 0;
    selectBufInfo.range = sizeof(int32_t);

    std::vector<vk::WriteDescriptorSet> writes = {
        { descriptorSet, 0, 0, 1, vk::DescriptorType::eStorageImage, &atlasInfo, nullptr, nullptr },
        { descriptorSet, 1, 0, 1, vk::DescriptorType::eStorageImage, &mapInfo, nullptr, nullptr },
        { descriptorSet, 2, 0, 1, vk::DescriptorType::eStorageImage, &outInfo, nullptr, nullptr },
        { descriptorSet, 3, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &editBufInfo, nullptr },
        { descriptorSet, 4, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &selectBufInfo, nullptr }
    };

    descriptorManager->updateSet(descriptorSet, writes);
}

void SDFRenderer::triggerPicking(float x, float y) {
    pushConstants.mouseX = x;
    pushConstants.mouseY = y;
    pickingRequested = true;
}

int SDFRenderer::getSelectedObjectIndex() {
    int32_t result = -1;
    void* data = context.getDevice().mapMemory(selectionBuffer.memory.get(), 0, sizeof(int32_t));
    std::memcpy(&result, data, sizeof(int32_t));
    context.getDevice().unmapMemory(selectionBuffer.memory.get());

    // Reset it to -1 for next pick
    int32_t clearVal = -1;
    void* clearPtr = context.getDevice().mapMemory(selectionBuffer.memory.get(), 0, sizeof(int32_t));
    std::memcpy(clearPtr, &clearVal, sizeof(int32_t));
    context.getDevice().unmapMemory(selectionBuffer.memory.get());

    return result;
}

} // namespace engine::renderer
