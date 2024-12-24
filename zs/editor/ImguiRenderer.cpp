#include "ImguiRenderer.hpp"

#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_glsl.hpp>
#include <stdexcept>

#include "GuiWindow.hpp"
#include "ImguiSystem.hpp"
#include "imgui.h"
#include "zensim/math/Vec.h"

namespace zs {

  static const char g_ui_vert_code[] = R"(
#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(push_constant) uniform PushConstants {
  vec2 scale;
  vec2 translate;
}
pushConstants;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;

out gl_PerVertex { vec4 gl_Position; };

void main() {
  outUV = inUV;
  outColor = inColor;
  gl_Position =
      vec4(inPos * pushConstants.scale + pushConstants.translate, 0.0, 1.0);
}
)";

  static const char g_ui_frag_code[] = R"(
#version 450

layout (binding = 0) uniform sampler2D fontSampler;

layout (location = 0) in vec2 inUV;
layout (location = 1) in vec4 inColor;

layout (location = 0) out vec4 outColor;

void main() 
{
	outColor = inColor * texture(fontSampler, inUV);
}
)";

  struct PushConstBlock {
    vec<float, 2> scale;
    vec<float, 2> translate;
  };  // pushConstBlock

  ImguiVkRenderer::ImguiVkRenderer(void *window, VulkanContext &ctx, const RenderPass &renderPass,
                                   vk::SampleCountFlagBits sampleBits, u32 numFrames)
      : _window{window},
        _ctx{ctx},
        _fontTexture{},
        _bufferedData(numFrames),
        _sampleBits{sampleBits} {
    (void)ImguiSystem::instance();
    rebuildFontTexture();

    // shaders
    ResourceSystem::load_shader(ctx, "imgui.vert", vk::ShaderStageFlagBits::eVertex,
                                g_ui_vert_code);
    ResourceSystem::load_shader(ctx, "imgui.frag", vk::ShaderStageFlagBits::eFragment,
                                g_ui_frag_code);
    auto &_vertShader = ResourceSystem::get_shader("imgui.vert");
    auto &_fragShader = ResourceSystem::get_shader("imgui.frag");
#if 0
  _vertShader = ctx.createShaderModuleFromGlsl(
      g_ui_vert_code, vk::ShaderStageFlagBits::eVertex, "imgui_ui.vert");
  _fragShader = ctx.createShaderModuleFromGlsl(
      g_ui_frag_code, vk::ShaderStageFlagBits::eFragment, "imgui_ui.frag");
#endif

    _ctx.acquireSet(_fragShader.layout(0), _fontDescriptorSet);

    zs::DescriptorWriter writer{ctx, _fragShader.layout(0)};
    vk::DescriptorImageInfo fontInfo{};
    fontInfo.sampler = _fontTexture.sampler;
    fontInfo.imageView = _fontTexture.image.get();
    fontInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    writer.writeImage(0, &fontInfo);
    writer.overwrite(_fontDescriptorSet);

    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->SetTexID((ImTextureID)(&_fontDescriptorSet));

    // pipeline
    std::vector<vk::VertexInputBindingDescription> vertexInputBindings
        = {vk::VertexInputBindingDescription{0, sizeof(ImDrawVert), vk::VertexInputRate::eVertex}};
    std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes = {
        vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32Sfloat,
                                            offsetof(ImDrawVert, pos)},  // Location 0: Position
        vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32Sfloat,
                                            offsetof(ImDrawVert, uv)},  // Location 1: UV
        vk::VertexInputAttributeDescription{2, 0, vk::Format::eR8G8B8A8Unorm,
                                            offsetof(ImDrawVert, col)},  // Location 2: Color
    };
    _pipeline = ctx.pipeline()
                    .setRasterizationSamples(_sampleBits)
                    .setRenderPass(renderPass, 0)
                    .setShader(_vertShader)
                    .setShader(_fragShader)
                    .setPushConstantRange(vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0,
                                                                sizeof(PushConstBlock)})
                    // overwrite input bindings/ attributes
                    .setBindingDescriptions(vertexInputBindings)
                    .setAttributeDescriptions(vertexInputAttributes)
                    .build();
  }

  void ImguiVkRenderer::rebuildFontTexture() {
    ImGuiIO &io = ImGui::GetIO();

    unsigned char *fontData;
    int texWidth, texHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
    vk::DeviceSize uploadSize = texWidth * texHeight * 4 * sizeof(char);
    _fontTexture
        = load_texture(_ctx, fontData, uploadSize, vk::Extent2D{(u32)texWidth, (u32)texHeight},
                       vk::Format::eR8G8B8A8Unorm);

    ImFont *font = (ImFont *)ImguiSystem::instance().getImguiFont(ImguiSystem::cn_font);

    // char c = '0';
    // for (int i = 0; i < 10; ++i)
    //   fmt::print("ch[{}] : {}\n", c + i, font->IndexLookup.Data[c + i]);
    ImFontGlyph numGlyphs[10];
    for (int i = 0; i < 10; ++i) numGlyphs[i] = font->Glyphs.Data[font->IndexLookup.Data['0' + i]];

    auto numGlyphBytes = sizeof(ImFontGlyph) * 10;
    auto stagingFontGlyphBuffer
        = _ctx.createStagingBuffer(numGlyphBytes, vk::BufferUsageFlagBits::eTransferSrc);
    stagingFontGlyphBuffer.map();
    memcpy(stagingFontGlyphBuffer.mappedAddress(), numGlyphs, numGlyphBytes);
    stagingFontGlyphBuffer.unmap();
    stagingFontGlyphBuffer.flush();

    auto numFontLUTBytes = sizeof(ImWchar) * font->IndexLookup.Size;
    auto stagingFontLUTBuffer
        = _ctx.createStagingBuffer(numFontLUTBytes, vk::BufferUsageFlagBits::eTransferSrc);
    stagingFontLUTBuffer.map();
    memcpy(stagingFontLUTBuffer.mappedAddress(), font->IndexLookup.Data, numFontLUTBytes);
    stagingFontLUTBuffer.unmap();
    stagingFontLUTBuffer.flush();

    auto &env = _ctx.env();
    auto &pool = env.pools(vk_queue_e::graphics);
    auto copyQueue = pool.queue;
    auto &cmd = static_cast<GUIWindow *>(_window)->currentCmd(0);

    _fontGlyphs = _ctx.createBuffer(
        numGlyphBytes,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal);
    _fontIndexLookup = _ctx.createBuffer(
        numFontLUTBytes,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal);
    cmd.begin();
    vk::BufferCopy copyRegion{};
    copyRegion.size = numGlyphBytes;
    (*cmd).copyBuffer(stagingFontGlyphBuffer, _fontGlyphs.get(), {copyRegion});

    copyRegion.size = numFontLUTBytes;
    (*cmd).copyBuffer(stagingFontLUTBuffer, _fontIndexLookup.get(), {copyRegion});
    cmd.end();

    // cmd.submit(fence, true, true);
    auto tmp = (vk::CommandBuffer)cmd;
    auto submitInfo = vk::SubmitInfo().setCommandBufferCount(1).setPCommandBuffers(&tmp);
    vk::Fence fence = _ctx.device.createFence(vk::FenceCreateInfo{}, nullptr, _ctx.dispatcher);
    // ctx.device.resetFences(1, &fence);
    auto res = copyQueue.submit(1, &submitInfo, fence, _ctx.dispatcher);
    if (_ctx.device.waitForFences(1, &fence, VK_TRUE, std::numeric_limits<u64>::max(),
                                  _ctx.dispatcher)
        != vk::Result::eSuccess)
      throw std::runtime_error("error waiting for fences");
    _ctx.device.destroyFence(fence, nullptr, _ctx.dispatcher);
    //
  }

  void ImguiVkRenderer::PrimitiveBuffers::reserveBuffers(VulkanContext &ctx,
                                                         vk::DeviceSize vertexSize,
                                                         vk::DeviceSize indexSize) {
    if (!_vertexBuffer || _vertexBuffer.get().getSize() < vertexSize) {
      // _vertexStagingBuffer
      //     = ctx.createStagingBuffer(vertexSize, vk::BufferUsageFlagBits::eTransferSrc);
      _vertexBuffer = ctx.createBuffer(
          vertexSize, vk::BufferUsageFlagBits::eVertexBuffer,
          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eDeviceLocal);
    }
    if (!_indexBuffer || _indexBuffer.get().getSize() < indexSize) {
      // _indexStagingBuffer
      //     = ctx.createStagingBuffer(indexSize, vk::BufferUsageFlagBits::eTransferSrc);
      _indexBuffer = ctx.createBuffer(
          indexSize, vk::BufferUsageFlagBits::eIndexBuffer,
          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eDeviceLocal);
    }
  }

  void ImguiVkRenderer::updateBuffers(PrimitiveBuffers &buffers, void *imDrawData_) {
    // ImDrawData *imDrawData = ImGui::GetDrawData();
    ImDrawData *imDrawData = (ImDrawData *)imDrawData_;
    vk::DeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
    vk::DeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);
    if ((vertexBufferSize == 0) || (indexBufferSize == 0)) return;

