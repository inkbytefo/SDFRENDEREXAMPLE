#include "ComputePipeline.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace engine::renderer {

namespace fs = std::filesystem;

static fs::path executableDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return fs::path(buf).parent_path();
#elif defined(__linux__)
    std::error_code ec;
    auto p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
#endif
    return fs::current_path();
}

// Shaders are found next to the executable (shaders/), in the build tree's shader
// directory baked in at configure time, or relative to the working directory.
static std::vector<char> loadSpirv(const std::string& name) {
    std::vector<fs::path> candidates = {
        executableDir() / "shaders" / name,
#ifdef SDF_SHADER_DIR
        fs::path(SDF_SHADER_DIR) / name,
#endif
        fs::path("shaders") / name,
    };

    for (const auto& path : candidates) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) continue;
        size_t size = static_cast<size_t>(file.tellg());
        std::vector<char> data(size);
        file.seekg(0);
        file.read(data.data(), static_cast<std::streamsize>(size));
        return data;
    }

    std::string tried;
    for (const auto& p : candidates) tried += "\n  " + p.string();
    throw std::runtime_error("failed to find shader '" + name + "'. Looked in:" + tried);
}

ComputePipeline::ComputePipeline(vk::Device device, const std::string& shaderName,
                                 const std::vector<vk::DescriptorSetLayout>& layouts,
                                 const std::vector<vk::PushConstantRange>& pushConstantRanges)
    : device(device) {
    auto bytes = loadSpirv(shaderName);
    if (bytes.size() % 4 != 0) throw std::runtime_error("invalid SPIR-V size: " + shaderName);

    std::vector<uint32_t> code(bytes.size() / 4);
    std::memcpy(code.data(), bytes.data(), bytes.size());
    vk::UniqueShaderModule shaderModule = createShaderModule(code);

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
    if (result.result != vk::Result::eSuccess) throw std::runtime_error("failed to create compute pipeline: " + shaderName);
    pipeline = std::move(result.value);
}

ComputePipeline::~ComputePipeline() = default;

vk::UniqueShaderModule ComputePipeline::createShaderModule(const std::vector<uint32_t>& code) {
    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();
    return device.createShaderModuleUnique(createInfo);
}

} // namespace engine::renderer
