#include "SceneEditor.hpp"
#include "zensim/execution/Atomics.hpp"

#define ENABLE_PROFILE 0

namespace zs {

  static const char g_prim_vert_vert_code[] = R"(
#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in uint inVid;

layout (set = 0, binding = 0) uniform SceneCamera {
	mat4 projection;
	mat4 view;
} cameraUbo;

layout(push_constant) uniform Model {
	mat4 model;
} params;

layout (location = 0) out uint outVid;

void main() 
{
	outVid = inVid;
    // outVid = uint(gl_VertexID);

	gl_Position = cameraUbo.projection * cameraUbo.view * params.model * vec4(inPos, 1.0);
    // gl_Position.z *= (1 - 0.01 - smoothstep(0.99, 0, gl_Position.z) * 0.495);
	gl_PointSize = 1.0;
}
)";

  static const char g_prim_vert_frag_code[] = R"(
#version 450

layout (location = 0) flat in uint inVid;

layout (push_constant) uniform fragmentPushConstants {
    layout(offset = 16 * 4) int objId;
} pushConstant;

layout (location = 0) out ivec3 outTag;

void main() 
{
	outTag = ivec3(pushConstant.objId, inVid, -1);
}
)";

  /// @ref
  /// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
  static const char g_pick_vis_vert_code[] = R"(
#version 450

out gl_PerVertex {
  vec4 gl_Position;
};

void main() {
  gl_Position =
      vec4(vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0f - 1.0f,
           0.0f, 1.0f);
}
)";
  /// @ref
  /// https://www.reddit.com/r/vulkan/comments/ilrbcu/use_an_input_attachment_of_type_vk_format_r32_uint/
  static const char g_pick_vis_frag_code[] = R"(
#version 450

// (obj id, vert id)
layout (input_attachment_index = 0, binding = 0) uniform isubpassInput inputTag;

layout (location = 0) out vec4 outColor;

