#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <map>

namespace engine::renderer {

class DescriptorManager {
public:
    DescriptorManager(vk::Device device);
    ~DescriptorManager();

    vk::DescriptorSetLayout createLayout(const std::vector<vk::DescriptorSetLayoutBinding>& bindings);
    vk::DescriptorSet allocateSet(vk::DescriptorSetLayout layout);

    void updateSet(vk::DescriptorSet set, const std::vector<vk::WriteDescriptorSet>& writes);

private:
    vk::Device device;
    vk::UniqueDescriptorPool descriptorPool;
    std::vector<vk::UniqueDescriptorSetLayout> layouts;
};

} // namespace engine::renderer
