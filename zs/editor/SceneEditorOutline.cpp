#include "SceneEditor.hpp"

namespace zs {
  static const char g_outline_base_vert_code[] = R"(
#version 450

layout (location = 0) in vec3 inPos;
layout (set = 0, binding = 0) uniform SceneCamera {
    mat4 projection;
    mat4 view;
} camera;
layout (push_constant) uniform Consts {
    mat4 model;
} params;

void main() {
    gl_Position = camera.projection * camera.view * params.model * vec4(inPos, 1.0);
}
    )";
  static const char g_outline_base_frag_code[] = R"(
#version 450
layout (location = 0) out vec4 outFragColor;
void main() {
    outFragColor = vec4(0.0, 0.0, 0.0, 0.5);
}
    )";

  static const char g_outline_vert_code[] = R"(
#version 450
const vec3 positions[4] = {
    vec3(-1.0, -1.0, 0.0),
    vec3(-1.0, 1.0, 0.0),
    vec3(1.0, 1.0, 0.0),
    vec3(1.0, -1.0, 0.0)
};

const vec2 uvs[4] = {
    vec2(0.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 1.0),
    vec2(1.0, 0.0)
};

layout(location = 0) out vec2 uv;

void main() 
{
    gl_Position = vec4(positions[gl_VertexIndex], 1.0);
    uv = uvs[gl_VertexIndex];
}
    )";
  static const char g_outline_frag_code[] = R"(
#version 450

layout (binding = 0) uniform sampler2D lastFrame;

layout(location = 0) in vec2 uv;

layout (location = 0) out vec4 outFragColor;

layout (push_constant) uniform Consts {
    vec2 deltaUV;
    float r, g, b;
} params;