#if ZS_IMGUI_RENDERER_USE_STAGING_BUFFER
    buffers.reserveBuffers(_ctx, vertexBufferSize, indexBufferSize);

    // prepare staging buffers
    buffers._vertexBuffer.get().map();
    buffers._indexBuffer.get().map();
    ImDrawVert *vtxDst = (ImDrawVert *)buffers._vertexBuffer.get().mappedAddress();
    ImDrawIdx *idxDst = (ImDrawIdx *)buffers._indexBuffer.get().mappedAddress();

    for (int n = 0; n < imDrawData->CmdListsCount; n++) {
      const ImDrawList *cmd_list = imDrawData->CmdLists[n];
      memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
      memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
      vtxDst += cmd_list->VtxBuffer.Size;
      idxDst += cmd_list->IdxBuffer.Size;
    }
    buffers._vertexBuffer.get().unmap();
    buffers._indexBuffer.get().unmap();
    buffers._vertexBuffer.get().flush();
    buffers._indexBuffer.get().flush();

    // prepare buffers
#  if 0
    auto &env = _ctx.env();
    auto &pool = env.pools(vk_queue_e::graphics);
    auto copyQueue = pool.queue;

    auto &cmd = static_cast<GUIWindow *>(_window)->currentCmd();

    cmd.begin(vk::CommandBufferBeginInfo{});
    auto bufferCopyRegion = vk::BufferCopy{};
    bufferCopyRegion.size = vertexBufferSize;
    (*cmd).copyBuffer(buffers._vertexStagingBuffer.get(), buffers._vertexBuffer.get(),
                   {bufferCopyRegion}, _ctx.dispatcher);
    bufferCopyRegion.size = indexBufferSize;
    (*cmd).copyBuffer(buffers._indexStagingBuffer.get(), buffers._indexBuffer.get(),
                   {bufferCopyRegion}, _ctx.dispatcher);

    cmd.end();

    auto tmp = (vk::CommandBuffer)cmd;
    auto submitInfo = vk::SubmitInfo().setCommandBufferCount(1).setPCommandBuffers(&tmp);
    vk::Fence fence = _ctx.device.createFence(vk::FenceCreateInfo{}, nullptr, _ctx.dispatcher);
    // ctx.device.resetFences(1, &fence);
    auto res = copyQueue.submit(1, &submitInfo, fence, _ctx.dispatcher);
    if (_ctx.device.waitForFences(1, &fence, VK_TRUE, std::numeric_limits<u64>::max(),
                                  _ctx.dispatcher)
        != vk::Result::eSuccess)
      throw std::runtime_error("error waiting for fences");
    _ctx.device.destroyFence(fence, nullptr, _ctx.dispatcher);