void main() 
{
	// Visualize depth input range
	// Read depth from previous depth input attachment

	int objid = subpassLoad(inputTag).r;
	int vid = subpassLoad(inputTag).g;
	if (objid == 0)
		outColor.rgb = vec3(1, 0, 0);
	else if (objid == 1)
		outColor.rgb = vec3(0, 1, 0);
	else if (objid == 2)
		outColor.rgb = vec3(0, 0.5, 1);
	else if (objid == -1)
		outColor.rgb = vec3(0, 0, 0);
	else if (objid >= 0 && vid == -1)
		outColor.rgb = vec3(0, 0.2, 0.2);
	if (subpassLoad(inputTag).b != -1)
		outColor.rgb *= (subpassLoad(inputTag).b % 10 + 1) / 10.0;
	if (objid >= 0 && vid >= 0)
		outColor.rgb = vec3(1) - outColor.rgb;
	// outColor.rgb = vec3(objid / 2.0, vid % 100 / 100.0, 1);
	outColor.a = 1;
}
)";

  void SceneEditor::setupPickResources() {
    auto &ctx = this->ctx();
    ResourceSystem::load_shader(ctx, "pick.vert", vk::ShaderStageFlagBits::eVertex,
                                g_prim_vert_vert_code);
    auto &vertShader = ResourceSystem::get_shader("pick.vert");
    ResourceSystem::load_shader(ctx, "pick.frag", vk::ShaderStageFlagBits::eFragment,
                                g_prim_vert_frag_code);
    auto &fragShader = ResourceSystem::get_shader("pick.frag");

    ResourceSystem::load_shader(ctx, "default_pick_vis.vert", vk::ShaderStageFlagBits::eVertex,
                                g_pick_vis_vert_code);
    auto &postFxVertShader = ResourceSystem::get_shader("default_pick_vis.vert");
    ResourceSystem::load_shader(ctx, "default_pick_vis.frag", vk::ShaderStageFlagBits::eFragment,
                                g_pick_vis_frag_code);
    auto &postFxFragShader = ResourceSystem::get_shader("default_pick_vis.frag");

    ctx.acquireSet(postFxFragShader.layout(0), scenePickPass.postFxInputAttachmentSet);

    ctx.acquireSet(guiRenderer->getFragShader().layout(0), scenePickPass.pickSet);
    guiRenderer->registerImage(scenePickPass.pickSet);

    auto rpBuilder
        = ctx.renderpass()
              .addAttachment(pickFormat, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
                             false, vk::SampleCountFlagBits::e1)
              .addAttachment(colorFormat, vk::ImageLayout::eUndefined,
                             vk::ImageLayout::eShaderReadOnlyOptimal, false,
                             vk::SampleCountFlagBits::e1)
              .addAttachment(depthFormat, vk::ImageLayout::eDepthStencilAttachmentOptimal,
                             vk::ImageLayout::eDepthStencilAttachmentOptimal,  // used as input
                                                                               // attachment
                             false, vk::SampleCountFlagBits::e1)
              .addSubpass({0}, /*depthStencilRef*/ 2, /*colorResolveRef*/ {},
                          /*depthStencilResolveRef*/ -1)
              .addSubpass({1}, /*depthStencilRef*/ -1, /*colorResolveRef*/ {},
                          /*depthStencilResolveRef*/ -1, {0});
    rpBuilder.setSubpassDependencies(
        {vk::SubpassDependency2{}
             .setSrcSubpass(VK_SUBPASS_EXTERNAL)
             .setDstSubpass(0)
             .setSrcAccessMask(vk::AccessFlagBits::eMemoryRead)
             .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentRead
                               | vk::AccessFlagBits::eColorAttachmentWrite)
             .setSrcStageMask(vk::PipelineStageFlagBits::eBottomOfPipe)
             .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput
                              | vk::PipelineStageFlagBits::eEarlyFragmentTests)
             .setDependencyFlags(vk::DependencyFlagBits::eByRegion),
         vk::SubpassDependency2{}
             .setSrcSubpass(0)
             .setDstSubpass(1)
             .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
             .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
             .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
             .setDstStageMask(vk::PipelineStageFlagBits::eFragmentShader)
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
    scenePickPass.renderPass = rpBuilder.build();

    auto pipelineBuilder = ctx.pipeline();
    pipelineBuilder.setRenderPass(scenePickPass.renderPass.get(), 0)
        .setBlendEnable(false)
#ifndef ZS_PLATFORM_OSX
        .enableDynamicState(vk::DynamicState::eDepthTestEnable)
#endif
        .setDepthCompareOp(SceneEditor::reversedZ ? vk::CompareOp::eGreaterOrEqual
                                                  : vk::CompareOp::eLessOrEqual)
        .setRasterizationSamples(vk::SampleCountFlagBits::e1)
        .setTopology(vk::PrimitiveTopology::ePointList)
        .setShader(vertShader)
        .setShader(fragShader)
        .setPushConstantRanges(
            {vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)},
             vk::PushConstantRange{vk::ShaderStageFlagBits::eFragment, sizeof(glm::mat4),
                                   sizeof(int)}})
        .setBindingDescriptions(VkModel::get_binding_descriptions(VkModel::pick))
        .setAttributeDescriptions(VkModel::get_attribute_descriptions(VkModel::pick));
    scenePickPass.pipeline = pipelineBuilder.build();

    scenePickPass.postFxPipeline = ctx.pipeline()
                                       .setRenderPass(scenePickPass.renderPass.get(), 1)
                                       .setShader(postFxVertShader)
                                       .setShader(postFxFragShader)
                                       .build();

    rebuildPickFbos();
  }

  void SceneEditor::rebuildPickFbos() {
    // scenePickPass.pickBufferHost = ctx().createBuffer(
    //     scenePickPass.pickBuffer.get().getSize(), vk::BufferUsageFlagBits::eTransferDst,
    //     vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eDeviceLocal);
    // scenePickPass.pickBufferHost.get().map();

    scenePickPass.postFxVis
        = ctx().create2DImage(vkCanvasExtent, colorFormat,
                              /* combined image sampler */ vk::ImageUsageFlagBits::eSampled |
                                  /* storage image */ vk::ImageUsageFlagBits::eStorage
                                  | vk::ImageUsageFlagBits::eColorAttachment);
    scenePickPass.fbo = ctx().createFramebuffer(
        {(vk::ImageView)scenePickPass.pickBuffer.get(),
         (vk::ImageView)scenePickPass.postFxVis.get(), (vk::ImageView)sceneAttachments.depth.get()},
        vkCanvasExtent, scenePickPass.renderPass.get());

    // for visualization
    fmt::print("viewport panel size: {}, {}\n", vkCanvasExtent.width, vkCanvasExtent.height);
    vk::DescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler.get();
    imageInfo.imageView = scenePickPass.postFxVis.get();
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx().writeDescriptorSet(imageInfo, scenePickPass.pickSet,
                             vk::DescriptorType::eCombinedImageSampler, 0);
    // for second subpass
    imageInfo.sampler = VK_NULL_HANDLE;
    imageInfo.imageView = scenePickPass.pickBuffer.get();
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx().writeDescriptorSet(imageInfo, scenePickPass.postFxInputAttachmentSet,
                             vk::DescriptorType::eInputAttachment, /*binding*/ 0);
  }

  void SceneEditor::renderFramePickBuffers() {
    auto &ctx = this->ctx();

    struct {
      glm::mat4 projection;
      glm::mat4 view;
    } params;
    params.projection = sceneRenderData.camera.get().matrices.perspective;
    params.view = sceneRenderData.camera.get().matrices.view;
    std::memcpy(sceneRenderData.sceneCameraUbo.get().mappedAddress(), &params, sizeof(params));

#if ENABLE_PROFILE
    CppTimer timer;
    timer.tick();
#endif
    if (ctx.device.waitForFences({(vk::Fence)fence.get()}, VK_TRUE, std::numeric_limits<u64>::max(),
                                 ctx.dispatcher)
        != vk::Result::eSuccess)
      throw std::runtime_error("error waiting for fences");
#if ENABLE_PROFILE
    timer.tock("SceneEditor:: sync before pick pass");
#endif

    // if (sceneAugmentRenderer.overlayTextNeedUpdate)

#if ENABLE_PROFILE
    timer.tick();
#endif
    {
      auto &cmd = this->cmd.get();
      cmd.begin();

      auto depthBarrier = image_layout_transition_barrier(
          sceneAttachments.depth.get(), vk::ImageAspectFlagBits::eDepth,
          vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal,
          vk::PipelineStageFlagBits::eColorAttachmentOutput,
          vk::PipelineStageFlagBits::eEarlyFragmentTests);
      (*cmd).pipelineBarrier(vk::PipelineStageFlagBits::eHost,
                             vk::PipelineStageFlagBits::eLateFragmentTests, vk::DependencyFlags(),
                             {}, {}, {depthBarrier}, ctx.dispatcher);

      vk::Rect2D rect = vk::Rect2D(vk::Offset2D(), vkCanvasExtent);
      std::array<vk::ClearValue, 1> clearValues{};
      clearValues[0].color = vk::ClearColorValue{std::array<int, 4>{-1, -1, -1, -1}};
      auto renderPassInfo = vk::RenderPassBeginInfo()
                                .setRenderPass(scenePickPass.renderPass.get())
                                .setFramebuffer(scenePickPass.fbo.get())
                                .setRenderArea(rect)
                                .setClearValueCount((zs::u32)clearValues.size())
                                .setPClearValues(clearValues.data());
      (*cmd).beginRenderPass(renderPassInfo, vk::SubpassContents::eSecondaryCommandBuffers);

      /// @brief parallel rendering command recording
      resetVkCmdCounter();
      auto visiblePrimChunks = chunk_view{getCurrentVisiblePrims(),
                                          evalRenderChunkSize(getCurrentVisiblePrims().size())};
      int j = 0;
      auto inheritance = vk::CommandBufferInheritanceInfo{scenePickPass.renderPass.get(),
                                                          /*subpass*/ 0, scenePickPass.fbo.get()};

      auto viewport = vk::Viewport()
                          .setX(0 /*offsetx*/)
                          .setY(vkCanvasExtent.height /*-offsety*/)
                          .setWidth(float(vkCanvasExtent.width))
                          .setHeight(-float(
                              vkCanvasExtent.height))  // negative viewport, opengl conformant
                          .setMinDepth(0.0f)
                          .setMaxDepth(1.0f);

      for (auto it = visiblePrimChunks.begin(); it < visiblePrimChunks.end(); ++it, ++j) {
        renderScheduler->enqueue(
            [&, it]() {
              auto &renderCmd = nextVkCommand();
              (*renderCmd)
                  .begin(
                      vk::CommandBufferBeginInfo{
                          vk::CommandBufferUsageFlagBits::eRenderPassContinue, &inheritance},
                      ctx.dispatcher);

              (*renderCmd).setViewport(0, {viewport});

              (*renderCmd).setScissor(0, {vk::Rect2D(vk::Offset2D(), vkCanvasExtent)});
              (*renderCmd)
                  .bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                      /*pipeline layout*/ scenePickPass.pipeline.get(),
                                      /*firstSet*/ 0,
                                      /*descriptor sets*/ {sceneRenderData.sceneCameraSet},
                                      /*dynamic offset*/ {0}, ctx.dispatcher);
#ifndef ZS_PLATFORM_OSX
              (*renderCmd).setDepthTestEnable(!ignoreDepthTest);
#endif
              (*renderCmd)
                  .bindPipeline(vk::PipelineBindPoint::eGraphics, scenePickPass.pipeline.get());

              // for (const auto &model : sceneRenderData.models)
              // for (const auto &primPtr : getCurrentVisiblePrims())
              for (const auto &primPtr : *it) {
                // auto prim = primPtr.lock();
                auto &prim = primPtr;
                if (!prim || prim->empty()) continue;
                auto pModel = prim->queryVkTriMesh(ctx, sceneRenderData.currentTimeCode);
                if (!pModel || currentVisiblePrimsDrawn.at(prim) == 0) continue;
                const auto &model = *pModel;
            // const auto &model = prim->vkTriMesh(ctx);
                // auto transform = prim->visualTransform(sceneRenderData.currentTimeCode);
                const auto& transform = prim->currentTimeVisualTransform();
                (*renderCmd)
                    .pushConstants(scenePickPass.pipeline.get(), vk::ShaderStageFlagBits::eVertex,
                                   0, sizeof(transform), &transform);
                int objId = prim->id();
                (*renderCmd)
                    .pushConstants(scenePickPass.pipeline.get(), vk::ShaderStageFlagBits::eFragment,
                                   sizeof(transform), sizeof(int), &objId);
                model.bind((*renderCmd), VkModel::pick);
                model.draw((*renderCmd), VkModel::pick);
              }
              (*renderCmd).end();
            },
            j);
      }
      renderScheduler->wait();

      (*cmd).executeCommands(getCurrentSecondaryVkCmds(), ctx.dispatcher);

      ///
      /// second subpass
      ///
      (*cmd).nextSubpass(vk::SubpassContents::eInline);

      (*cmd).setViewport(0, {viewport});
      (*cmd).setScissor(0, {vk::Rect2D(vk::Offset2D(), vkCanvasExtent)});
      (*cmd).bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                /*pipeline layout*/ scenePickPass.pipeline.get(),
                                /*firstSet*/ 0,
                                /*descriptor sets*/ {sceneRenderData.sceneCameraSet},
                                /*dynamic offset*/ {0}, ctx.dispatcher);
#ifndef ZS_PLATFORM_OSX
      (*cmd).setDepthTestEnable(!ignoreDepthTest);
#endif

      glm::vec2 visRange{0.0, 1.0};
      (*cmd).bindPipeline(vk::PipelineBindPoint::eGraphics, scenePickPass.postFxPipeline.get());
      (*cmd).bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                /*pipeline layout*/ scenePickPass.postFxPipeline.get(),
                                /*firstSet*/ 0,
                                /*descriptor sets*/ {scenePickPass.postFxInputAttachmentSet},
                                /*dynamic offset*/ {}, ctx.dispatcher);
      (*cmd).draw(3, 1, 0, 0, ctx.dispatcher);

      (*cmd).endRenderPass();

      cmd.end();

      cmd.submit(fence.get(), /*reset fence*/ true, /*reset config*/ true);
    }
#if ENABLE_PROFILE
    timer.tock("SceneEditor:: render frame pick buffers");
#endif
  }

}  // namespace zs

#undef ENABLE_PROFILE