#pragma once
#include "world/system/ResourceSystem.hpp"
#include "zensim/ZpcImplPattern.hpp"
#include "zensim/vulkan/VkTexture.hpp"
#include "zensim/vulkan/Vulkan.hpp"

#define ZS_IMGUI_RENDERER_USE_STAGING_BUFFER 1

namespace zs {

  struct ImguiVkRenderer {
    ImguiVkRenderer(void *window, zs::VulkanContext &ctx, const RenderPass &rp,
                    vk::SampleCountFlagBits sampleBits, u32 numFrames);
    ~ImguiVkRenderer() = default;
    ImguiVkRenderer(ImguiVkRenderer &&o)
        : _window{zs::exchange(o._window, nullptr)}, 
          _ctx{o._ctx},
          _fontTexture{zs::move(o._fontTexture)},
          _fontGlyphs{zs::move(o._fontGlyphs)},
          _fontIndexLookup{zs::move(o._fontIndexLookup)},
          _bufferedData{zs::move(o._bufferedData)},
          _pipeline{zs::move(o._pipeline)},
          _fontDescriptorSet{o._fontDescriptorSet},
          _sampleBits{o._sampleBits} {
      o._fontDescriptorSet = VK_NULL_HANDLE;
    }

    struct PrimitiveBuffers {
      void reserveBuffers(VulkanContext &ctx, vk::DeviceSize vertexSize, vk::DeviceSize indexSize);

      u32 _numVertices{0}, _numIndices{0};
      Owner<Buffer> _vertexBuffer{}, _indexBuffer{};
      // _vertexStagingBuffer{}, _indexStagingBuffer{};
    };

    void rebuildFontTexture();
    void updateBuffers(u32 frameNo);
    void updateBuffers(PrimitiveBuffers &buffers, void *imDrawData);
    void renderFrame(u32 frameNo, vk::CommandBuffer cmd);
    void renderFrame(PrimitiveBuffers &buffers, vk::CommandBuffer cmd, void *imDrawData);
    u32 numBuffers() const noexcept { return (u32)_bufferedData.size(); }
    vk::SampleCountFlagBits getSampleBits() const noexcept { return _sampleBits; }

    const ShaderModule &getVertShader() const { return ResourceSystem::get_shader("imgui.vert"); }
    const ShaderModule &getFragShader() const { return ResourceSystem::get_shader("imgui.frag"); }
    VulkanContext &ctx() { return _ctx; }
    const VulkanContext &ctx() const { return _ctx; }

    void registerImage(const vk::DescriptorSet &pSet) const {
      ResourceSystem::register_image_for_gui(const_cast<vk::DescriptorSet *>(&pSet));
    }
    bool viewportRequireSceneRenderResults(void *imDrawData) const;
    bool viewportRequireSceneRenderResults() const;

  private:
    friend struct SceneEditor;
    void *_window;
    VulkanContext &_ctx;
    VkTexture _fontTexture;
    Owner<Buffer> _fontGlyphs, _fontIndexLookup;
    Owner<Pipeline> _pipeline;  // pipelineLayout, pipelineCache
    vk::DescriptorSet _fontDescriptorSet;
    vk::SampleCountFlagBits _sampleBits;
    // managed during per-frame update

    std::vector<PrimitiveBuffers> _bufferedData;
  };

}  // namespace zs