#  endif
#else
    ctx.waitIdle();

    // Update buffers only if vertex or index count has been changed compared to
    // current buffer size

    // Vertex buffer
    if (!vertexBuffer || (vertexCount != imDrawData->TotalVtxCount)) {
      if (vertexBuffer) {
        vertexBuffer->unmap();
        vertexBuffer.reset();
      }
      vertexBuffer = std::make_unique<zs::Buffer>(
          ctx.createBuffer(vertexBufferSize, vk::BufferUsageFlagBits::eVertexBuffer,
                           vk::MemoryPropertyFlagBits::eHostVisible));
      vertexCount = imDrawData->TotalVtxCount;
      vertexBuffer->map();
    }

    // Index buffer
    if (!indexBuffer || (indexCount < imDrawData->TotalIdxCount)) {
      if (indexBuffer) {
        indexBuffer->unmap();
        indexBuffer.reset();
      }
      indexBuffer = std::make_unique<zs::Buffer>(
          ctx.createBuffer(indexBufferSize, vk::BufferUsageFlagBits::eIndexBuffer,
                           vk::MemoryPropertyFlagBits::eHostVisible));
      indexCount = imDrawData->TotalIdxCount;
      indexBuffer->map();
    }

    // Upload data
    ImDrawVert *vtxDst = (ImDrawVert *)vertexBuffer->mappedAddress();
    ImDrawIdx *idxDst = (ImDrawIdx *)indexBuffer->mappedAddress();

    for (int n = 0; n < imDrawData->CmdListsCount; n++) {
      const ImDrawList *cmd_list = imDrawData->CmdLists[n];
      memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
      memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
      vtxDst += cmd_list->VtxBuffer.Size;
      idxDst += cmd_list->IdxBuffer.Size;
    }

    // Flush to make writes visible to GPU
    vertexBuffer->flush();
    indexBuffer->flush();
