#include "BrickVolume.hpp"
#include "core/VulkanContext.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace engine::renderer {

static constexpr uint32_t BRICK_SAMPLES = 8;
static constexpr uint32_t PASS_INIT = 0, PASS_CLASSIFY = 1, PASS_ALLOCATE = 2, PASS_FILL = 3;
static const vk::ImageSubresourceRange kColorRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

static uint32_t divUp(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

BrickVolume::BrickVolume(core::VulkanContext& context, const Config& cfg,
                         const std::array<vk::Buffer, 2>& editBuffers, vk::DeviceSize editBufferSize)
    : context(context), config(cfg) {
    capacity = config.atlasBricks.x * config.atlasBricks.y * config.atlasBricks.z;

    const float cell = cellSize();
    const float halfDiag = 0.5f * cell * std::sqrt(3.0f);
    params.originVoxel = glm::vec4(config.origin, config.voxelSize);
    params.gridDim = glm::uvec4(config.gridCells, MAX_EDITS);
    params.atlasBricks = glm::uvec4(config.atlasBricks, capacity);
    params.metrics = glm::vec4(cell,            // cell size
                               halfDiag,        // half cell diagonal
                               cell,            // brick samples are clamped to +-1 cell
                               4.0f * cell);    // empty cells skip at most 4 cells at once
    stats.capacity = capacity;

    createResources(editBuffers, editBufferSize);
    initialize();
    markAllDirty();
}

BrickVolume::~BrickVolume() = default;

float BrickVolume::dirtyPadding() const {
    // Classification looks this far around each cell center (see passClassify), plus one
    // cell for rounding to the grid.
    const float margin = config.voxelSize * 1.75f;
    return params.metrics.y + margin + params.metrics.w + cellSize();
}

void BrickVolume::createResources(const std::array<vk::Buffer, 2>& editBuffers, vk::DeviceSize editBufferSize) {
    auto& rm = context.getResourceManager();
    auto device = context.getDevice();

    atlas = rm.createImage(config.atlasBricks.x * BRICK_SAMPLES, config.atlasBricks.y * BRICK_SAMPLES,
                           config.atlasBricks.z * BRICK_SAMPLES, vk::Format::eR16Sfloat, vk::ImageTiling::eOptimal,
                           vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                           vk::MemoryPropertyFlagBits::eDeviceLocal, vk::ImageViewType::e3D);

    sparseMap = rm.createImage(config.gridCells.x, config.gridCells.y, config.gridCells.z,
                               vk::Format::eR32Uint, vk::ImageTiling::eOptimal,
                               vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst,
                               vk::MemoryPropertyFlagBits::eDeviceLocal, vk::ImageViewType::e3D);

    const auto storage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
    stateBuffer = rm.createBuffer(16 + sizeof(uint32_t) * capacity, storage | vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::MemoryPropertyFlagBits::eDeviceLocal);
    allocBuffer = rm.createBuffer(sizeof(uint32_t) * capacity, storage, vk::MemoryPropertyFlagBits::eDeviceLocal);
    workBuffer = rm.createBuffer(16 + 2 * sizeof(uint32_t) * capacity, storage | vk::BufferUsageFlagBits::eIndirectBuffer,
                                 vk::MemoryPropertyFlagBits::eDeviceLocal);

    paramsBuffer = rm.createBuffer(sizeof(VolumeParams), vk::BufferUsageFlagBits::eUniformBuffer,
                                   vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    void* mapped = device.mapMemory(paramsBuffer.memory.get(), 0, sizeof(VolumeParams));
    std::memcpy(mapped, &params, sizeof(VolumeParams));
    device.unmapMemory(paramsBuffer.memory.get());

    for (auto& sb : statsBuffers) {
        sb = rm.createBuffer(16, vk::BufferUsageFlagBits::eTransferDst,
                             vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    }

    descriptors = std::make_unique<DescriptorManager>(device);
    const auto cs = vk::ShaderStageFlagBits::eCompute;
    setLayout = descriptors->createLayout({
        { 0, vk::DescriptorType::eStorageBuffer, 1, cs }, // edits
        { 1, vk::DescriptorType::eStorageImage,  1, cs }, // sparse map
        { 2, vk::DescriptorType::eStorageImage,  1, cs }, // atlas
        { 3, vk::DescriptorType::eStorageBuffer, 1, cs }, // state / free stack
        { 4, vk::DescriptorType::eStorageBuffer, 1, cs }, // alloc list
        { 5, vk::DescriptorType::eStorageBuffer, 1, cs }, // work list
        { 6, vk::DescriptorType::eUniformBuffer, 1, cs }, // volume params
    });

    vk::DescriptorImageInfo mapInfo{ nullptr, sparseMap.view.get(), vk::ImageLayout::eGeneral };
    vk::DescriptorImageInfo atlasInfo{ nullptr, atlas.view.get(), vk::ImageLayout::eGeneral };
    vk::DescriptorBufferInfo stateInfo{ stateBuffer.buffer.get(), 0, VK_WHOLE_SIZE };
    vk::DescriptorBufferInfo allocInfo{ allocBuffer.buffer.get(), 0, VK_WHOLE_SIZE };
    vk::DescriptorBufferInfo workInfo{ workBuffer.buffer.get(), 0, VK_WHOLE_SIZE };
    vk::DescriptorBufferInfo paramsInfo{ paramsBuffer.buffer.get(), 0, sizeof(VolumeParams) };

    for (size_t f = 0; f < sets.size(); ++f) {
        sets[f] = descriptors->allocateSet(setLayout);
        vk::DescriptorBufferInfo editInfo{ editBuffers[f], 0, editBufferSize };
        descriptors->updateSet(sets[f], {
            { sets[f], 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &editInfo },
            { sets[f], 1, 0, 1, vk::DescriptorType::eStorageImage, &mapInfo },
            { sets[f], 2, 0, 1, vk::DescriptorType::eStorageImage, &atlasInfo },
            { sets[f], 3, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &stateInfo },
            { sets[f], 4, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &allocInfo },
            { sets[f], 5, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &workInfo },
            { sets[f], 6, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &paramsInfo },
        });
    }

    vk::PushConstantRange push{ cs, 0, sizeof(BakePush) };
    pipeline = std::make_unique<ComputePipeline>(device, "BrickBake.spv",
                                                 std::vector<vk::DescriptorSetLayout>{ setLayout },
                                                 std::vector<vk::PushConstantRange>{ push });

    const double mb = (double(config.atlasBricks.x) * config.atlasBricks.y * config.atlasBricks.z * 512 * 2 +
                       double(config.gridCells.x) * config.gridCells.y * config.gridCells.z * 4) / (1024.0 * 1024.0);
    std::cout << "BrickVolume: " << config.gridCells.x << "^3 cells of " << cellSize() << " m, "
              << capacity << " bricks, ~" << static_cast<int>(mb) << " MB" << std::endl;
}

void BrickVolume::initialize() {
    context.immediateSubmit([&](vk::CommandBuffer cmd) {
        std::array<vk::ImageMemoryBarrier, 2> toGeneral;
        vk::Image images[2] = { atlas.image.get(), sparseMap.image.get() };
        for (int i = 0; i < 2; ++i) {
            toGeneral[i].oldLayout = vk::ImageLayout::eUndefined;
            toGeneral[i].newLayout = vk::ImageLayout::eGeneral;
            toGeneral[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toGeneral[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toGeneral[i].dstAccessMask = vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eShaderWrite;
            toGeneral[i].image = images[i];
            toGeneral[i].subresourceRange = kColorRange;
        }
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                            vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eComputeShader,
                            {}, nullptr, nullptr, toGeneral);

        // Every cell starts empty with a safe distance of 0 (float bits 0).
        vk::ClearColorValue zero(std::array<uint32_t, 4>{ 0, 0, 0, 0 });
        cmd.clearColorImage(sparseMap.image.get(), vk::ImageLayout::eGeneral, zero, kColorRange);

        vk::MemoryBarrier cleared{ vk::AccessFlagBits::eTransferWrite,
                                   vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite };
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                            {}, cleared, nullptr, nullptr);

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->getPipeline());
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline->getLayout(), 0, sets[0], nullptr);
        BakePush push{ glm::uvec4(0, 0, 0, PASS_INIT), glm::uvec4(0) };
        cmd.pushConstants(pipeline->getLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(push), &push);
        cmd.dispatch(divUp(capacity, 512), 1, 1);
    });
    stats.used = 0;
}

void BrickVolume::markDirty(const glm::vec3& wMin, const glm::vec3& wMax) {
    const float pad = dirtyPadding();
    const float cell = cellSize();
    glm::ivec3 grid(config.gridCells);
    glm::ivec3 lo = glm::clamp(glm::ivec3(glm::floor((wMin - pad - config.origin) / cell)), glm::ivec3(0), grid);
    glm::ivec3 hi = glm::clamp(glm::ivec3(glm::ceil((wMax + pad - config.origin) / cell)), glm::ivec3(0), grid);
    if (glm::any(glm::lessThanEqual(hi, lo))) return;

    if (!dirty) {
        dirtyMin = lo;
        dirtyMax = hi;
        dirty = true;
    } else {
        dirtyMin = glm::min(dirtyMin, lo);
        dirtyMax = glm::max(dirtyMax, hi);
    }
}

void BrickVolume::markAllDirty() {
    dirty = true;
    dirtyMin = glm::ivec3(0);
    dirtyMax = glm::ivec3(config.gridCells);
}

void BrickVolume::record(vk::CommandBuffer cmd, uint32_t frame, uint32_t editCount) {
    if (!dirty) return;
    dirty = false;

    glm::uvec3 boxMin(dirtyMin);
    glm::uvec3 boxSize(dirtyMax - dirtyMin);
    uint32_t boxCells = boxSize.x * boxSize.y * boxSize.z;
    stats.lastDirtyCells = boxCells;
    editCount = std::min(editCount, MAX_EDITS);

    const auto compute = vk::PipelineStageFlagBits::eComputeShader;
    const auto transfer = vk::PipelineStageFlagBits::eTransfer;
    const auto rw = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

    // Earlier frames' raymarch reads the atlas/map; earlier bakes wrote them and the lists.
    vk::MemoryBarrier begin{ rw | vk::AccessFlagBits::eIndirectCommandRead | vk::AccessFlagBits::eTransferRead,
                             rw | vk::AccessFlagBits::eTransferWrite };
    cmd.pipelineBarrier(compute | transfer | vk::PipelineStageFlagBits::eDrawIndirect, compute | transfer,
                        {}, begin, nullptr, nullptr);

    // Reset the work list header (it is also the indirect dispatch command) and the
    // overflow + alloc counters, which therefore describe the latest bake only.
    const uint32_t header[4] = { 0, 1, 1, 0 };
    cmd.updateBuffer(workBuffer.buffer.get(), 0, sizeof(header), header);
    cmd.fillBuffer(stateBuffer.buffer.get(), 4, 8, 0);

    vk::MemoryBarrier reset{ vk::AccessFlagBits::eTransferWrite, rw };
    cmd.pipelineBarrier(transfer, compute, {}, reset, nullptr, nullptr);

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->getPipeline());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline->getLayout(), 0, sets[frame], nullptr);

    auto push = [&](uint32_t pass) {
        BakePush p{ glm::uvec4(boxMin, pass), glm::uvec4(boxSize, editCount) };
        cmd.pushConstants(pipeline->getLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(p), &p);
    };
    vk::MemoryBarrier step{ vk::AccessFlagBits::eShaderWrite, rw };

    push(PASS_CLASSIFY);
    cmd.dispatch(divUp(boxSize.x, 8), divUp(boxSize.y, 8), divUp(boxSize.z, 8));
    cmd.pipelineBarrier(compute, compute, {}, step, nullptr, nullptr);

    push(PASS_ALLOCATE);
    cmd.dispatch(divUp(std::min(boxCells, capacity), 512), 1, 1);

    vk::MemoryBarrier toIndirect{ vk::AccessFlagBits::eShaderWrite, rw | vk::AccessFlagBits::eIndirectCommandRead };
    cmd.pipelineBarrier(compute, compute | vk::PipelineStageFlagBits::eDrawIndirect, {}, toIndirect, nullptr, nullptr);

    push(PASS_FILL);
    cmd.dispatchIndirect(workBuffer.buffer.get(), 0);

    // Make the new bricks visible to the raymarcher and the counters to the host.
    vk::MemoryBarrier done{ vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead };
    cmd.pipelineBarrier(compute, compute | transfer, {}, done, nullptr, nullptr);

    vk::BufferCopy region{ 0, 0, 16 };
    cmd.copyBuffer(stateBuffer.buffer.get(), statsBuffers[frame].buffer.get(), region);
    vk::MemoryBarrier toHost{ vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead };
    cmd.pipelineBarrier(transfer, vk::PipelineStageFlagBits::eHost, {}, toHost, nullptr, nullptr);
    statsPending[frame] = true;
}

void BrickVolume::collectStats(uint32_t frame) {
    if (!statsPending[frame]) return;
    statsPending[frame] = false;

    auto device = context.getDevice();
    uint32_t header[4];
    void* mapped = device.mapMemory(statsBuffers[frame].memory.get(), 0, sizeof(header));
    std::memcpy(header, mapped, sizeof(header));
    device.unmapMemory(statsBuffers[frame].memory.get());

    stats.used = capacity - std::min(header[0], capacity);
    stats.overflow = header[1];
}

} // namespace engine::renderer
