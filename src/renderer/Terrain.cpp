#include "Terrain.hpp"
#include "DescriptorManager.hpp"

namespace engine::renderer {

Terrain::Terrain(core::VulkanContext& context, uint32_t size)
    : context(context), size(size) {
    createResources();
    createPipeline();
}

Terrain::~Terrain() {}

void Terrain::createResources() {
    auto& rm = context.getResourceManager();

    // Heightmap: R32_SFLOAT, Storage | Sampled
    heightmap = rm.createImage(
        size, size, 1,
        vk::Format::eR32Sfloat,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::ImageViewType::e2D
    );

    // Splatmap: R8G8B8A8_UNORM, Storage | Sampled
    splatmap = rm.createImage(
        size, size, 1,
        vk::Format::eR8G8B8A8Unorm,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::ImageViewType::e2D
    );

    // Initialize heightmap to 0
    context.immediateSubmit([&](vk::CommandBuffer cmd) {
        vk::ClearColorValue clearColor(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
        vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        
        // Transition to General for clear/compute
        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eGeneral;
        barrier.srcAccessMask = vk::AccessFlagBits::eNone;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.subresourceRange = range;
        
        barrier.image = heightmap.image.get();
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader, {}, nullptr, nullptr, barrier);
        
        barrier.image = splatmap.image.get();
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader, {}, nullptr, nullptr, barrier);

        cmd.clearColorImage(heightmap.image.get(), vk::ImageLayout::eGeneral, clearColor, range);
        
        // Initialize splatmap to red (1,0,0,0) -> Base layer
        clearColor = std::array<float, 4>{1.0f, 0.0f, 0.0f, 0.0f};
        cmd.clearColorImage(splatmap.image.get(), vk::ImageLayout::eGeneral, clearColor, range);
    });
}

void Terrain::createPipeline() {
    descriptorManager = std::make_unique<DescriptorManager>(context.getDevice());

    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        { 0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute }, // Heightmap
        { 1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute }  // Splatmap
    };

    descriptorSetLayout = descriptorManager->createLayout(bindings);
    descriptorSet = descriptorManager->allocateSet(descriptorSetLayout);

    vk::DescriptorImageInfo heightInfo{};
    heightInfo.imageView = heightmap.view.get();
    heightInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::DescriptorImageInfo splatInfo{};
    splatInfo.imageView = splatmap.view.get();
    splatInfo.imageLayout = vk::ImageLayout::eGeneral;

    std::vector<vk::WriteDescriptorSet> writes = {
        { descriptorSet, 0, 0, 1, vk::DescriptorType::eStorageImage, &heightInfo, nullptr, nullptr },
        { descriptorSet, 1, 0, 1, vk::DescriptorType::eStorageImage, &splatInfo, nullptr, nullptr }
    };
    descriptorManager->updateSet(descriptorSet, writes);

    vk::PushConstantRange pcRange{};
    pcRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pcRange.offset = 0;
    pcRange.size = sizeof(BrushParams);

    computePipeline = std::make_unique<ComputePipeline>(
        context.getDevice(),
        "shaders/TerrainBrush.spv",
        std::vector<vk::DescriptorSetLayout>{ descriptorSetLayout },
        std::vector<vk::PushConstantRange>{ pcRange }
    );
}

void Terrain::queueBrush(const BrushParams& params) {
    pendingParams = params;
    hasPending = true;
}

void Terrain::executePending(vk::CommandBuffer cmd) {
    if (!hasPending) return;

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline->getPipeline());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipeline->getLayout(), 0, 1, &descriptorSet, 0, nullptr);
    cmd.pushConstants(computePipeline->getLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(BrushParams), &pendingParams);

    uint32_t groupX = (size + 7) / 8;
    uint32_t groupY = (size + 7) / 8;
    cmd.dispatch(groupX, groupY, 1);

    hasPending = false;

    // Barrier to ensure SDF shader sees the changes
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eGeneral;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead; // Sampled in SDF
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.image = heightmap.image.get();
    
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, nullptr, nullptr, barrier
    );
    
    barrier.image = splatmap.image.get();
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, nullptr, nullptr, barrier
    );
}

} // namespace engine::renderer