#endif
  }
  void ImguiVkRenderer::updateBuffers(u32 frameNo) {
    if (frameNo >= _bufferedData.size())
      throw std::runtime_error(
          fmt::format("updating buffer [{}], which exceeds the total number of "
                      "reserved frames [{}].",
                      frameNo, _bufferedData.size()));

    PrimitiveBuffers &buffers = _bufferedData[frameNo];
    updateBuffers(buffers, ImGui::GetDrawData());
  }

  bool ImguiVkRenderer::viewportRequireSceneRenderResults(void *imDrawData_) const {
    ImGuiIO &io = ImGui::GetIO();
    ImDrawData *imDrawData = (ImDrawData *)imDrawData_;
    if (imDrawData->CmdListsCount > 0) {
      for (int32_t i = 0; i < imDrawData->CmdListsCount; i++) {
        const ImDrawList *cmd_list = imDrawData->CmdLists[i];
        for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++) {
          const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[j];

          if (ResourceSystem::image_avail_for_gui((void *)pcmd->TextureId)) return true;
        }
      }
    }
    return false;
  }
  bool ImguiVkRenderer::viewportRequireSceneRenderResults() const {
    return viewportRequireSceneRenderResults(ImGui::GetDrawData());
  }
  void ImguiVkRenderer::renderFrame(PrimitiveBuffers &buffers, vk::CommandBuffer cmd,
                                    void *imDrawData_) {
    ImGuiIO &io = ImGui::GetIO();
    ImDrawData *imDrawData = (ImDrawData *)imDrawData_;

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline.get());

    // ImDrawData *imDrawData = ImGui::GetDrawData();
    int fbWidth = (int)(imDrawData->DisplaySize.x * imDrawData->FramebufferScale.x);
    int fbHeight = (int)(imDrawData->DisplaySize.y * imDrawData->FramebufferScale.y);
    if (fbWidth <= 0 || fbHeight <= 0) return;

    auto viewport = vk::Viewport()
                        .setWidth((float)fbWidth)
                        .setHeight((float)fbHeight)
                        .setMinDepth(0.0f)
                        .setMaxDepth(1.0f);
    cmd.setViewport(0, {viewport});

    // UI scale and translate via push constants
    PushConstBlock pushConstBlock;
    pushConstBlock.scale
        = vec<float, 2>{2.0f / imDrawData->DisplaySize.x, 2.0f / imDrawData->DisplaySize.y};
    pushConstBlock.translate = vec<float, 2>::constant(-1.0f)
                               - vec<float, 2>{imDrawData->DisplayPos.x * pushConstBlock.scale[0],
                                               imDrawData->DisplayPos.y * pushConstBlock.scale[1]};
    cmd.pushConstants(_pipeline.get(), vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushConstBlock),
                      &pushConstBlock);

    // Render commands
    int32_t vertexOffset = 0;
    int32_t indexOffset = 0;

    ImVec2 clipOff = imDrawData->DisplayPos;
    ImVec2 clipScale = imDrawData->FramebufferScale;
    if (imDrawData->CmdListsCount > 0) {
      vk::Buffer bufs[] = {buffers._vertexBuffer.get()};
      vk::DeviceSize offsets[1] = {0};
      cmd.bindVertexBuffers(/*firstBinding*/ 0, bufs, offsets, _ctx.dispatcher);
      cmd.bindIndexBuffer({buffers._indexBuffer.get()}, /*offset*/ 0, vk::IndexType::eUint16,
                          _ctx.dispatcher);

      for (int32_t i = 0; i < imDrawData->CmdListsCount; i++) {
        const ImDrawList *cmd_list = imDrawData->CmdLists[i];
        for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++) {
          const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[j];

          ImVec2 clipMin((pcmd->ClipRect.x - clipOff.x) * clipScale.x,
                         (pcmd->ClipRect.y - clipOff.y) * clipScale.y);
          ImVec2 clipMax((pcmd->ClipRect.z - clipOff.x) * clipScale.x,
                         (pcmd->ClipRect.w - clipOff.y) * clipScale.y);
          if (clipMin.x < 0.0f) {
            clipMin.x = 0.0f;
          }
          if (clipMin.y < 0.0f) {
            clipMin.y = 0.0f;
          }
          if (clipMax.x > fbWidth) {
            clipMax.x = (float)fbWidth;
          }
          if (clipMax.y > fbHeight) {
            clipMax.y = (float)fbHeight;
          }
          if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) continue;

          vk::Rect2D scissorRect;
          scissorRect.offset.x = (int32_t)clipMin.x;
          scissorRect.offset.y = (int32_t)clipMin.y;
          scissorRect.extent.width = (uint32_t)(clipMax.x - clipMin.x);
          scissorRect.extent.height = (uint32_t)(clipMax.y - clipMin.y);
          cmd.setScissor(0, {scissorRect});

          auto pImageDescriptor = (vk::DescriptorSet *)pcmd->TextureId;
          // if (pImageDescriptor != nullptr) {
          // if (false) {
          if (ResourceSystem::image_avail_for_gui((void *)pImageDescriptor)) {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   /*pipeline layout*/ _pipeline.get(),
                                   /*firstSet*/ 0,
                                   /*descriptor sets*/ {*pImageDescriptor},
                                   /*dynamic offset*/ {}, _ctx.dispatcher);
          } else
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   /*pipeline layout*/ _pipeline.get(),
                                   /*firstSet*/ 0,
                                   /*descriptor sets*/ {_fontDescriptorSet},
                                   /*dynamic offset*/ {}, _ctx.dispatcher);

          cmd.drawIndexed(/*index count*/ pcmd->ElemCount,
                          /*instance count*/ 1,
                          /*first index*/ pcmd->IdxOffset + indexOffset,
                          /*vertex offset*/ pcmd->VtxOffset + vertexOffset,
                          /*first instance*/ 0, _ctx.dispatcher);
        }
        indexOffset += cmd_list->IdxBuffer.Size;
        vertexOffset += cmd_list->VtxBuffer.Size;
      }
    }
  }
  void ImguiVkRenderer::renderFrame(u32 frameNo, vk::CommandBuffer cmd) {
    renderFrame(_bufferedData[frameNo], cmd, ImGui::GetDrawData());
  }

}  // namespace zs