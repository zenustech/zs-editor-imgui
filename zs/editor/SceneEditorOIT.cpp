#include "SceneEditor.hpp"

// Weighted Order-Independent Transparency
// ref: Morgan McGuire and Louis Bavoil, Weighted Blended Order-Independent Transparency, Journal of
// Computer Graphics Techniques (JCGT), vol. 2, no. 2, 122-141, 2013
// https://jcgt.org/published/0002/02/09/

namespace zs {
  static const char g_blend_mesh_vert_code[] = R"(
#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inColor;
// layout (location = 3) in vec2 inUV;
// layout (location = 3) in uint inVid;

layout (set = 0, binding = 0) uniform SceneCamera {
	mat4 projection;
	mat4 view;
} cameraUbo;

layout (push_constant) uniform Model {
 	mat4 model;
} params;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec3 outViewVec;
layout (location = 3) out vec3 outLightVec;

void main() 
{
	outColor = inColor;
	vec4 viewPos = cameraUbo.view * params.model * vec4(inPos, 1.0);
	gl_Position = cameraUbo.projection * viewPos;

  // outUV = inUV;
	outNormal = normalize(cameraUbo.view * params.model * vec4(inNormal, 0.0f)).xyz;

	vec3 lightPos = vec3(0.0f);
	outLightVec = lightPos.xyz - viewPos.xyz;
	outViewVec = viewPos.xyz - vec3(0.0f);
}
)";
  static const char g_blend_mesh_frag_code[] = R"(
#version 450

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
// layout (location = 2) in vec2 inUV;
layout (location = 2) in vec3 inViewVec;
layout (location = 3) in vec3 inLightVec;

layout (push_constant) uniform fragmentPushConstants {
    layout(offset = 16 * 4) int objId;
} pushConstant;

layout (location = 0) out vec4 outFragColor;
layout (location = 1) out vec4 outDiv;

float getWeight(float z, float alpha){
	return alpha * max(0.01, 300.0 * z * z * z); // reversed-z
}

void main() 
{
	vec3 N = normalize(inNormal);
	vec3 L = normalize(inLightVec);
	vec3 V = normalize(inViewVec);
	vec3 ambient = vec3(0.2);
	vec3 diffuse = max(dot(N, L), 0.0) * vec3(1.0);

  vec3 clr = (ambient + diffuse) * inColor.rgb;
	float alpha = 0.5;
	float weight = getWeight(gl_FragCoord.z, alpha);

	outFragColor = vec4(clr * weight, alpha);
	outDiv = vec4(alpha * weight, 0.0, 0.0, 0.0);
}
)";
  static const char g_post_blend_vert_code[] = R"(
#version 450

const vec2 positions[4] = {
    vec2(-1.0, -1.0),
    vec2(1.0, -1.0),
    vec2(1.0, 1.0),
    vec2(-1.0, 1.0),
};

const vec2 UVs[4] = {
	vec2(0.0, 1.0),
	vec2(1.0, 1.0),
	vec2(1.0, 0.0),
	vec2(0.0, 0.0),
};

layout (location = 0) out vec2 outUV;

void main() {
	gl_Position = vec4(positions[gl_VertexIndex], 1.0, 1.0); // reversed-z
	outUV = UVs[gl_VertexIndex];
}
)";
  static const char g_post_blend_frag_code[] = R"(
#version 450

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outFragColor;

layout (binding = 0) uniform sampler2D accum0;
layout (binding = 1) uniform sampler2D accum1;