void main()
{
    float localPixelA = texture(lastFrame, uv).a;
    // inner pixels are marked channel A = 0.5, discard them
    if (localPixelA > 0.4 && localPixelA < 0.6) discard;

    float result = 0.0;
    for (int i=-1; i<=1; ++i){
        for (int j=-1; j<=1; ++j){
            result += texture(lastFrame, uv + params.deltaUV * vec2(i, j)).a;
        }
    }
    if (result < 0.1) discard;
    outFragColor = vec4(params.r, params.g, params.b, 1.0);
}
    )";

  void SceneEditor::setupOutlineResources() {
    auto& ctx = this->ctx();

    sceneOutlineRenderer.sampler
        = ctx.createSampler(vk::SamplerCreateInfo{}
                                .setMaxAnisotropy(1.f)
                                .setMagFilter(vk::Filter::eNearest)
                                .setMinFilter(vk::Filter::eNearest)
                                .setMipmapMode(vk::SamplerMipmapMode::eNearest)
                                .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
                                .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
                                .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
                                .setCompareOp(vk::CompareOp::eNever)
                                .setBorderColor(vk::BorderColor::eFloatTransparentBlack));

    ResourceSystem::load_shader(ctx, "default_outline_base_vert", vk::ShaderStageFlagBits::eVertex,
                                g_outline_base_vert_code);
    ResourceSystem::load_shader(ctx, "default_outline_base_frag",
                                vk::ShaderStageFlagBits::eFragment, g_outline_base_frag_code);
    auto& outlineBaseVertShader = ResourceSystem::get_shader("default_outline_base_vert");
    auto& outlineBaseFragShader = ResourceSystem::get_shader("default_outline_base_frag");

    ResourceSystem::load_shader(ctx, "default_outline_vert", vk::ShaderStageFlagBits::eVertex,
                                g_outline_vert_code);
    ResourceSystem::load_shader(ctx, "default_outline_frag", vk::ShaderStageFlagBits::eFragment,
                                g_outline_frag_code);
    auto& outlineVertShader = ResourceSystem::get_shader("default_outline_vert");
    auto& outlineFragShader = ResourceSystem::get_shader("default_outline_frag");
    ctx.acquireSet(outlineFragShader.layout(0), sceneOutlineRenderer.outlineImageDescriptorSet);
    ctx.acquireSet(outlineFragShader.layout(0), sceneOutlineRenderer.outlineSwapImageDescriptorSet);

    // base render pass
    {
      auto rpBuilder = ctx.renderpass().setNumPasses(1);
      rpBuilder
          .addAttachment(colorFormat, vk::ImageLayout::eUndefined,
                         vk::ImageLayout::eShaderReadOnlyOptimal, true, vk::SampleCountFlagBits::e1)
          .addSubpass({0}, /*depthStencilRef*/ -1, /*colorResolveRef*/ {},
                      /*depthStencilResolveRef*/ -1, /*inputAttachments*/ {});
      rpBuilder.setSubpassDependencies(
          {vk::SubpassDependency2{}
               .setSrcSubpass(VK_SUBPASS_EXTERNAL)
               .setDstSubpass(0)
               .setSrcAccessMask(vk::AccessFlagBits::eMemoryRead)
               .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentRead
                                 | vk::AccessFlagBits::eColorAttachmentWrite)
               .setSrcStageMask(vk::PipelineStageFlagBits::eBottomOfPipe)
               .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
               .setDependencyFlags(vk::DependencyFlagBits::eByRegion),
           vk::SubpassDependency2{}
               .setSrcSubpass(0)
               .setDstSubpass(VK_SUBPASS_EXTERNAL)
               .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentRead
                                 | vk::AccessFlagBits::eColorAttachmentWrite)
               .setDstAccessMask(vk::AccessFlagBits::eMemoryRead)
               .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
               .setDstStageMask(vk::PipelineStageFlagBits::eBottomOfPipe)
               .setDependencyFlags(vk::DependencyFlagBits::eByRegion)});
      sceneOutlineRenderer.baseRenderPass = rpBuilder.build();
    }

    // outline render pass with step=2
    {
      auto rpBuilder = ctx.renderpass().setNumPasses(1);
      rpBuilder
          .addAttachment(colorFormat, vk::ImageLayout::eUndefined,
                         vk::ImageLayout::eShaderReadOnlyOptimal, true, vk::SampleCountFlagBits::e1)
          .addSubpass({0}, /*depthStencilRef*/ -1, /*colorResolveRef*/ {},
                      /*depthStencilResolveRef*/ -1, /*inputAttachments*/ {});
      rpBuilder.setSubpassDependencies(
          {vk::SubpassDependency2{}
               .setSrcSubpass(VK_SUBPASS_EXTERNAL)
               .setDstSubpass(0)
               .setSrcAccessMask(vk::AccessFlagBits::eMemoryRead)
               .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentRead
                                 | vk::AccessFlagBits::eColorAttachmentWrite)
               .setSrcStageMask(vk::PipelineStageFlagBits::eBottomOfPipe)
               .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
               .setDependencyFlags(vk::DependencyFlagBits::eByRegion),
           vk::SubpassDependency2{}
               .setSrcSubpass(0)
               .setDstSubpass(VK_SUBPASS_EXTERNAL)
               .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentRead
                                 | vk::AccessFlagBits::eColorAttachmentWrite)
               .setDstAccessMask(vk::AccessFlagBits::eMemoryRead)
               .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
               .setDstStageMask(vk::PipelineStageFlagBits::eBottomOfPipe)
               .setDependencyFlags(vk::DependencyFlagBits::eByRegion)});
      sceneOutlineRenderer.renderPass = rpBuilder.build();
    }

    // outline render pass with step=1
    {
      auto rpBuilder = ctx.renderpass().setNumPasses(1);
      rpBuilder
          .addAttachment(colorFormat, vk::ImageLayout::eShaderReadOnlyOptimal,
                         vk::ImageLayout::eShaderReadOnlyOptimal, false,
                         vk::SampleCountFlagBits::e1)
          .addSubpass({0}, /*depthStencilRef*/ -1, /*colorResolveRef*/ {},
                      /*depthStencilResolveRef*/ -1, /*inputAttachments*/ {});
      rpBuilder.setSubpassDependencies(
          {vk::SubpassDependency2{}
               .setSrcSubpass(VK_SUBPASS_EXTERNAL)
               .setDstSubpass(0)
               .setSrcAccessMask(vk::AccessFlagBits::eMemoryRead)
               .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentRead
                                 | vk::AccessFlagBits::eColorAttachmentWrite)
               .setSrcStageMask(vk::PipelineStageFlagBits::eBottomOfPipe)
               .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
               .setDependencyFlags(vk::DependencyFlagBits::eByRegion),
           vk::SubpassDependency2{}
               .setSrcSubpass(0)
               .setDstSubpass(VK_SUBPASS_EXTERNAL)
               .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentRead
                                 | vk::AccessFlagBits::eColorAttachmentWrite)
               .setDstAccessMask(vk::AccessFlagBits::eMemoryRead)
               .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
               .setDstStageMask(vk::PipelineStageFlagBits::eBottomOfPipe)
               .setDependencyFlags(vk::DependencyFlagBits::eByRegion)});
      sceneOutlineRenderer.swapRenderPass = rpBuilder.build();
    }

    sceneOutlineRenderer.outlineBasePipeline
        = ctx.pipeline()
              .setRenderPass(sceneOutlineRenderer.renderPass.get(), 0)
              .setRasterizationSamples(vk::SampleCountFlagBits::e1)
              .setCullMode(vk::CullModeFlagBits::eBack)
              .setTopology(vk::PrimitiveTopology::eTriangleList)
              .setBlendEnable(false)
              .setShader(outlineBaseVertShader)
              .setShader(outlineBaseFragShader)
              .setPushConstantRanges(
                  {vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)}})
              .setBindingDescriptions(VkModel::get_binding_descriptions(VkModel::tri))
              .setAttributeDescriptions(VkModel::get_attribute_descriptions(VkModel::tri))
              .build();

    sceneOutlineRenderer.outlinePipeline
        = ctx.pipeline()
              .setRenderPass(sceneOutlineRenderer.renderPass.get(), 0)
              .setRasterizationSamples(vk::SampleCountFlagBits::e1)
              .setCullMode(vk::CullModeFlagBits::eNone)
              .setTopology(vk::PrimitiveTopology::eTriangleFan)
              .setShader(outlineVertShader)
              .setShader(outlineFragShader)
              .setPushConstantRanges({vk::PushConstantRange{vk::ShaderStageFlagBits::eFragment, 0,
                                                            sizeof(glm::vec2) + sizeof(float) * 3}})
              .build();

    sceneOutlineRenderer.outlineSwapPipeline
        = ctx.pipeline()
              .setRenderPass(sceneOutlineRenderer.swapRenderPass.get(), 0)
              .setRasterizationSamples(vk::SampleCountFlagBits::e1)
              .setCullMode(vk::CullModeFlagBits::eNone)
              .setTopology(vk::PrimitiveTopology::eTriangleFan)
              .setShader(outlineVertShader)
              .setShader(outlineFragShader)
              .setPushConstantRanges({vk::PushConstantRange{vk::ShaderStageFlagBits::eFragment, 0,
                                                            sizeof(glm::vec2) + sizeof(float) * 3}})
              .build();

    rebuildOutlineFbo();
  }

  void SceneEditor::rebuildOutlineFbo() {
    sceneOutlineRenderer.outlineImage
        = ctx().create2DImage(vkCanvasExtent, colorFormat,
                              /* combined image sampler */ vk::ImageUsageFlagBits::eSampled
                                  | vk::ImageUsageFlagBits::eColorAttachment |
                                  /* input attachment */ vk::ImageUsageFlagBits::eInputAttachment);
    sceneOutlineRenderer.outlineSwapImage
        = ctx().create2DImage(vkCanvasExtent, colorFormat,
                              /* combined image sampler */ vk::ImageUsageFlagBits::eSampled
                                  | vk::ImageUsageFlagBits::eColorAttachment |
                                  /* input attachment */ vk::ImageUsageFlagBits::eInputAttachment);

    sceneOutlineRenderer.baseFBO
        = ctx().createFramebuffer({(vk::ImageView)sceneOutlineRenderer.outlineImage.get()},
                                  vkCanvasExtent, sceneOutlineRenderer.baseRenderPass.get());
    sceneOutlineRenderer.FBO
        = ctx().createFramebuffer({(vk::ImageView)sceneOutlineRenderer.outlineSwapImage.get()},
                                  vkCanvasExtent, sceneOutlineRenderer.renderPass.get());
    sceneOutlineRenderer.swapFBO
        = ctx().createFramebuffer({(vk::ImageView)sceneAttachments.color.get()}, vkCanvasExtent,
                                  sceneOutlineRenderer.swapRenderPass.get());

    vk::DescriptorImageInfo imageInfo{};
    imageInfo.sampler = sceneOutlineRenderer.sampler.get();
    imageInfo.imageView = sceneOutlineRenderer.outlineImage.get();
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx().writeDescriptorSet(imageInfo, sceneOutlineRenderer.outlineImageDescriptorSet,
                             vk::DescriptorType::eCombinedImageSampler,
                             /*binding*/ 0);

    imageInfo.imageView = sceneOutlineRenderer.outlineSwapImage.get();
    ctx().writeDescriptorSet(imageInfo, sceneOutlineRenderer.outlineSwapImageDescriptorSet,
                             vk::DescriptorType::eCombinedImageSampler,
                             /*binding*/ 0);
  }

  void SceneEditor::_renderOutlineForOneModel(VulkanContext& ctx, VkCommand& cmd,
                                              const VkModel& model, const glm::mat4& transform,
                                              const float* outlineColor) {
    vk::Rect2D rect = vk::Rect2D(vk::Offset2D(), vkCanvasExtent);
    std::array<vk::ClearValue, 1> clearValues{};
    clearValues[0].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};
    /*
    First render pass, render base model
    */
    {
      auto renderPassInfo = vk::RenderPassBeginInfo()
                                .setRenderPass(sceneOutlineRenderer.baseRenderPass.get())
                                .setFramebuffer(sceneOutlineRenderer.baseFBO.get())
                                .setRenderArea(rect)
                                .setClearValueCount((zs::u32)clearValues.size())
                                .setPClearValues(clearValues.data());
      (*cmd).beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

      // setup viewport
      {
        auto viewport
            = vk::Viewport()
                  .setX(0 /*offsetx*/)
                  .setY(vkCanvasExtent.height /*-offsety*/)
                  .setWidth(float(vkCanvasExtent.width))
                  .setHeight(-float(vkCanvasExtent.height))  // negative viewport, opengl conformant
                  .setMinDepth(0.0f)
                  .setMaxDepth(1.0f);
        (*cmd).setViewport(0, {viewport});
        (*cmd).setScissor(0, {vk::Rect2D(vk::Offset2D(), vkCanvasExtent)});
      }

      // render outline for chosen model
      (*cmd).bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                /*pipeline layout*/ sceneOutlineRenderer.outlineBasePipeline.get(),
                                /*firstSet*/ 0,
                                /*descriptor sets*/ {sceneRenderData.sceneCameraSet},
                                /*dynamic offset*/ {0}, ctx.dispatcher);
      (*cmd).bindPipeline(vk::PipelineBindPoint::eGraphics,
                          sceneOutlineRenderer.outlineBasePipeline.get());
