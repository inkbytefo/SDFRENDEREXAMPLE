#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <string>

namespace engine::renderer {

class ComputePipeline {
public:
    ComputePipeline(vk::Device device, const std::string& shaderPath, const std::vector<vk::DescriptorSetLayout>& layouts, 
                    const std::vector<vk::PushConstantRange>& pushConstantRanges = {});
    ~ComputePipeline();

    vk::Pipeline getPipeline() const { return pipeline.get(); }
    vk::PipelineLayout getLayout() const { return pipelineLayout.get(); }

private:
    vk::Device device;
    vk::UniquePipelineLayout pipelineLayout;
    vk::UniquePipeline pipeline;

    vk::UniqueShaderModule createShaderModule(const std::vector<uint32_t>& code);
};

} // namespace engine::renderer