void main() {
	outFragColor = texture(accum0, inUV);
	outFragColor.rgb /= clamp(texture(accum1, inUV).r, 1e-4, 5e4);
	outFragColor.a = 1.0 - outFragColor.a;
}
)";

  void SceneEditor::setupOITResources() {
    auto& ctx = this->ctx();

    sceneOITRenderer.sampler
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

    ResourceSystem::load_shader(ctx, "accum_blend_vert", vk::ShaderStageFlagBits::eVertex,
                                g_blend_mesh_vert_code);
    ResourceSystem::load_shader(ctx, "accum_blend_frag", vk::ShaderStageFlagBits::eFragment,
                                g_blend_mesh_frag_code);
    ResourceSystem::load_shader(ctx, "post_blend_vert", vk::ShaderStageFlagBits::eVertex,
                                g_post_blend_vert_code);
    ResourceSystem::load_shader(ctx, "post_blend_frag", vk::ShaderStageFlagBits::eFragment,
                                g_post_blend_frag_code);

    auto& blendVertShader = ResourceSystem::get_shader("accum_blend_vert");
    auto& blendFragShader = ResourceSystem::get_shader("accum_blend_frag");
    auto& postBlendVertShader = ResourceSystem::get_shader("post_blend_vert");
    auto& postBlendFragShader = ResourceSystem::get_shader("post_blend_frag");
    ctx.acquireSet(postBlendFragShader.layout(0), sceneOITRenderer.accumImageDescriptorSet);

    // accumulate render pass
    {
      auto rpBuilder = ctx.renderpass().setNumPasses(1);
      rpBuilder
          .addAttachment(depthFormat,  // vk::ImageLayout::eDepthStencilAttachmentOptimal,
                         vk::ImageLayout::eShaderReadOnlyOptimal,  // see [sceneRenderer.renderPass
                                                                   // :: attachment 3]
                         vk::ImageLayout::eDepthStencilReadOnlyOptimal, false,
                         vk::SampleCountFlagBits::e1)
          .addAttachment(vk::Format::eR32G32B32A32Sfloat, vk::ImageLayout::eUndefined,
                         vk::ImageLayout::eShaderReadOnlyOptimal, true, vk::SampleCountFlagBits::e1)
          .addAttachment(colorFormat, vk::ImageLayout::eUndefined,
                         vk::ImageLayout::eShaderReadOnlyOptimal, true, vk::SampleCountFlagBits::e1)
          .addSubpass({1, 2}, /*depthStencilRef*/ 0, /*colorResolveRef*/ {},
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
      sceneOITRenderer.accumRenderPass = rpBuilder.build();
    }

    // post blend pass
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
      sceneOITRenderer.postRenderPass = rpBuilder.build();
    }

    sceneOITRenderer.accumPipeline
        = ctx.pipeline()
              .setRenderPass(sceneOITRenderer.accumRenderPass.get(), 0)
              .setRasterizationSamples(vk::SampleCountFlagBits::e1)
              .setCullMode(vk::CullModeFlagBits::eBack)
              .setTopology(vk::PrimitiveTopology::eTriangleList)
              .setDepthTestEnable(true)
              .setDepthWriteEnable(false)
              .setDepthCompareOp(SceneEditor::reversedZ ? vk::CompareOp::eGreaterOrEqual
                                                        : vk::CompareOp::eLessOrEqual)
              .setBlendEnable(true)
              .setColorBlendOp(vk::BlendOp::eAdd, 0)
              .setColorBlendFactor(vk::BlendFactor::eOne, vk::BlendFactor::eOne, 0)
              .setColorBlendOp(vk::BlendOp::eAdd, 1)
              .setColorBlendFactor(vk::BlendFactor::eOne, vk::BlendFactor::eOne, 1)
              .setAlphaBlendOp(vk::BlendOp::eAdd)
              .setAlphaBlendFactor(vk::BlendFactor::eZero, vk::BlendFactor::eOneMinusSrcAlpha)
              .setShader(blendVertShader)
              .setShader(blendFragShader)
              .setPushConstantRanges(
                  {vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)}})
              .setBindingDescriptions(VkModel::get_binding_descriptions_normal_color(VkModel::tri))
              .setAttributeDescriptions(
                  VkModel::get_attribute_descriptions_normal_color(VkModel::tri))
              .build();

    sceneOITRenderer.postPipeline
        = ctx.pipeline()
              .setRenderPass(sceneOITRenderer.postRenderPass.get(), 0)
              .setRasterizationSamples(vk::SampleCountFlagBits::e1)
              .setCullMode(vk::CullModeFlagBits::eBack)
              .setTopology(vk::PrimitiveTopology::eTriangleFan)
              .setDepthWriteEnable(false)
              .setDepthTestEnable(false)
              .setBlendEnable(true)
              .setColorBlendOp(vk::BlendOp::eAdd)
              .setColorBlendFactor(vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha)
              .setAlphaBlendOp(vk::BlendOp::eAdd)
              .setAlphaBlendFactor(vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha)
              .setShader(postBlendVertShader)
              .setShader(postBlendFragShader)
              .build();

    rebuildOITFBO();
  }

  void SceneEditor::rebuildOITFBO() {
    sceneOITRenderer.accumImage0
        = ctx().create2DImage(vkCanvasExtent, vk::Format::eR32G32B32A32Sfloat,
                              /* combined image sampler */ vk::ImageUsageFlagBits::eSampled
                                  | vk::ImageUsageFlagBits::eColorAttachment |
                                  /* input attachment */ vk::ImageUsageFlagBits::eInputAttachment);
    sceneOITRenderer.accumImage1
        = ctx().create2DImage(vkCanvasExtent, colorFormat,
                              /* combined image sampler */ vk::ImageUsageFlagBits::eSampled
                                  | vk::ImageUsageFlagBits::eColorAttachment |
                                  /* input attachment */ vk::ImageUsageFlagBits::eInputAttachment);

    sceneOITRenderer.accumFBO = ctx().createFramebuffer(
        {
            (vk::ImageView)sceneAttachments.depth.get(),
            (vk::ImageView)sceneOITRenderer.accumImage0.get(),
            (vk::ImageView)sceneOITRenderer.accumImage1.get(),
        },
        vkCanvasExtent, sceneOITRenderer.accumRenderPass.get());
    sceneOITRenderer.postFBO
        = ctx().createFramebuffer({(vk::ImageView)sceneAttachments.color.get()}, vkCanvasExtent,
                                  sceneOITRenderer.postRenderPass.get());

    vk::DescriptorImageInfo imageInfo{};
    imageInfo.sampler = sceneOITRenderer.sampler.get();
    imageInfo.imageView = sceneOITRenderer.accumImage0.get();
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx().writeDescriptorSet(imageInfo, sceneOITRenderer.accumImageDescriptorSet,
                             vk::DescriptorType::eCombinedImageSampler,
                             /*binding*/ 0);

    imageInfo.imageView = sceneOITRenderer.accumImage1.get();
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx().writeDescriptorSet(imageInfo, sceneOITRenderer.accumImageDescriptorSet,
                             vk::DescriptorType::eCombinedImageSampler,
                             /*binding*/ 1);
  }

  void SceneEditor::renderTransparent() {
    auto& ctx = this->ctx();
    fence.get().wait();
    auto& cmd = this->cmd.get();
    cmd.begin();

#if 0
    auto imageBarrier = image_layout_transition_barrier(
        sceneAttachments.depth.get(), vk::ImageAspectFlagBits::eDepth, vk::ImageLayout::eUndefined,
        vk::ImageLayout::eeDepthStencilAttachmentOptimal,
        vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eEarlyFragmentTests);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eTransfer,
                        vk::DependencyFlags(), {}, {}, {imageBarrier}, ctx.dispatcher);
