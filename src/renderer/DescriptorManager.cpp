#include "DescriptorManager.hpp"

namespace engine::renderer {

DescriptorManager::DescriptorManager(vk::Device device) : device(device) {
    std::vector<vk::DescriptorPoolSize> poolSizes = {
        { vk::DescriptorType::eStorageImage, 10 },
        { vk::DescriptorType::eStorageBuffer, 10 }
    };

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets = 10;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    descriptorPool = device.createDescriptorPoolUnique(poolInfo);
}

DescriptorManager::~DescriptorManager() {}

vk::DescriptorSetLayout DescriptorManager::createLayout(const std::vector<vk::DescriptorSetLayoutBinding>& bindings) {
    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    layouts.push_back(device.createDescriptorSetLayoutUnique(layoutInfo));
    return layouts.back().get();
}

vk::DescriptorSet DescriptorManager::allocateSet(vk::DescriptorSetLayout layout) {
    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = descriptorPool.get();
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    auto sets = device.allocateDescriptorSets(allocInfo);
    return sets[0];
}

void DescriptorManager::updateSet(vk::DescriptorSet set, const std::vector<vk::WriteDescriptorSet>& writes) {
    std::vector<vk::WriteDescriptorSet> updatedWrites = writes;
    for (auto& write : updatedWrites) {
        write.dstSet = set;
    }
    device.updateDescriptorSets(updatedWrites, nullptr);
}

} // namespace engine::renderer
