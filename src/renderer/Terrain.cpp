#include "Terrain.hpp"
#include "DescriptorManager.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace engine::renderer {

static const vk::ImageSubresourceRange kColorRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

Terrain::Terrain(core::VulkanContext& context, uint32_t size)
    : context(context), size(size) {
    createResources();
    createPipeline();
}

Terrain::~Terrain() = default;

void Terrain::createResources() {
    auto& rm = context.getResourceManager();

    heightmap = rm.createImage(size, size, 1, vk::Format::eR32Sfloat, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eDeviceLocal, vk::ImageViewType::e2D);

    heightScratch = rm.createImage(size, size, 1, vk::Format::eR32Sfloat, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal, vk::ImageViewType::e2D);

    splatmap = rm.createImage(size, size, 1, vk::Format::eR8G8B8A8Unorm, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal, vk::ImageViewType::e2D);

    // All three images live in GENERAL for their whole lifetime.
    context.immediateSubmit([&](vk::CommandBuffer cmd) {
        std::array<vk::ImageMemoryBarrier, 3> barriers;
        vk::Image images[3] = { heightmap.image.get(), heightScratch.image.get(), splatmap.image.get() };
        for (int i = 0; i < 3; ++i) {
            barriers[i].oldLayout = vk::ImageLayout::eUndefined;
            barriers[i].newLayout = vk::ImageLayout::eGeneral;
            barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].dstAccessMask = vk::AccessFlagBits::eTransferWrite;
            barriers[i].image = images[i];
            barriers[i].subresourceRange = kColorRange;
        }
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                            {}, nullptr, nullptr, barriers);

        vk::ClearColorValue zero(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f });
        vk::ClearColorValue baseLayer(std::array<float, 4>{ 1.0f, 0.0f, 0.0f, 0.0f });
        cmd.clearColorImage(heightmap.image.get(), vk::ImageLayout::eGeneral, zero, kColorRange);
        cmd.clearColorImage(heightScratch.image.get(), vk::ImageLayout::eGeneral, zero, kColorRange);
        cmd.clearColorImage(splatmap.image.get(), vk::ImageLayout::eGeneral, baseLayer, kColorRange);
    });
}

void Terrain::createPipeline() {
    descriptorManager = std::make_unique<DescriptorManager>(context.getDevice());

    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        { 0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute }, // heightmap
        { 1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute }, // splatmap
        { 2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute }, // height snapshot
    };
    descriptorSetLayout = descriptorManager->createLayout(bindings);
    descriptorSet = descriptorManager->allocateSet(descriptorSetLayout);

    vk::DescriptorImageInfo heightInfo{ nullptr, heightmap.view.get(), vk::ImageLayout::eGeneral };
    vk::DescriptorImageInfo splatInfo{ nullptr, splatmap.view.get(), vk::ImageLayout::eGeneral };
    vk::DescriptorImageInfo scratchInfo{ nullptr, heightScratch.view.get(), vk::ImageLayout::eGeneral };

    descriptorManager->updateSet(descriptorSet, {
        { descriptorSet, 0, 0, 1, vk::DescriptorType::eStorageImage, &heightInfo },
        { descriptorSet, 1, 0, 1, vk::DescriptorType::eStorageImage, &splatInfo },
        { descriptorSet, 2, 0, 1, vk::DescriptorType::eStorageImage, &scratchInfo },
    });

    vk::PushConstantRange pcRange{ vk::ShaderStageFlagBits::eCompute, 0, sizeof(BrushParams) };
    computePipeline = std::make_unique<ComputePipeline>(
        context.getDevice(), "TerrainBrush.spv",
        std::vector<vk::DescriptorSetLayout>{ descriptorSetLayout },
        std::vector<vk::PushConstantRange>{ pcRange });
}

void Terrain::queueBrush(glm::vec3 worldPos, float worldRadius, float strength, BrushMode mode,
                         uint32_t layer, float targetHeight) {
    BrushParams p{};
    p.pos = glm::vec2((worldPos.x + WORLD_SIZE * 0.5f) / WORLD_SIZE, (worldPos.z + WORLD_SIZE * 0.5f) / WORLD_SIZE);
    p.radius = worldRadius / WORLD_SIZE;
    p.strength = strength;
    p.mode = static_cast<uint32_t>(mode);
    p.layer = layer;
    p.targetHeight = targetHeight;
    pendingParams = p;
    hasPending = true;
}

void Terrain::executePending(vk::CommandBuffer cmd) {
    if (!hasPending) return;
    hasPending = false;

    // Texel rectangle covered by the brush (+1 texel for the smoothing kernel).
    float s = static_cast<float>(size);
    int x0 = std::max(0, static_cast<int>(std::floor((pendingParams.pos.x - pendingParams.radius) * s)) - 1);
    int y0 = std::max(0, static_cast<int>(std::floor((pendingParams.pos.y - pendingParams.radius) * s)) - 1);
    int x1 = std::min(static_cast<int>(size), static_cast<int>(std::ceil((pendingParams.pos.x + pendingParams.radius) * s)) + 1);
    int y1 = std::min(static_cast<int>(size), static_cast<int>(std::ceil((pendingParams.pos.y + pendingParams.radius) * s)) + 1);
    if (x1 <= x0 || y1 <= y0) return;
    pendingParams.origin = glm::ivec2(x0, y0);

    // Previous frames sample the heightmap/splatmap in the raymarcher (WAR) and may have written them.
    vk::MemoryBarrier before{};
    before.srcAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
    before.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite |
                           vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eTransfer,
                        {}, before, nullptr, nullptr);

    if (pendingParams.mode == static_cast<uint32_t>(BrushMode::Smooth)) {
        vk::ImageCopy region{};
        region.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
        region.dstSubresource = region.srcSubresource;
        region.srcOffset = vk::Offset3D{ x0, y0, 0 };
        region.dstOffset = region.srcOffset;
        region.extent = vk::Extent3D{ static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0), 1 };
        cmd.copyImage(heightmap.image.get(), vk::ImageLayout::eGeneral,
                      heightScratch.image.get(), vk::ImageLayout::eGeneral, region);

        vk::MemoryBarrier copied{ vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead };
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                            {}, copied, nullptr, nullptr);
    }

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline->getPipeline());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipeline->getLayout(), 0, 1, &descriptorSet, 0, nullptr);
    cmd.pushConstants(computePipeline->getLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(BrushParams), &pendingParams);
    cmd.dispatch((x1 - x0 + 7) / 8, (y1 - y0 + 7) / 8, 1);

    vk::MemoryBarrier after{ vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead };
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                        {}, after, nullptr, nullptr);
}

} // namespace engine::renderer
