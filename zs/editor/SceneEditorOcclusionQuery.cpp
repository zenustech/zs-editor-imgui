#include "SceneEditor.hpp"

#define OCCLUSION_DEBUG_RENDER 0

namespace zs {
  static const char g_occlusion_vert_code[] = R"(
#version 450
layout (set = 0, binding = 0) uniform SceneCamera {
    mat4 projection;
    mat4 view;
} camera;

layout (push_constant) uniform PushConstants {
    layout(offset = 0) vec4 pos[2];
} constant;

int boxVertIndices[108] = {
    // left
    0, 0, 0,  0, 1, 0,  0, 0, 1,
    0, 1, 1,  0, 1, 0,  0, 0, 1,
    
    // right
    1, 1, 1,  1, 1, 0,  1, 0, 1,
    1, 0, 0,  1, 1, 0,  1, 0, 1,
    
    // bottom
    0, 0, 0,  1, 0, 0,  0, 0, 1,
    1, 0, 1,  1, 0, 0,  0, 0, 1,
    
    // top
    0, 1, 0,  1, 1, 0,  0, 1, 1,
    1, 1, 1,  1, 1, 0,  0, 1, 1,
    
    // front
    0, 0, 0,  1, 0, 0,  0, 1, 0,
    1, 1, 0,  1, 0, 0,  0, 1, 0,
    
    // back
    0, 0, 1,  1, 0, 1,  0, 1, 1,
    1, 1, 1,  1, 0, 1,  0, 1, 1
};

void main() {
  int vid = gl_VertexIndex * 3;
  vec4 vertPos = vec4(
    constant.pos[boxVertIndices[vid]].x,
    constant.pos[boxVertIndices[vid + 1]].y,
    constant.pos[boxVertIndices[vid + 2]].z,
    1.0
  );
  gl_Position = camera.projection * camera.view * vertPos;
}
    )";

#if OCCLUSION_DEBUG_RENDER
  static const char g_occlusion_frag_code[] = R"(
#version 450
layout (location = 0) out vec4 outFragColor;
void main() {
    outFragColor = vec4(0.0, 1.0, 1.0, 0.5);
}
    )";
#else
  static const char g_occlusion_frag_code[] = R"(
#version 450
layout (location = 0) out vec4 outFragColor;
void main() {
    outFragColor = vec4(0.0, 0.0, 0.0, 0.0);
}
    )";