#endif
    // render accumulation
    {
      vk::Rect2D rect = vk::Rect2D(vk::Offset2D(), vkCanvasExtent);
      std::array<vk::ClearValue, 3> clearValues{};
      clearValues[1].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
      clearValues[2].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};
      auto renderPassInfo = vk::RenderPassBeginInfo()
                                .setRenderPass(sceneOITRenderer.accumRenderPass.get())
                                .setFramebuffer(sceneOITRenderer.accumFBO.get())
                                .setRenderArea(rect)
                                .setClearValueCount((zs::u32)clearValues.size())
                                .setPClearValues(clearValues.data());
      (*cmd).beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

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

      for (const auto& primPtr : getCurrentVisiblePrims()) {
        // auto prim = primPtr.lock();
        auto& prim = primPtr;
        if (prim->details().refIsOpaque()) continue;
        if (!currentVisiblePrimsDrawn[prim]) continue;
        const auto& transform = prim->currentTimeVisualTransform();
        auto pModel = prim->queryVkTriMesh(ctx, sceneRenderData.currentTimeCode);
        const auto& model = *pModel;

        if (model.isParticle()) {
          (*cmd).bindDescriptorSets(
              vk::PipelineBindPoint::eGraphics,
              /*pipeline layout*/ sceneRenderer.pointTransparentPipeline.get(),
              /*firstSet*/ 0,
              /*descriptor sets*/ {sceneRenderData.sceneCameraSet},
              /*dynamic offset*/ {0}, ctx.dispatcher);
          // use ubo instead of push constant for camera

          (*cmd).bindPipeline(vk::PipelineBindPoint::eGraphics,
                              sceneRenderer.pointTransparentPipeline.get());

          sceneRenderer.pointVertParams.model = transform;
          (*cmd).pushConstants(
              sceneRenderer.pointTransparentPipeline.get(), vk::ShaderStageFlagBits::eVertex, 0,
              sizeof(sceneRenderer.pointVertParams), &sceneRenderer.pointVertParams);
          model.bind((*cmd), VkModel::point);
          model.draw((*cmd), VkModel::point);
        } else {
          // const auto &texSet = ResourceSystem::get_texture_descriptor_set(model.texturePath);
          // use ubo instead of push constant for camera
          (*cmd).bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                    /*pipeline layout*/ sceneOITRenderer.accumPipeline.get(),
                                    /*firstSet*/ 0,
                                    /*descriptor sets*/ {sceneRenderData.sceneCameraSet},
                                    /*dynamic offset*/ {0}, ctx.dispatcher);

          (*cmd).bindPipeline(vk::PipelineBindPoint::eGraphics,
                              sceneOITRenderer.accumPipeline.get());

          (*cmd).pushConstants(sceneOITRenderer.accumPipeline.get(),
                               vk::ShaderStageFlagBits::eVertex, 0, sizeof(transform), &transform);
          model.bindNormalColor((*cmd), VkModel::tri);
          model.drawNormalColor((*cmd), VkModel::tri);
        }
      }

      (*cmd).endRenderPass();
    }

    // post blending
    {
      vk::Rect2D rect = vk::Rect2D(vk::Offset2D(), vkCanvasExtent);
      auto renderPassInfo = vk::RenderPassBeginInfo()
                                .setRenderPass(sceneOITRenderer.postRenderPass.get())
                                .setFramebuffer(sceneOITRenderer.postFBO.get())
                                .setRenderArea(rect);
      (*cmd).beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

      (*cmd).bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                /*pipeline layout*/ sceneOITRenderer.postPipeline.get(),
                                /*firstSet*/ 0,
                                /*descriptor sets*/ {sceneOITRenderer.accumImageDescriptorSet},
                                /*dynamic offset*/ {}, ctx.dispatcher);
      (*cmd).bindPipeline(vk::PipelineBindPoint::eGraphics, sceneOITRenderer.postPipeline.get());
      (*cmd).draw(4, 1, 0, 0, ctx.dispatcher);

      (*cmd).endRenderPass();
    }

    (*cmd).end();
    cmd.submit(fence.get(), /*reset fence*/ true, /*reset config*/ true);

    fence.get().wait();
  }
}  // namespace zs