#if 0
            auto transform = model.useTransform
                ? get_transform(model.transform)
                : get_transform(model.translate, model.rotate, model.scale);
#endif
      (*cmd).pushConstants(sceneOutlineRenderer.outlineBasePipeline.get(),
                           vk::ShaderStageFlagBits::eVertex, 0, sizeof(transform), &transform);
      model.bind(*cmd, VkModel::tri);
      model.draw(*cmd, VkModel::tri);
      (*cmd).endRenderPass();
    }

    /*
    Second render pass, render JFA outline at step 2
    */
    {
      auto renderPassInfo = vk::RenderPassBeginInfo()
                                .setRenderPass(sceneOutlineRenderer.renderPass.get())
                                .setFramebuffer(sceneOutlineRenderer.FBO.get())
                                .setRenderArea(rect)
                                .setClearValueCount((zs::u32)clearValues.size())
                                .setPClearValues(clearValues.data());
      (*cmd).beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

      (*cmd).bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics,
          /*pipeline layout*/ sceneOutlineRenderer.outlinePipeline.get(),
          /*firstSet*/ 0,
          /*descriptor sets*/ {sceneOutlineRenderer.outlineImageDescriptorSet},
          /*dynamic offset*/ {}, ctx.dispatcher);
      (*cmd).bindPipeline(vk::PipelineBindPoint::eGraphics,
                          sceneOutlineRenderer.outlinePipeline.get());
      glm::vec2 deltaUV = glm::vec2(1.0f / vkCanvasExtent.width, 1.0f / vkCanvasExtent.height);
      (*cmd).pushConstants(sceneOutlineRenderer.outlinePipeline.get(),
                           vk::ShaderStageFlagBits::eFragment, 0, sizeof(deltaUV), &deltaUV);
      (*cmd).pushConstants(sceneOutlineRenderer.outlinePipeline.get(),
                           vk::ShaderStageFlagBits::eFragment, sizeof(deltaUV), sizeof(float) * 3,
                           outlineColor);
      (*cmd).draw(4, 1, 0, 0, ctx.dispatcher);
      (*cmd).endRenderPass();
    }

    // /*
    // Third render pass, render JFA outline at step 1 and output to the target
    // */
    {
      auto renderPassInfo = vk::RenderPassBeginInfo()
                                .setRenderPass(sceneOutlineRenderer.swapRenderPass.get())
                                .setFramebuffer(sceneOutlineRenderer.swapFBO.get())
                                .setRenderArea(rect)
                                .setClearValueCount((zs::u32)clearValues.size())
                                .setPClearValues(clearValues.data());
      (*cmd).beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

      (*cmd).bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics,
          /*pipeline layout*/ sceneOutlineRenderer.outlineSwapPipeline.get(),
          /*firstSet*/ 0,
          /*descriptor sets*/ {sceneOutlineRenderer.outlineSwapImageDescriptorSet},
          /*dynamic offset*/ {}, ctx.dispatcher);
      (*cmd).bindPipeline(vk::PipelineBindPoint::eGraphics,
                          sceneOutlineRenderer.outlineSwapPipeline.get());
      glm::vec2 deltaUV = glm::vec2(1.0f / vkCanvasExtent.width, 1.0f / vkCanvasExtent.height);
      (*cmd).pushConstants(sceneOutlineRenderer.outlineSwapPipeline.get(),
                           vk::ShaderStageFlagBits::eFragment, 0, sizeof(deltaUV), &deltaUV);
      (*cmd).pushConstants(sceneOutlineRenderer.outlineSwapPipeline.get(),
                           vk::ShaderStageFlagBits::eFragment, sizeof(deltaUV), sizeof(float) * 3,
                           outlineColor);
      (*cmd).draw(4, 1, 0, 0, ctx.dispatcher);

      (*cmd).endRenderPass();
    }
  }

  void SceneEditor::renderOutline() {
    // render outline for hovered model
    // size_t modelSize = sceneRenderData.models.size();
    size_t modelSize = getCurrentScene().numPrimitives();

    auto hoveredPrim = hoveredPrimPtr.lock();
    auto focusPrim = focusPrimPtr.lock();
    bool renderHoveredModel = (hoveredPrim &&
                               // !sceneRenderData.models[hoveredObjId].isParticle()
                               true);
    bool renderFocusedModel = (focusPrim &&
                               // !sceneRenderData.models[focusObjId].isParticle()
                               true);
    if (!renderHoveredModel && !renderFocusedModel) return;

    auto& ctx = this->ctx();
    fence.get().wait();
    auto& cmd = this->cmd.get();
    cmd.begin();

    // render outline for hovered and focused models
    if (renderFocusedModel && hoveredPrim != focusPrim) {
      auto pModel = focusPrim->queryVkTriMesh(ctx, sceneRenderData.currentTimeCode);
      if (pModel && currentVisiblePrimsDrawn.at(focusPrim.get())) {
        const auto& model = *pModel;

        float outlineColor[3] = {0.1, 1.0, 0.1};
        _renderOutlineForOneModel(
            ctx, cmd,
            /*sceneRenderData.models[focusObjId]*/ /*focusPrim->vkTriMesh(ctx)*/ model,
            // focusPrim->visualTransform(sceneRenderData.currentTimeCode),
            focusPrim->currentTimeVisualTransform(), outlineColor);
      }
    }
    if (renderHoveredModel) {
      auto pModel = hoveredPrim->queryVkTriMesh(ctx, sceneRenderData.currentTimeCode);
      if (pModel && currentVisiblePrimsDrawn.at(hoveredPrim.get())) {
        const auto& model = *pModel;

        float outlineColor[3] = {1.0, 1.0, 0.1};
        _renderOutlineForOneModel(
            ctx, cmd,
            /*sceneRenderData.models[hoveredObjId]*/ /*hoveredPrim->vkTriMesh(ctx)*/ model,
            // hoveredPrim->visualTransform(sceneRenderData.currentTimeCode),
            hoveredPrim->currentTimeVisualTransform(), outlineColor);
      }
    }

    (*cmd).end();
    cmd.submit(fence.get(), /*reset fence*/ true, /*reset config*/ true);

    fence.get().wait();
  }
}  // namespace zs
