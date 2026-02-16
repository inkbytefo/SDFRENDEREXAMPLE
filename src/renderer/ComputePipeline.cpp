#include "ComputePipeline.hpp"
#include <fstream>

namespace engine::renderer {

ComputePipeline::ComputePipeline(vk::Device device, const std::string& shaderPath, const std::vector<vk::DescriptorSetLayout>& layouts,
                                 const std::vector<vk::PushConstantRange>& pushConstantRanges)
    : device(device) {
    
    // Load SPIR-V binary
    std::ifstream file(shaderPath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        // Try fallback for when running from project root
        std::string fallback = "build/" + shaderPath;
        file.open(fallback, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("failed to open shader file: " + shaderPath + " (also checked " + fallback + ")");
        }
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
    file.seekg(0);
    file.read((char*)buffer.data(), fileSize);
    file.close();

    vk::UniqueShaderModule shaderModule = createShaderModule(buffer);

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
    layoutInfo.pSetLayouts = layouts.data();
    layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
    layoutInfo.pPushConstantRanges = pushConstantRanges.data();
    pipelineLayout = device.createPipelineLayoutUnique(layoutInfo);

    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.layout = pipelineLayout.get();
    pipelineInfo.stage.stage = vk::ShaderStageFlagBits::eCompute;
    pipelineInfo.stage.module = shaderModule.get();
    pipelineInfo.stage.pName = "main";

    auto result = device.createComputePipelineUnique(nullptr, pipelineInfo);
    pipeline = std::move(result.value);
}

ComputePipeline::~ComputePipeline() {}

vk::UniqueShaderModule ComputePipeline::createShaderModule(const std::vector<uint32_t>& code) {
    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();
    return device.createShaderModuleUnique(createInfo);
}

} // namespace engine::renderer