#endif

  void SceneEditor::setupOcclusionQueryResouces() {
    auto& ctx = this->ctx();

    ResourceSystem::load_shader(ctx, "default_occlusion_vert", vk::ShaderStageFlagBits::eVertex, g_occlusion_vert_code);
    ResourceSystem::load_shader(ctx, "default_occlusion_frag", vk::ShaderStageFlagBits::eFragment, g_occlusion_frag_code);
    auto& occlusionVertShader = ResourceSystem::get_shader("default_occlusion_vert");
    auto& occlusionFragShader = ResourceSystem::get_shader("default_occlusion_frag");

    // build render pass
    {
      auto rpBuilder = ctx.renderpass().setNumPasses(1);
      rpBuilder
          .addAttachment(depthFormat, vk::ImageLayout::eDepthStencilReadOnlyOptimal,
                         vk::ImageLayout::eDepthStencilReadOnlyOptimal, false,
                         vk::SampleCountFlagBits::e1)
          .addAttachment(colorFormat, vk::ImageLayout::eShaderReadOnlyOptimal,
                         vk::ImageLayout::eShaderReadOnlyOptimal, false,
                         vk::SampleCountFlagBits::e1)
          .addSubpass({1}, /*depthStencilRef*/ 0, /*colorResolveRef*/ {},
                      /*depthStencilResolveRef*/ -1, /*inputAttachments*/ {});
      rpBuilder.setSubpassDependencies(
          {vk::SubpassDependency2{}
               .setSrcSubpass(VK_SUBPASS_EXTERNAL)
               .setDstSubpass(0)
               .setSrcAccessMask(vk::AccessFlagBits::eMemoryRead)
               .setSrcStageMask(vk::PipelineStageFlagBits::eBottomOfPipe)
               .setDstAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentRead
                                 | vk::AccessFlagBits::eColorAttachmentWrite)
               .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput
                                | vk::PipelineStageFlagBits::eLateFragmentTests)
               .setDependencyFlags(vk::DependencyFlagBits::eByRegion),
           vk::SubpassDependency2{}
               .setSrcSubpass(0)
               .setDstSubpass(VK_SUBPASS_EXTERNAL)
               .setDstAccessMask(vk::AccessFlagBits::eMemoryRead)
               .setSrcAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentRead
                                 | vk::AccessFlagBits::eColorAttachmentWrite)
               .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput
                                | vk::PipelineStageFlagBits::eEarlyFragmentTests)
               .setDstStageMask(vk::PipelineStageFlagBits::eBottomOfPipe)
               .setDependencyFlags(vk::DependencyFlagBits::eByRegion)});
      sceneOcclusionQuery.renderPass = rpBuilder.build();
    }

    sceneOcclusionQuery.renderPipeline
      = ctx.pipeline()
      .setRenderPass(sceneOcclusionQuery.renderPass.get(), 0)
      .setRasterizationSamples(vk::SampleCountFlagBits::e1)
      .setCullMode(vk::CullModeFlagBits::eNone)
      .setTopology(vk::PrimitiveTopology::eTriangleList)
      .setDepthTestEnable(true)
      .setDepthWriteEnable(false)
      .enableDepthBias(SceneEditor::reversedZ ? 2.0f : -2.0f)
      .setColorWriteMask(vk::ColorComponentFlagBits::eA, 0)
      .setDepthCompareOp(SceneEditor::reversedZ ? vk::CompareOp::eGreaterOrEqual
                                                : vk::CompareOp::eLessOrEqual)
      .setColorBlendOp(vk::BlendOp::eAdd)
      .setColorBlendFactor(vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha)
      .setAlphaBlendOp(vk::BlendOp::eAdd)
      .setAlphaBlendFactor(vk::BlendFactor::eZero, vk::BlendFactor::eOne) // don't affect render alpha
      // .setColorWriteMask({})
      .setShader(occlusionVertShader)
      .setShader(occlusionFragShader)
      .setPushConstantRange(vk::PushConstantRange{ vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::vec4) * 2 })
      .build();

    rebuildOcclusionQueryFbo();
  }

  void SceneEditor::rebuildOcclusionQueryFbo() {
    sceneOcclusionQuery.occlusionFBO = ctx().createFramebuffer(
      { (vk::ImageView)sceneAttachments.depth.get(), (vk::ImageView)sceneAttachments.color.get() },
      viewportPanelSize,
      sceneOcclusionQuery.renderPass.get()
    );
  }
  
  void SceneEditor::ensureOcclusionQueryBuffer(const VkCommand& cmd, size_t byteSize) {
    if (!sceneOcclusionQuery.queryBuffer || sceneOcclusionQuery.queryBuffer.get().getSize() < byteSize) {
      auto& ctx = this->ctx();
      sceneOcclusionQuery.occlusionResults.resize(byteSize / sizeof(int));
      std::memset(sceneOcclusionQuery.occlusionResults.data(), 0x1, byteSize);

      sceneOcclusionQuery.queryPool = ctx.createQueryPool(vk::QueryType::eOcclusion, byteSize / sizeof(int));

      sceneOcclusionQuery.queryBuffer = ctx.createBuffer(
        byteSize,
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible
      );
    }
  }

  void SceneEditor::getOcclusionQueryResults() {
    // not initialized
    if (!sceneOcclusionQuery.queryBuffer) return;
    if (sceneOcclusionQuery.actualQueryCount == 0) return;

    const auto byteSize = sceneOcclusionQuery.actualQueryCount * sizeof(int);
    sceneOcclusionQuery.queryBuffer.get().map();
    memcpy(sceneOcclusionQuery.occlusionResults.data(), sceneOcclusionQuery.queryBuffer.get().mappedAddress(), byteSize);
    sceneOcclusionQuery.queryBuffer.get().unmap();
  }

  void SceneEditor::runOcclusionQuery() {
    int& queryID = sceneOcclusionQuery.actualQueryCount;
    queryID = 0;
    // if (!zs_resources().vis_prims_ready()) return;
    // if (!_visBufferReady) return;

    auto& ctx = this->ctx();
    auto& cmd = this->cmd.get();
    cmd.begin();

    auto& visiblePrims = getCurrentVisiblePrims();
    size_t currentQueryCount = visiblePrims.size();
    ensureOcclusionQueryBuffer(cmd, currentQueryCount * sizeof(int));

    vk::Rect2D rect = vk::Rect2D(vk::Offset2D(), viewportPanelSize);
    auto renderPassInfo = vk::RenderPassBeginInfo()
      .setRenderPass(sceneOcclusionQuery.renderPass.get())
      .setFramebuffer(sceneOcclusionQuery.occlusionFBO.get())
      .setRenderArea(rect);

    (*cmd).resetQueryPool(sceneOcclusionQuery.queryPool.get(), 0, currentQueryCount, ctx.dispatcher);
    (*cmd).beginRenderPass(renderPassInfo, vk::SubpassContents::eSecondaryCommandBuffers);
    resetVkCmdCounter();

    auto viewport
      = vk::Viewport()
      .setX(0 /*offsetx*/)
      .setY(viewportPanelSize.height /*-offsety*/)
      .setWidth(float(viewportPanelSize.width))
      .setHeight(-float(viewportPanelSize.height))  // negative viewport, opengl conformant
      .setMinDepth(0.0f)
      .setMaxDepth(1.0f);

    auto inheritance = vk::CommandBufferInheritanceInfo{
      sceneOcclusionQuery.renderPass.get(),
      /*subpass*/ 0,
      sceneOcclusionQuery.occlusionFBO.get()
    };
    renderScheduler->enqueue(
      [&]() {
        auto& renderCmd = nextVkCommand();
        (*renderCmd)
          .begin(
          vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eRenderPassContinue, &inheritance },
          ctx.dispatcher);

        (*renderCmd).setViewport(0, { viewport });
        (*renderCmd).setScissor(0, { vk::Rect2D(vk::Offset2D(), viewportPanelSize) });
        (*renderCmd).bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics,
          /*pipeline layout*/ sceneOcclusionQuery.renderPipeline.get(),
          /*firstSet*/ 0,
          /*descriptor sets*/{ sceneRenderData.sceneCameraSet },
          /*dynamic offset*/{ 0 }, ctx.dispatcher);
        (*renderCmd).bindPipeline(vk::PipelineBindPoint::eGraphics, sceneOcclusionQuery.renderPipeline.get());

        for (const auto& primPtr : visiblePrims) {
          // auto prim = primPtr.lock();
          auto &prim = primPtr;
          if (!prim || prim->empty()) continue;
          auto pModel = prim->queryVkTriMesh(ctx, sceneRenderData.currentTimeCode);
          if (!pModel || !pModel->isValid()) continue;

#if ENABLE_FRUSTUM_CULLING
          // no need to test prims culled by frustum
          {
            auto isCulledIt = isCulledByFrustum.find((void*)prim);
            if (isCulledIt != isCulledByFrustum.end() && isCulledIt->second) {
              sceneOcclusionQuery.primToQueryIndex.erase(prim);
              continue;
            }
          }
#endif
          const auto& box = prim->details().worldBoundingBox();
          // extend AABB a little to avoid self-occlusion or z-fighting
          const glm::vec3& extendSize = glm::max(glm::vec3(1.0f), (box.maxPos - box.minPos) * 0.05f);
          const glm::vec3 minPos = box.minPos - extendSize;
          const glm::vec3 maxPos = box.maxPos + extendSize;

          /*
          * if camera is inside the object's AABB
          * then the object might not pass the occlusion test since it may occlude its AABB
          * so we don't test it in this case
          */
          const glm::vec3& cameraPos = -sceneRenderData.camera.get().position;
          if (cameraPos.x >= minPos.x && cameraPos.x <= maxPos.x
            && cameraPos.y >= minPos.y && cameraPos.y <= maxPos.y
            && cameraPos.z >= minPos.z && cameraPos.z <= maxPos.z) {
            sceneOcclusionQuery.primToQueryIndex.erase(prim);
            continue;
          }

          sceneOcclusionQuery.primToQueryIndex[prim] = queryID;

          // drawing aabb and query
          const glm::vec4 v[2] = { {minPos, 1.0f},{maxPos, 1.0f} };
          (*renderCmd).pushConstants(
            sceneOcclusionQuery.renderPipeline.get(),
            vk::ShaderStageFlagBits::eVertex,
            0, sizeof(glm::vec4) * 2, v
          );

          (*renderCmd).beginQuery(sceneOcclusionQuery.queryPool.get(), queryID, {}, ctx.dispatcher);
          (*renderCmd).draw(36, 1, 0, 0, ctx.dispatcher);
          (*renderCmd).endQuery(sceneOcclusionQuery.queryPool.get(), queryID, ctx.dispatcher);
          ++queryID;
        }

        (*renderCmd).end();
      }, 0
    );
    renderScheduler->wait();
    (*cmd).executeCommands(getCurrentSecondaryVkCmds(), ctx.dispatcher);
    (*cmd).endRenderPass();

    if (queryID > 0) {
      (*cmd).copyQueryPoolResults(
        sceneOcclusionQuery.queryPool.get(),
        0, queryID, // first query , query count
        sceneOcclusionQuery.queryBuffer.get(), 0, // dst buffer, dst offset
        sizeof(int), // stride
        vk::QueryResultFlagBits::eWait,
        ctx.dispatcher
      );
    }

    (*cmd).end();
    cmd.submit(fence.get(), /*reset fence*/ true, /*reset config*/ true);
    fence.get().wait();
  }
}
