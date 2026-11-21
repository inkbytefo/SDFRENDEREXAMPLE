#include "SDFRenderer.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace engine::renderer {

static const vk::ImageSubresourceRange kColorRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

SDFRenderer::SDFRenderer(core::VulkanContext& ctx) : context(ctx) {
    auto device = context.getDevice();
    auto& rm = context.getResourceManager();

    terrain = std::make_unique<Terrain>(context);

    // Per-frame host-visible buffers, persistently mapped.
    const vk::DeviceSize editBytes = sizeof(core::SDFEdit) * MAX_EDITS;
    std::array<vk::Buffer, FRAMES> editHandles{};
    for (uint32_t f = 0; f < FRAMES; ++f) {
        editBuffers[f] = rm.createBuffer(editBytes, vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        editMapped[f] = device.mapMemory(editBuffers[f].memory.get(), 0, editBytes);
        std::memset(editMapped[f], 0, editBytes);
        editHandles[f] = editBuffers[f].buffer.get();

        selectionBuffers[f] = rm.createBuffer(sizeof(SelectionData), vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        selectionMapped[f] = static_cast<SelectionData*>(
            device.mapMemory(selectionBuffers[f].memory.get(), 0, sizeof(SelectionData)));
        *selectionMapped[f] = SelectionData{};
    }

    volume = std::make_unique<BrickVolume>(context, BrickVolume::Config{}, editHandles, editBytes);

    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.maxLod = 0.0f;
    linearSampler = device.createSamplerUnique(samplerInfo);

    descriptors = std::make_unique<DescriptorManager>(device);
    const auto cs = vk::ShaderStageFlagBits::eCompute;
    setLayout = descriptors->createLayout({
        { 0, vk::DescriptorType::eStorageImage,         1, cs }, // output
        { 1, vk::DescriptorType::eStorageBuffer,        1, cs }, // edits (per frame)
        { 2, vk::DescriptorType::eStorageBuffer,        1, cs }, // selection (per frame)
        { 3, vk::DescriptorType::eCombinedImageSampler, 1, cs }, // terrain height
        { 4, vk::DescriptorType::eCombinedImageSampler, 1, cs }, // terrain splat
        { 5, vk::DescriptorType::eCombinedImageSampler, 1, cs }, // brick atlas
        { 6, vk::DescriptorType::eStorageImage,         1, cs }, // sparse map
        { 7, vk::DescriptorType::eUniformBuffer,        1, cs }, // volume params
    });
    for (auto& s : sets) s = descriptors->allocateSet(setLayout);

    vk::PushConstantRange push{ cs, 0, sizeof(RayPushConstants) };
    pipeline = std::make_unique<ComputePipeline>(device, "SDFCompute.spv",
                                                 std::vector<vk::DescriptorSetLayout>{ setLayout },
                                                 std::vector<vk::PushConstantRange>{ push });

    createOutputImage(context.getSwapchain()->getExtent());
    writeDescriptorSets();
}

SDFRenderer::~SDFRenderer() {
    context.getDevice().waitIdle();
    for (uint32_t f = 0; f < FRAMES; ++f) {
        context.getDevice().unmapMemory(editBuffers[f].memory.get());
        context.getDevice().unmapMemory(selectionBuffers[f].memory.get());
    }
}

void SDFRenderer::createOutputImage(vk::Extent2D extent) {
    outputWidth = std::max(1u, extent.width);
    outputHeight = std::max(1u, extent.height);
    outputImage = context.getResourceManager().createImage(
        outputWidth, outputHeight, 1, vk::Format::eR8G8B8A8Unorm, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eDeviceLocal, vk::ImageViewType::e2D);
}

void SDFRenderer::resize(vk::Extent2D extent) {
    context.getDevice().waitIdle();
    createOutputImage(extent);
    writeDescriptorSets();
}

void SDFRenderer::writeDescriptorSets() {
    vk::DescriptorImageInfo outInfo{ nullptr, outputImage.view.get(), vk::ImageLayout::eGeneral };
    vk::DescriptorImageInfo heightInfo{ linearSampler.get(), terrain->getHeightmap().view.get(), vk::ImageLayout::eGeneral };
    vk::DescriptorImageInfo splatInfo{ linearSampler.get(), terrain->getSplatmap().view.get(), vk::ImageLayout::eGeneral };
    vk::DescriptorImageInfo atlasInfo{ linearSampler.get(), volume->getAtlasView(), vk::ImageLayout::eGeneral };
    vk::DescriptorImageInfo mapInfo{ nullptr, volume->getMapView(), vk::ImageLayout::eGeneral };
    vk::DescriptorBufferInfo paramsInfo{ volume->getParamsBuffer(), 0, sizeof(VolumeParams) };

    for (uint32_t f = 0; f < FRAMES; ++f) {
        vk::DescriptorBufferInfo editInfo{ editBuffers[f].buffer.get(), 0, VK_WHOLE_SIZE };
        vk::DescriptorBufferInfo selInfo{ selectionBuffers[f].buffer.get(), 0, sizeof(SelectionData) };
        descriptors->updateSet(sets[f], {
            { sets[f], 0, 0, 1, vk::DescriptorType::eStorageImage, &outInfo },
            { sets[f], 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &editInfo },
            { sets[f], 2, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &selInfo },
            { sets[f], 3, 0, 1, vk::DescriptorType::eCombinedImageSampler, &heightInfo },
            { sets[f], 4, 0, 1, vk::DescriptorType::eCombinedImageSampler, &splatInfo },
            { sets[f], 5, 0, 1, vk::DescriptorType::eCombinedImageSampler, &atlasInfo },
            { sets[f], 6, 0, 1, vk::DescriptorType::eStorageImage, &mapInfo },
            { sets[f], 7, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &paramsInfo },
        });
    }
}

void SDFRenderer::beginFrame(uint32_t frame) {
    // The fence for this slot has signaled, so the GPU is done with its selection buffer.
    SelectionData result = *selectionMapped[frame];
    hover = result;
    if (clickInFlight[frame]) {
        clickResult = result;
        clickInFlight[frame] = false;
    }
    *selectionMapped[frame] = SelectionData{};
    volume->collectStats(frame);
}

std::optional<SDFRenderer::SelectionData> SDFRenderer::takeClickResult() {
    auto r = clickResult;
    clickResult.reset();
    return r;
}

void SDFRenderer::update(float deltaTime, const core::InputState& input, bool imguiCapture) {
    totalTime += deltaTime;

    if (input.mouseCaptured && !imguiCapture) {
        const float sensitivity = 0.003f;
        camYaw -= static_cast<float>(input.mouseDeltaX) * sensitivity;
        camPitch -= static_cast<float>(input.mouseDeltaY) * sensitivity;
        camPitch = std::clamp(camPitch, -1.5f, 1.5f);
    }

    if (!imguiCapture) {
        camSpeed = std::clamp(camSpeed + static_cast<float>(input.scrollDelta) * 0.5f, 0.5f, 50.0f);
    }

    camDir = glm::vec3(std::cos(camPitch) * std::sin(camYaw), std::sin(camPitch), std::cos(camPitch) * std::cos(camYaw));

    if (input.mouseCaptured && !imguiCapture) {
        float speed = camSpeed * deltaTime;
        // Matches the raymarcher's screen-right vector: cross(forward, up).
        glm::vec3 right = glm::normalize(glm::vec3(-camDir.z, 0.0f, camDir.x) + glm::vec3(1e-6f, 0, 0));
        if (input.isKeyDown(GLFW_KEY_W)) camPos += camDir * speed;
        if (input.isKeyDown(GLFW_KEY_S)) camPos -= camDir * speed;
        if (input.isKeyDown(GLFW_KEY_D)) camPos += right * speed;
        if (input.isKeyDown(GLFW_KEY_A)) camPos -= right * speed;
        if (input.isKeyDown(GLFW_KEY_E) || input.isKeyDown(GLFW_KEY_SPACE)) camPos.y += speed;
        if (input.isKeyDown(GLFW_KEY_Q) || input.isKeyDown(GLFW_KEY_LEFT_CONTROL)) camPos.y -= speed;
    }
}

// Copies the editor's edits into this frame's buffer (sanitized) and marks the volume
// dirty wherever they differ from what is currently baked.
uint32_t SDFRenderer::uploadEdits(uint32_t frame) {
    const size_t count = std::min<size_t>(edits.size(), MAX_EDITS);
    const glm::vec3 vMin = volume->worldMin(), vMax = volume->worldMax();

    std::vector<core::SDFEdit> gpu(edits.begin(), edits.begin() + count);
    for (auto& e : gpu) {
        float len = glm::length(e.rotation);
        e.rotation = len > 1e-6f ? e.rotation / len : glm::vec4(0, 0, 0, 1);
        e.scale = glm::max(e.scale, glm::vec3(0.01f));
        e.position = glm::clamp(e.position, vMin, vMax); // geometry is clipped to the volume
        e.blendFactor = std::max(e.blendFactor, 0.0f);
        if (e.primitiveType > 4) e.primitiveType = 0;
        if (e.operation > 4) e.operation = 0;
    }

    auto touch = [&](const core::SDFEdit& e) {
        if (e.operation == static_cast<uint32_t>(core::SDFOp::Intersection)) {
            volume->markAllDirty(); // intersection clips the whole field
            return;
        }
        float r = core::editBoundRadius(e) + e.blendFactor;
        volume->markDirty(e.position - glm::vec3(r), e.position + glm::vec3(r));
    };

    const size_t maxCount = std::max(gpu.size(), bakedEdits.size());
    for (size_t i = 0; i < maxCount; ++i) {
        const core::SDFEdit* now = i < gpu.size() ? &gpu[i] : nullptr;
        const core::SDFEdit* was = i < bakedEdits.size() ? &bakedEdits[i] : nullptr;
        if (now && was && std::memcmp(now, was, sizeof(core::SDFEdit)) == 0) continue;
        if (now) touch(*now);
        if (was) touch(*was);
    }
    bakedEdits = gpu;

    if (count > 0) std::memcpy(editMapped[frame], gpu.data(), sizeof(core::SDFEdit) * count);
    return static_cast<uint32_t>(count);
}

void SDFRenderer::render(vk::CommandBuffer cmd, uint32_t frame) {
    uint32_t editCount = uploadEdits(frame);

    terrain->executePending(cmd);
    volume->record(cmd, frame, editCount);

    vk::ImageMemoryBarrier toGeneral{};
    toGeneral.oldLayout = vk::ImageLayout::eUndefined;
    toGeneral.newLayout = vk::ImageLayout::eGeneral;
    toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.image = outputImage.image.get();
    toGeneral.subresourceRange = kColorRange;
    toGeneral.srcAccessMask = vk::AccessFlagBits::eTransferRead; // previous frame's blit
    toGeneral.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                        {}, nullptr, nullptr, toGeneral);

    RayPushConstants pc{};
    pc.camPos = glm::vec4(camPos, totalTime);
    pc.camDir = glm::vec4(camDir, 0.0f);
    pc.brush = glm::vec4(brushPos, brushRadius);
    pc.mouse = glm::vec4(mouseX, mouseY, static_cast<float>(editCount), 0.0f);
    pc.flags = glm::uvec4(renderMode, showGround ? 1u : 0u, showGrid ? 1u : 0u, 0u);
    pc.size = glm::uvec4(outputWidth, outputHeight, 0u, 0u);

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->getPipeline());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline->getLayout(), 0, sets[frame], nullptr);
    cmd.pushConstants(pipeline->getLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);
    cmd.dispatch((outputWidth + 7) / 8, (outputHeight + 7) / 8, 1);

    if (clickRequested) {
        clickInFlight[frame] = true;
        clickRequested = false;
    }

    // Selection result -> host (read after this slot's fence); output -> blit.
    vk::MemoryBarrier toHost{ vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eHostRead };
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eHost,
                        {}, toHost, nullptr, nullptr);

    vk::ImageMemoryBarrier toSrc = toGeneral;
    toSrc.oldLayout = vk::ImageLayout::eGeneral;
    toSrc.newLayout = vk::ImageLayout::eTransferSrcOptimal;
    toSrc.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    toSrc.dstAccessMask = vk::AccessFlagBits::eTransferRead;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer,
                        {}, nullptr, nullptr, toSrc);
}

} // namespace engine::renderer
