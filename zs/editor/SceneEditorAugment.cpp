#include "SceneEditor.hpp"
#include "fonts/stb_font_consolas_24_latin1.inl"
#include "imgui.h"
#include "world/scene/PrimitiveQuery.hpp"
#include "world/system/ZsExecSystem.hpp"

#define TEXTOVERLAY_MAX_CHAR_COUNT 50000
static stb_fontchar g_stbFontData[STB_FONT_consolas_24_latin1_NUM_CHARS];

#define MAX_SELECTION_INDICES 2000000

#define ENABLE_PROFILE 0

namespace zs {

  static const char g_overlay_vert_code[] = R"(
#version 450 core
// #extension GL_EXT_debug_printf : enable

layout (location = 0) in vec2 inPos;
layout (location = 1) in vec2 inUV;

layout (location = 0) out vec2 outUV;

out gl_PerVertex 
{
	vec4 gl_Position;   
};

void main(void) {
  // debugPrintfEXT("gl_VertexIndex = %i", gl_VertexIndex);
  // normal z
	// gl_Position = vec4(inPos, 0.00001, 1.0);  // depth 0 reserved for selection box
  // reverse-Z
	gl_Position = vec4(inPos, 0.99999, 1.0);  // depth 0 reserved for selection box
    // debugPrintfEXT("%f, %f; %f, %f\n", inPos.x, inPos.y, inUV.x, inUV.y);
	outUV = inUV;
}

)";
  static const char g_overlay_frag_code[] = R"(
#version 450 core

layout (location = 0) in vec2 inUV;

layout (binding = 0) uniform sampler2D samplerFont;

layout (location = 0) out vec4 outFragColor;

void main(void) {
    // vec3 rgb = vec3(0.265625, 0.5234375, 0.953125);
    // vec3 rgb = vec3(0, 1, 0.85);
    // vec3 rgb = vec3(0.3, 0.465, 0.53);
    vec3 rgb = vec3(0.0, 0.0, 0.2);
    vec3 outline = vec3(0, 0, 0);
	  // outFragColor = texture(samplerFont, inUV);
	  outFragColor.rgb = rgb;
	  outFragColor.a = texture(samplerFont, inUV).a;
}
)";

  static const char g_wireframe_vert_code[] = R"(
#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inColor;

layout (set = 0, binding = 0) uniform SceneCamera {
	mat4 projection;
	mat4 view;
} cameraUbo;

layout (push_constant) uniform Model {
 	mat4 model;
} params;

layout (location = 0) out vec3 outColor;

void main() {
	// outColor = vec3(1, 0, 1);
	outColor = vec3(1.0 - inColor.x, 1.0 - inColor.y, 1.0 - inColor.z);
	gl_Position = cameraUbo.projection * cameraUbo.view * params.model * vec4(inPos, 1.0);
}
)";
  static const char g_wireframe_frag_code[] = R"(
#version 450

layout (location = 0) in vec3 inColor;

layout (location = 0) out vec4 outFragColor;

void main() {
	outFragColor.rgb = inColor;
	outFragColor.a = 1;
}
)";

  static const char g_gen_text_code[] = R"(
#version 450

// ImGui::ImFontGlyph
struct ImFontGlyph {
  uint Flag;  // colored (1), visible (1), codepoint (30)
  float AdvanceX;
  float X0, Y0, X1, Y1;
  float U0, V0, U1, V1;
};

layout(local_size_x = 32, local_size_y = 32) in;

layout(binding = 0, rgba32i) uniform readonly iimage2D pickImage;

// layout(std140, binding = 1) readonly buffer StbFont {
//   stb_fontchar stbFontData[];
// };
layout(std430, binding = 1) readonly buffer FontGlyph {
  ImFontGlyph fontGlyph[];
};

layout(std430, binding = 2) buffer Counter { 
  uint numLetters; 
  // int selectedObjId; 
};
struct Vertex {
  vec2 pos;
  vec2 uv;
};
layout(std430, binding = 3) buffer TextVerts { Vertex text[]; };

layout(push_constant) uniform Selection {
  int focusId;
  int numTotalVerts;
  int width;
  int height;
  uint limit;
  // ivec2 selectPixel;
} params;

void main() {
  if (gl_GlobalInvocationID.x >= params.width || gl_GlobalInvocationID.y >= params.height)
    return;
  ivec2 ids = imageLoad(pickImage,
                        ivec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y))
                  .rg;

  if (ids.r == params.focusId && ids.g >= 0) {

    const uint firstChar = 32; // STB_FONT_consolas_24_latin1_FIRST_CHAR
    const float charW = 3.5 / params.width;
    const float charH = 3.5 / params.height;
    float x = 1.0 * gl_GlobalInvocationID.x / params.width * 2.0 - 1.0;
    float y = 1.0 * gl_GlobalInvocationID.y / params.height * 2.0 - 1.0;

    int nBits = 0;
    int ns[11], n = ids.g;
    while (n != 0) {
      int tmp = nBits;
      ns[tmp] = n % 10;
      n /= 10;
      nBits = nBits + 1;
    }

    uint offset = atomicAdd(numLetters, nBits);

    if (offset + nBits > params.limit) {
      atomicAdd(numLetters, -nBits);
    } else {
      offset *= 6;
      float depth = 0.00001;
      if (params.numTotalVerts > 0)
        depth = 1.0 * ids.g / params.numTotalVerts;
      while (nBits > 0) {
        nBits--;
/*
        int letter = ns[nBits] + 48; // '0': 48
        const stb_fontchar charData = stbFontData[letter - firstChar];
        float u0 = charData.s0 - 0.002;
        float u1 = charData.s1 + 0.002;
        float v0 = charData.t0 - 0.002;
        float v1 = charData.t1 + 0.002;
        uint x0 = charData.x0y0 >> 16;
        uint y0 = charData.x0y0 & 0xffff;
        uint x1 = charData.x1y1 >> 16;
        uint y1 = charData.x1y1 & 0xffff;
*/
        // int letter = ns[nBits] + 48;
        // uint loc = fontLUT[letter];
        const ImFontGlyph charData = fontGlyph[ns[nBits]];
        float u0 = charData.U0;
        float u1 = charData.U1;
        float v0 = charData.V0;
        float v1 = charData.V1;
        float x0 = charData.X0;
        float y0 = charData.Y0;
        float x1 = charData.X1;
        float y1 = charData.Y1;

        /// 
        Vertex vert;
        vert.pos = vec2(x + x0 * charW, y + y0 * charH);
        vert.uv = vec2(u0, v0);
        text[offset++] = vert;
        vert.pos = vec2(x + x1 * charW, y + y0 * charH);
        vert.uv = vec2(u1, v0);
        text[offset++] = vert;
        vert.pos = vec2(x + x0 * charW, y + y1 * charH);
        vert.uv = vec2(u0, v1);
        text[offset++] = vert;
        vert.pos = vec2(x + x0 * charW, y + y1 * charH);
        vert.uv = vec2(u0, v1);
        text[offset++] = vert;
        vert.pos = vec2(x + x1 * charW, y + y0 * charH);
        vert.uv = vec2(u1, v0);
        text[offset++] = vert;
        vert.pos = vec2(x + x1 * charW, y + y1 * charH);
        vert.uv = vec2(u1, v1);
        text[offset++] = vert;

        x += charData.AdvanceX * charW;
      }
    }
  }

  // imageStore(resultImage, ivec2(gl_GlobalInvocationID.xy), res);
}
)";

  static const char g_gather_selection_code[] = R"(
#version 450

layout(local_size_x = 32, local_size_y = 32) in;

layout(binding = 0, rgba32i) uniform readonly iimage2D pickImage;

layout(std140, binding = 1) buffer Counter {
  uint numPickedUp;
  int selectedObjId;
};
layout(std430, binding = 2) buffer GatheredVerts { ivec2 gatheredIds[]; };

layout(push_constant) uniform Params {
  uint widthOffset;
  uint heightOffset;
  ivec2 extent;
  int focusId;
  uint limit;
} params;

void main() {
  if (gl_GlobalInvocationID.x >= params.extent.x || gl_GlobalInvocationID.y >= params.extent.y)
    return;

  ivec2 loc = ivec2(params.widthOffset + gl_GlobalInvocationID.x,
                    params.heightOffset + gl_GlobalInvocationID.y);
  ivec2 ids = imageLoad(pickImage, loc).rg;

  if (gl_GlobalInvocationID.x == 0 && gl_GlobalInvocationID.y == 0)
    selectedObjId = ids.r;

  if (ids.r == params.focusId && ids.g >= 0) {
    uint offset = atomicAdd(numPickedUp, 1);

    if (offset >= params.limit) {
      atomicAdd(numPickedUp, -1);
    } else {
      gatheredIds[offset] = ivec2(ids.r, ids.g);
    }
  }
}
)";

  static const char g_gather_painted_code[] = R"(
#version 450

layout(local_size_x = 32, local_size_y = 32) in;

layout(binding = 0, rgba32i) uniform readonly iimage2D pickImage;

layout(std140, binding = 1) buffer Counter {
  uint numPickedUp;
  int selectedObjId;
};
layout(std430, binding = 2) buffer GatheredVerts { ivec2 gatheredIds[]; };

layout(push_constant) uniform Params {
  uint widthOffset;
  uint heightOffset;
  ivec2 center;
  uint radius;
  int focusId;  // option
  uint limit;
} params;

void main() {
  uint x = params.widthOffset + gl_GlobalInvocationID.x;
  uint y = params.heightOffset + gl_GlobalInvocationID.y;
  uint dis2 = (x - params.center.x) * (x - params.center.x) + (y - params.center.y) * (y - params.center.y);
  if (dis2 > params.radius * params.radius)
    return;

  ivec2 loc = ivec2(x, y);
  ivec2 ids = imageLoad(pickImage, loc).rg;

  if (dis2 == 0)
    selectedObjId = ids.r;

  if ((ids.r == params.focusId || params.focusId == -1) && ids.g >= 0) {
    uint offset = atomicAdd(numPickedUp, 1);

    if (offset >= params.limit) {
      atomicAdd(numPickedUp, -1);
    } else {
      gatheredIds[offset] = ivec2(ids.r, ids.g);
    }
  }
}
)";

  struct GenTextParam {
    glm::ivec2 ids;
    glm::ivec2 extent;
    u32 limit;
    // glm::ivec2 selectPixel;
  };

  struct SelectionParam {
    glm::uvec2 offset;
    glm::ivec2 extent;
    int focusObjId;
    u32 limit;
  };
  struct PaintParam {
    glm::uvec2 offset;
    glm::ivec2 center;
    u32 radius;
    int focusObjId;
    u32 limit;
  };
  struct TextVertex {
    glm::vec2 pos;
    glm::vec2 uv;
  };

  void SceneEditor::setupAugmentResources() {
    auto &ctx = this->ctx();

    sceneAugmentRenderer.sampler
        = ctx.createSampler(vk::SamplerCreateInfo{}
                                .setMaxAnisotropy(1.f)
                                .setMagFilter(vk::Filter::eLinear)
                                .setMinFilter(vk::Filter::eLinear)
                                .setMipmapMode(vk::SamplerMipmapMode::eLinear)
                                .setAddressModeU(vk::SamplerAddressMode::eRepeat)
                                .setAddressModeV(vk::SamplerAddressMode::eRepeat)
                                .setAddressModeW(vk::SamplerAddressMode::eRepeat)
                                .setCompareOp(vk::CompareOp::eNever)
                                .setBorderColor(vk::BorderColor::eFloatOpaqueWhite));

    // draw overlay text
    ResourceSystem::load_shader(ctx, "default_overlay.vert", vk::ShaderStageFlagBits::eVertex,
                                g_overlay_vert_code);  // sceneAugmentRenderer.overlayVertShader
    auto &overlayVertShader = ResourceSystem::get_shader("default_overlay.vert");
    ResourceSystem::load_shader(ctx, "default_overlay.frag", vk::ShaderStageFlagBits::eFragment,
                                g_overlay_frag_code);  // sceneAugmentRenderer.overlayFragShader
    auto &overlayFragShader = ResourceSystem::get_shader("default_overlay.frag");
    ctx.acquireSet(overlayFragShader.layout(0), sceneAugmentRenderer.overlayFontSet);
    /// @note no need to display overlayFontSet in ImGui::Image
    // guiRenderer->registerImage(sceneAugmentRenderer.overlayFontSet);
    // wireframe
    ResourceSystem::load_shader(ctx, "default_wireframe.vert", vk::ShaderStageFlagBits::eVertex,
                                g_wireframe_vert_code);  // sceneAugmentRenderer.wiredVertShader
    auto &wiredVertShader = ResourceSystem::get_shader("default_wireframe.vert");
    ResourceSystem::load_shader(ctx, "default_wireframe.frag", vk::ShaderStageFlagBits::eFragment,
                                g_wireframe_frag_code);  // sceneAugmentRenderer.wiredFragShader
    auto &wiredFragShader = ResourceSystem::get_shader("default_wireframe.frag");
    // generate overlay text (compute)
    ResourceSystem::load_shader(ctx, "default_gen_overlay_text.comp",
                                vk::ShaderStageFlagBits::eCompute,
                                g_gen_text_code);  // sceneAugmentRenderer.genTextShader
    auto &genTextShader = ResourceSystem::get_shader("default_gen_overlay_text.comp");
    ctx.acquireSet(genTextShader.layout(0), sceneAugmentRenderer.textGenSet);
    sceneAugmentRenderer.genTextPipeline = Pipeline{genTextShader, sizeof(GenTextParam)};
    // gather selection indices (compute)
    ResourceSystem::load_shader(ctx, "default_selection.comp", vk::ShaderStageFlagBits::eCompute,
                                g_gather_selection_code);  // sceneAugmentRenderer.selectionShader
    auto &selectionShader = ResourceSystem::get_shader("default_selection.comp");
    ctx.acquireSet(selectionShader.layout(0), sceneAugmentRenderer.selectionSet);
    sceneAugmentRenderer.selectionPipeline = Pipeline{selectionShader, sizeof(SelectionParam)};
    // paint indices (compute)
    ResourceSystem::load_shader(ctx, "default_paint.comp", vk::ShaderStageFlagBits::eCompute,
                                g_gather_painted_code);
    auto &paintShader = ResourceSystem::get_shader("default_paint.comp");
    ctx.acquireSet(paintShader.layout(0), sceneAugmentRenderer.paintSet);
    sceneAugmentRenderer.paintPipeline = Pipeline{paintShader, sizeof(PaintParam)};

    /// font (texture, template buffer)
    const u32 fontWidth = STB_FONT_consolas_24_latin1_BITMAP_WIDTH;
    const u32 fontHeight = STB_FONT_consolas_24_latin1_BITMAP_HEIGHT;

    static unsigned char font24pixels[fontHeight][fontWidth];
    stb_font_consolas_24_latin1(g_stbFontData, font24pixels, fontHeight);

    sceneAugmentRenderer.fontTexture
        = load_texture(ctx, &font24pixels[0][0], fontWidth * fontHeight,
                       vk::Extent2D{(u32)fontWidth, (u32)fontHeight}, vk::Format::eR8Unorm);

    /// buffers
    {
      auto numStbBytes = sizeof(stb_fontchar) * STB_FONT_consolas_24_latin1_NUM_CHARS;
      auto stagingStbFontBuffer
          = ctx.createStagingBuffer(numStbBytes, vk::BufferUsageFlagBits::eTransferSrc);
      stagingStbFontBuffer.map();
      memcpy(stagingStbFontBuffer.mappedAddress(), g_stbFontData, numStbBytes);
      stagingStbFontBuffer.unmap();

      // font template
      sceneAugmentRenderer.fontTemplateBuffer = ctx.createBuffer(
          numStbBytes,
          vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
          vk::MemoryPropertyFlagBits::eDeviceLocal);

      fence.get().wait();
      vk::BufferCopy copyRegion{};
      copyRegion.size = numStbBytes;
      cmd.get().begin();
      (*cmd.get())
          .copyBuffer(stagingStbFontBuffer, sceneAugmentRenderer.fontTemplateBuffer.get(),
                      {copyRegion});
      (*cmd.get()).end();
      cmd.get().submit(fence.get(), true, true);

      fence.get().wait();

#if 0
      //
      sceneAugmentRenderer.textPosBuffer = ctx.createBuffer(
          6 * TEXTOVERLAY_MAX_CHAR_COUNT * sizeof(glm::vec2),
          vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer,
          vk::MemoryPropertyFlagBits::eDeviceLocal);
      //
      sceneAugmentRenderer.textUvBuffer = ctx.createBuffer(
          6 * TEXTOVERLAY_MAX_CHAR_COUNT * sizeof(glm::vec2),
          vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer,
          vk::MemoryPropertyFlagBits::eDeviceLocal);
#endif
      //
      sceneAugmentRenderer.overlayTextBuffer = ctx.createBuffer(
          6 * TEXTOVERLAY_MAX_CHAR_COUNT * sizeof(TextVertex),
          vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer,
          vk::MemoryPropertyFlagBits::eDeviceLocal);
    }
    {
      // counter
      sceneAugmentRenderer.counterBuffer = ctx.createBuffer(
          sizeof(u32) + sizeof(i32), vk::BufferUsageFlagBits::eStorageBuffer,
          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
    }
    {
      // selection buffer
      sceneAugmentRenderer.selectedIndicesBuffer = ctx.createBuffer(
          sizeof(glm::ivec2) * MAX_SELECTION_INDICES, vk::BufferUsageFlagBits::eStorageBuffer,
          vk::MemoryPropertyFlagBits::eHostVisible);
    }

    // render pass
    auto rpBuilder
        = ctx.renderpass()
              .addAttachment(colorFormat, vk::ImageLayout::eShaderReadOnlyOptimal,
                             vk::ImageLayout::eShaderReadOnlyOptimal, false,
                             vk::SampleCountFlagBits::e1)
              .addAttachment(depthFormat, vk::ImageLayout::eDepthStencilAttachmentOptimal,
                             vk::ImageLayout::eDepthStencilAttachmentOptimal, false,
                             vk::SampleCountFlagBits::e1)
              .addSubpass({0}, /*depthStencilRef*/ 1, /*colorResolveRef*/ {},
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
    sceneAugmentRenderer.renderPass = rpBuilder.build();

    auto pipelineBuilder = ctx.pipeline();
    pipelineBuilder.setRenderPass(sceneAugmentRenderer.renderPass.get(), 0)
        .setCullMode(vk::CullModeFlagBits::eBack)
        .setFrontFace(vk::FrontFace::eClockwise)
        .setRasterizationSamples(vk::SampleCountFlagBits::e1)
        .setTopology(vk::PrimitiveTopology::eTriangleList)
        .setDepthCompareOp(SceneEditor::reversedZ ? vk::CompareOp::eGreaterOrEqual
                                                  : vk::CompareOp::eLessOrEqual)
        .setShader(overlayVertShader)
        .setShader(overlayFragShader)
        .setBindingDescriptions({vk::VertexInputBindingDescription{0, sizeof(TextVertex),
                                                                   vk::VertexInputRate::eVertex}})
        .setAttributeDescriptions(
            {vk::VertexInputAttributeDescription{/*location*/ 0,
                                                 /*binding*/ 0, vk::Format::eR32G32Sfloat, (u32)0},
             vk::VertexInputAttributeDescription{/*location*/ 1,
                                                 /*binding*/ 0, vk::Format::eR32G32Sfloat,
                                                 (u32)offsetof(TextVertex, uv)}});
    sceneAugmentRenderer.overlayPipeline = pipelineBuilder.build();

    sceneAugmentRenderer.wiredPipeline
        = ctx.pipeline()
              .setRenderPass(sceneAugmentRenderer.renderPass.get(), 0)
              .setTopology(vk::PrimitiveTopology::eTriangleList)
              .setRasterizationSamples(vk::SampleCountFlagBits::e1)
              .setPolygonMode(vk::PolygonMode::eLine)
              .enableDynamicState(vk::DynamicState::eLineWidth)
              .setShader(wiredVertShader)
              .setShader(wiredFragShader)
              .setDepthCompareOp(SceneEditor::reversedZ ? vk::CompareOp::eGreaterOrEqual
                                                        : vk::CompareOp::eLessOrEqual)
              .setPushConstantRange({vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)})
              .setBindingDescriptions(VkModel::get_binding_descriptions_color(VkModel::tri))
              .setAttributeDescriptions(VkModel::get_attribute_descriptions_color(VkModel::tri))
              .build();

    rebuildAugmentFbo();
  }

  void SceneEditor::rebuildAugmentFbo() {
    sceneAugmentRenderer.fbo = ctx().createFramebuffer(
        {(vk::ImageView)sceneAttachments.color.get(), (vk::ImageView)sceneAttachments.depth.get()},
        vkCanvasExtent, sceneAugmentRenderer.renderPass.get());

    /// overlay text generation
    {
      vk::DescriptorImageInfo imageInfo{};
      imageInfo.sampler = VK_NULL_HANDLE;
      imageInfo.imageView = scenePickPass.pickBuffer.get();
      imageInfo.imageLayout = vk::ImageLayout::eGeneral;
      ctx().writeDescriptorSet(imageInfo, sceneAugmentRenderer.textGenSet,
                               vk::DescriptorType::eStorageImage, /*binding*/ 0);
      ctx().writeDescriptorSet(guiRenderer->_fontGlyphs.get().descriptorInfo(),
                               sceneAugmentRenderer.textGenSet, vk::DescriptorType::eStorageBuffer,
                               /*binding*/ 1);
      ctx().writeDescriptorSet(sceneAugmentRenderer.counterBuffer.get().descriptorInfo(),
                               sceneAugmentRenderer.textGenSet, vk::DescriptorType::eStorageBuffer,
                               /*binding*/ 2);
      /// text buffers
      ctx().writeDescriptorSet(sceneAugmentRenderer.overlayTextBuffer.get().descriptorInfo(),
                               sceneAugmentRenderer.textGenSet, vk::DescriptorType::eStorageBuffer,
                               /*binding*/ 3);
    }
    /// index selection generation
    {
      vk::DescriptorImageInfo imageInfo{};
      imageInfo.sampler = VK_NULL_HANDLE;
      imageInfo.imageView = scenePickPass.pickBuffer.get();
      imageInfo.imageLayout = vk::ImageLayout::eGeneral;
      ctx().writeDescriptorSet(imageInfo, sceneAugmentRenderer.selectionSet,
                               vk::DescriptorType::eStorageImage, /*binding*/ 0);

      ctx().writeDescriptorSet(sceneAugmentRenderer.counterBuffer.get().descriptorInfo(),
                               sceneAugmentRenderer.selectionSet,
                               vk::DescriptorType::eStorageBuffer,
                               /*binding*/ 1);
      ctx().writeDescriptorSet(sceneAugmentRenderer.selectedIndicesBuffer.get().descriptorInfo(),
                               sceneAugmentRenderer.selectionSet,
                               vk::DescriptorType::eStorageBuffer,
                               /*binding*/ 2);
    }
    /// paint selection generation
    {
      vk::DescriptorImageInfo imageInfo{};
      imageInfo.sampler = VK_NULL_HANDLE;
      imageInfo.imageView = scenePickPass.pickBuffer.get();
      imageInfo.imageLayout = vk::ImageLayout::eGeneral;
      ctx().writeDescriptorSet(imageInfo, sceneAugmentRenderer.paintSet,
                               vk::DescriptorType::eStorageImage, /*binding*/ 0);

      ctx().writeDescriptorSet(sceneAugmentRenderer.counterBuffer.get().descriptorInfo(),
                               sceneAugmentRenderer.paintSet, vk::DescriptorType::eStorageBuffer,
                               /*binding*/ 1);
      ctx().writeDescriptorSet(sceneAugmentRenderer.selectedIndicesBuffer.get().descriptorInfo(),
                               sceneAugmentRenderer.paintSet, vk::DescriptorType::eStorageBuffer,
                               /*binding*/ 2);
    }

    /// overlay text render
    vk::DescriptorImageInfo imageInfo{};
    imageInfo.sampler = sceneAugmentRenderer.sampler.get();
    imageInfo.imageView = sceneAugmentRenderer.fontTexture.image.get();
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx().writeDescriptorSet(imageInfo, sceneAugmentRenderer.overlayFontSet,
                             vk::DescriptorType::eCombinedImageSampler, 0);
  }

  void SceneEditor::renderSceneAugmentView() {
    auto &ctx = this->ctx();

    fence.get().wait();

    auto &cmd = this->cmd.get();

#if ENABLE_PROFILE
    CppTimer timer;
#endif

    if (sceneAugmentRenderer.overlayTextNeedUpdate) {
#if ENABLE_PROFILE
      timer.tick();
#endif
      auto mapped = beginText();

      auto extent = scenePickPass.pickBuffer.get().getExtent();
      /// generate overlay text
      GenTextParam genTextParams;
      if (auto focusPrim = focusPrimPtr.lock()) {
        auto pModel = focusPrim->queryVkTriMesh(ctx, sceneRenderData.currentTimeCode);
        if (pModel && currentVisiblePrimsDrawn.at(focusPrim.get())) {
          const auto &model = *pModel;

          cmd.begin();

          (*cmd).bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                    /*pipeline layout*/ sceneAugmentRenderer.genTextPipeline.get(),
                                    /*firstSet*/ 0,
                                    /*descriptor sets*/ {sceneAugmentRenderer.textGenSet},
                                    /*dynamic offset*/ {}, ctx.dispatcher);
          (*cmd).bindPipeline(vk::PipelineBindPoint::eCompute,
                              sceneAugmentRenderer.genTextPipeline.get());

          genTextParams.limit = TEXTOVERLAY_MAX_CHAR_COUNT;
          genTextParams.extent = glm::ivec2{(int)vkCanvasExtent.width, (int)vkCanvasExtent.height};
          genTextParams.ids
              // = glm::ivec2{focusObjId, sceneRenderData.models[focusObjId].verts.vertexCount};
              = glm::ivec2{focusPrim->id(), /*focusPrim->vkTriMesh(ctx)*/ model.verts.vertexCount};

          (*cmd).pushConstants(sceneAugmentRenderer.genTextPipeline.get(),
                               vk::ShaderStageFlagBits::eCompute, 0, sizeof(genTextParams),
                               &genTextParams);
          (*cmd).dispatch((extent.width + 31) / 32, (extent.height + 31) / 32, 1);

          cmd.end();
          cmd.submit(fence.get(), /*reset fence*/ true, /*reset config*/ true);

          fence.get().wait();
        }
      }

      endText();

      sceneAugmentRenderer.counterBuffer.get().map();
      sceneAugmentRenderer.numLetters
          = *(u32 *)sceneAugmentRenderer.counterBuffer.get().mappedAddress();
      sceneAugmentRenderer.counterBuffer.get().unmap();

#if ENABLE_PROFILE
      timer.tock("SceneEditor::update overlay text");
#endif
    }

    if (viewportHovered) {
      if (interactionMode.isPaintMode()) {
#if ENABLE_PROFILE
        timer.tick();
#endif
        PaintParam paintParams;
        auto focusPrim = focusPrimPtr.lock();
        paintParams.focusObjId = focusPrim ? focusPrim->id() : -1;
        paintParams.limit = MAX_SELECTION_INDICES;

        // fmt::print("painting focus prim id: {} (name {})\n", paintParams.focusObjId, focusPrim ?
        // focusPrim->label() : "null");

        glm::uvec2 extent;
        if (paintCenter) {
          auto center = (*paintCenter);
          paintParams.center = center;
          int radius = paintRadius;
          extent = center + glm::uvec2(radius, radius);
          center -= radius;
          paintParams.offset
              = glm::uvec2(center.x >= 0 ? center.x : 0, center.y >= 0 ? center.y : 0);
          extent = extent - paintParams.offset + glm::uvec2(1, 1);
          paintParams.radius = radius;
        } else {
          paintParams.center = glm::ivec2(canvasLocalMousePos[0], canvasLocalMousePos[1]);
          paintParams.offset = paintParams.center;
          extent = glm::uvec2(1, 1);
          paintParams.radius = 0;
        }

        sceneAugmentRenderer.counterBuffer.get().map();
        *((u32 *)sceneAugmentRenderer.counterBuffer.get().mappedAddress()) = 0;
        *((int *)sceneAugmentRenderer.counterBuffer.get().mappedAddress() + 1) = -1;
        sceneAugmentRenderer.counterBuffer.get().unmap();
        sceneAugmentRenderer.counterBuffer.get().flush();

        {
          fence.get().wait();
          cmd.begin();
          (*cmd).bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                    /*pipeline layout*/ sceneAugmentRenderer.paintPipeline.get(),
                                    /*firstSet*/ 0,
                                    /*descriptor sets*/ {sceneAugmentRenderer.paintSet},
                                    /*dynamic offset*/ {}, ctx.dispatcher);
          (*cmd).bindPipeline(vk::PipelineBindPoint::eCompute,
                              sceneAugmentRenderer.paintPipeline.get());

          (*cmd).pushConstants(sceneAugmentRenderer.paintPipeline.get(),
                               vk::ShaderStageFlagBits::eCompute, 0, sizeof(paintParams),
                               &paintParams);
          (*cmd).dispatch((extent[0] + 31) / 32, (extent[1] + 31) / 32, 1);

          cmd.end();
          cmd.submit(fence.get(), /*reset fence*/ true, /*reset config*/ true);
          fence.get().wait();  // wait for computation then retrieve indices
        }

        sceneAugmentRenderer.counterBuffer.get().map();
        auto numSelectedIndices
            = *((u32 *)sceneAugmentRenderer.counterBuffer.get().mappedAddress());
        PrimIndex hoveredObjId
            = *((int *)sceneAugmentRenderer.counterBuffer.get().mappedAddress() + 1);
        static_assert(is_same_v<PrimIndex, int>, "PrimIndex should be of type int.");
        hoveredPrimPtr = getScenePrimById(hoveredObjId);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) focusPrimPtr = hoveredPrimPtr;
        sceneAugmentRenderer.counterBuffer.get().unmap();

        if (paintCenter) {
          selectedIndices.resize(numSelectedIndices);
          sceneAugmentRenderer.selectedIndicesBuffer.get().map();
          std::memcpy(selectedIndices.data(),
                      sceneAugmentRenderer.selectedIndicesBuffer.get().mappedAddress(),
                      sizeof(glm::ivec2) * numSelectedIndices);
          sceneAugmentRenderer.selectedIndicesBuffer.get().unmap();

          zs::Buffer *prevClrBuffer = nullptr;
          std::sort(selectedIndices.begin(), selectedIndices.end(),
                    [](const glm::ivec2 &a, const glm::ivec2 &b) { return a[0] < b[0]; });
          /// actual painting

          // zs_execution().ref_event_scheduler().enqueue([&ctx, this]() {});
          for (int i = 0; i < numSelectedIndices; ++i) {
            auto ids = selectedIndices[i];

            auto primPtr = getScenePrimById(ids.x);
            // if (primPtr.expired()) continue;
            auto prim = primPtr.lock();
            // fmt::print("iterating prim (label: {}, id: {}) [{}] vert [{}]\n", prim->label(),
            // prim->id(), ids.x, ids.y);
            auto pModel = prim->queryVkTriMesh(ctx, sceneRenderData.currentTimeCode);
            if (!pModel || currentVisiblePrimsDrawn.at(prim.get()) == 0) continue;
            auto &model = *pModel;

            // auto &clrBuffer = sceneRenderData.models[ids.x].getColorBuffer();
            // auto &clrBuffer = getScenePrimByIndex(ids.x)->vkTriMesh(ctx).getColorBuffer();
            auto &clrBuffer = model.getColorBuffer();
            if (&clrBuffer != prevClrBuffer) {
              if (prevClrBuffer) {
                (*prevClrBuffer).unmap();
                (*prevClrBuffer).flush();
              }
              prevClrBuffer = &clrBuffer;
              clrBuffer.map();
            }
            *((glm::vec3 *)clrBuffer.mappedAddress() + ids.y) = paintColor;
          }
          if (prevClrBuffer) {
            (*prevClrBuffer).unmap();
            (*prevClrBuffer).flush();
          }

          paintCenter = {};
        }
#if ENABLE_PROFILE
        timer.tock("SceneEditor:: paint");
#endif
      } else {
#if ENABLE_PROFILE
        timer.tick();
#endif
        /// @brief selection by box or point
        SelectionParam selectionParams;
        auto focusPrim = focusPrimPtr.lock();
        selectionParams.focusObjId = focusPrim ? focusPrim->id() : -1;
        selectionParams.limit = MAX_SELECTION_INDICES;

        glm::ivec2 extent;
        if (selectionBox) {
          selectionParams.offset = (*selectionBox).offset;
          extent = (*selectionBox).extent;
        } else {
          selectionParams.offset = glm::ivec2(canvasLocalMousePos[0], canvasLocalMousePos[1]);
          extent = glm::uvec2(1, 1);
        }
        selectionParams.extent = extent;

        sceneAugmentRenderer.counterBuffer.get().map();
        *((u32 *)sceneAugmentRenderer.counterBuffer.get().mappedAddress()) = 0;
        *((int *)sceneAugmentRenderer.counterBuffer.get().mappedAddress() + 1) = -1;
        sceneAugmentRenderer.counterBuffer.get().unmap();
        sceneAugmentRenderer.counterBuffer.get().flush();

        {
          fence.get().wait();
          cmd.begin();
          (*cmd).bindDescriptorSets(
              vk::PipelineBindPoint::eCompute,
              /*pipeline layout*/ sceneAugmentRenderer.selectionPipeline.get(),
              /*firstSet*/ 0,
              /*descriptor sets*/ {sceneAugmentRenderer.selectionSet},
              /*dynamic offset*/ {}, ctx.dispatcher);
          (*cmd).bindPipeline(vk::PipelineBindPoint::eCompute,
                              sceneAugmentRenderer.selectionPipeline.get());

          (*cmd).pushConstants(sceneAugmentRenderer.selectionPipeline.get(),
                               vk::ShaderStageFlagBits::eCompute, 0, sizeof(selectionParams),
                               &selectionParams);
          (*cmd).dispatch((extent[0] + 31) / 32, (extent[1] + 31) / 32, 1);

          cmd.end();
          cmd.submit(fence.get(), /*reset fence*/ true, /*reset config*/ true);
          fence.get().wait();  // wait for computation then retrieve indices
        }

        sceneAugmentRenderer.counterBuffer.get().map();
        auto numSelectedIndices
            = *((u32 *)sceneAugmentRenderer.counterBuffer.get().mappedAddress());
        // hoveredObjId = *((int *)sceneAugmentRenderer.counterBuffer.get().mappedAddress() + 1);
        // if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) focusObjId = hoveredObjId;
        PrimIndex hoveredObjId
            = *((int *)sceneAugmentRenderer.counterBuffer.get().mappedAddress() + 1);
        static_assert(is_same_v<PrimIndex, int>, "PrimIndex should be of type int.");
        hoveredPrimPtr = getScenePrimById(hoveredObjId);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) focusPrimPtr = hoveredPrimPtr;

        if (auto hoveredPrim = hoveredPrimPtr.lock()) {
          if (get_ray_intersection_with_prim(getCameraPosition(),
                                             getScreenPointCameraRayDirection(), *hoveredPrim,
                                             &hoveredHitPt)) {
            ;
          }
        }

        sceneAugmentRenderer.counterBuffer.get().unmap();

        if (selectionBox) {
          selectedIndices.resize(numSelectedIndices);
          sceneAugmentRenderer.selectedIndicesBuffer.get().map();
          std::memcpy(selectedIndices.data(),
                      sceneAugmentRenderer.selectedIndicesBuffer.get().mappedAddress(),
                      sizeof(glm::ivec2) * numSelectedIndices);
          sceneAugmentRenderer.selectedIndicesBuffer.get().unmap();

          selectionBox = {};
        }
#if ENABLE_PROFILE
        timer.tock("SceneEditor:: selection");
#endif
      }
    }

    /// render overlay text
#if ENABLE_PROFILE
    timer.tick();
#endif
    cmd.begin();

    vk::Rect2D rect = vk::Rect2D(vk::Offset2D(), vkCanvasExtent);
    std::array<vk::ClearValue, 2> clearValues{};
    // clearValues[0].color = vk::ClearColorValue{0.1f, 0.7f, 0.2f, 0.f};
    // clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
    auto renderPassInfo = vk::RenderPassBeginInfo()
                              .setRenderPass(sceneAugmentRenderer.renderPass.get())
                              .setFramebuffer(sceneAugmentRenderer.fbo.get())
                              .setRenderArea(rect)
                              .setClearValueCount((zs::u32)clearValues.size())
                              .setPClearValues(clearValues.data());
    if (showWireframe) {
#if ENABLE_PROFILE
      (*cmd).writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, queryPool.get(), vkq_wireframe,
                            ctx.dispatcher);
      CppTimer locTimer, compTimer;
#endif

#if ENABLE_PROFILE
      locTimer.tick();
#endif

#if 0
      (*cmd).beginRenderPass(renderPassInfo, vk::SubpassContents::eSecondaryCommandBuffers);

      /// @brief parallel rendering command recording
      resetVkCmdCounter();
      auto visiblePrimChunks = chunk_view{getCurrentVisiblePrims(),
                                          evalRenderChunkSize(getCurrentVisiblePrims().size())};
      int j = 0;
      auto inheritance
          = vk::CommandBufferInheritanceInfo{sceneAugmentRenderer.renderPass.get(),
                                             /*subpass*/ 0, sceneAugmentRenderer.fbo.get()};
      for (auto it = visiblePrimChunks.begin(); it < visiblePrimChunks.end(); ++it, ++j) {
        renderScheduler->enqueue(
            [&, it]() {
              auto &renderCmd = nextVkCommand();
              (*renderCmd)
                  .begin(
                      vk::CommandBufferBeginInfo{
                          vk::CommandBufferUsageFlagBits::eRenderPassContinue, &inheritance},
                      ctx.dispatcher);
              /// wireframe
              {
                auto viewport
                    = vk::Viewport()
                          .setX(0 /*offsetx*/)
                          .setY(vkCanvasExtent.height /*-offsety*/)
                          .setWidth(float(vkCanvasExtent.width))
                          .setHeight(-float(
                              vkCanvasExtent.height))  // negative viewport, opengl conformant
                          .setMinDepth(0.0f)
                          .setMaxDepth(1.0f);

                (*renderCmd).setViewport(0, {viewport});
              }
              (*renderCmd).setScissor(0, {vk::Rect2D(vk::Offset2D(), vkCanvasExtent)});
              if (ctx.enabledDeviceFeatures.features.wideLines)
                (*renderCmd).setLineWidth(2.0f);
              else
                (*renderCmd).setLineWidth(1.0f);

              (*renderCmd)
                  .bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                      /*pipeline layout*/ sceneAugmentRenderer.wiredPipeline.get(),
                                      /*firstSet*/ 0,
                                      /*descriptor sets*/ {sceneRenderData.sceneCameraSet},
                                      /*dynamic offset*/ {0}, ctx.dispatcher);
              (*renderCmd)
                  .bindPipeline(vk::PipelineBindPoint::eGraphics,
                                sceneAugmentRenderer.wiredPipeline.get());

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
                if (!model.isParticle()) {
                  // auto transform = prim->visualTransform(sceneRenderData.currentTimeCode);
                  const auto& transform = prim->currentTimeVisualTransform();
                  (*renderCmd)
                      .pushConstants(sceneAugmentRenderer.wiredPipeline.get(),
                                     vk::ShaderStageFlagBits::eVertex, 0, sizeof(transform),
                                     &transform);
                  model.bindColor((*renderCmd), VkModel::tri);
                  model.drawColor((*renderCmd), VkModel::tri);
                }
              }
              (*renderCmd).end();
            },
            j);
      }  // end iterating chunks

      renderScheduler->wait();
      (*cmd).executeCommands(getCurrentSecondaryVkCmds(), ctx.dispatcher);

      (*cmd).endRenderPass();
#else
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
      if (ctx.enabledDeviceFeatures.features.wideLines)
        (*cmd).setLineWidth(2.0f);
      else
        (*cmd).setLineWidth(1.0f);

      (*cmd).bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                /*pipeline layout*/ sceneAugmentRenderer.wiredPipeline.get(),
                                /*firstSet*/ 0,
                                /*descriptor sets*/ {sceneRenderData.sceneCameraSet},
                                /*dynamic offset*/ {0}, ctx.dispatcher);
      (*cmd).bindPipeline(vk::PipelineBindPoint::eGraphics,
                          sceneAugmentRenderer.wiredPipeline.get());

      // for (const auto &model : sceneRenderData.models)
      for (const auto &primPtr : getCurrentVisiblePrims()) {
        // auto prim = primPtr.lock();
        auto &prim = primPtr;
        if (!prim || prim->empty()) continue;
        auto pModel = prim->queryVkTriMesh(ctx, sceneRenderData.currentTimeCode);
        if (!pModel || currentVisiblePrimsDrawn.at(prim) == 0) continue;
        const auto &model = *pModel;
        // const auto &model = prim->vkTriMesh(ctx);
        if (!model.isParticle()) {
          // auto transform = prim->visualTransform(sceneRenderData.currentTimeCode);
          const auto &transform = prim->currentTimeVisualTransform();
          (*cmd).pushConstants(sceneAugmentRenderer.wiredPipeline.get(),
                               vk::ShaderStageFlagBits::eVertex, 0, sizeof(transform), &transform);
          model.bindColor((*cmd), VkModel::tri);
          model.drawColor((*cmd), VkModel::tri);
        }
      }  // end iterating chunks

      (*cmd).endRenderPass();
#endif

#if ENABLE_PROFILE
      (*cmd).writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, queryPool.get(),
                            vkq_wireframe + 1, ctx.dispatcher);
      locTimer.tock("\tSceneEditor:: wireframe record");
#endif
    }

    /// render text overlay
    if (showIndex) {
      (*cmd).beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
      auto viewport
          = vk::Viewport()
                .setX(0 /*offsetx*/)
                .setY(0 /*-offsety*/)
                .setWidth(float(vkCanvasExtent.width))
                .setHeight(float(vkCanvasExtent.height))  // negative viewport, opengl conformant
                .setMinDepth(0.0f)
                .setMaxDepth(1.0f);
      (*cmd).setViewport(0, {viewport});
      (*cmd).setScissor(0, {vk::Rect2D(vk::Offset2D(), vkCanvasExtent)});
      (*cmd).bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                /*pipeline layout*/ sceneAugmentRenderer.overlayPipeline.get(),
                                /*firstSet*/ 0,
                                // j/*descriptor sets*/ {sceneAugmentRenderer.overlayFontSet},
                                /*descriptor sets*/ {guiRenderer->_fontDescriptorSet},
                                /*dynamic offset*/ {}, ctx.dispatcher);
      (*cmd).bindPipeline(vk::PipelineBindPoint::eGraphics,
                          sceneAugmentRenderer.overlayPipeline.get());

      std::array<vk::DeviceSize, 1> offsets{0};
      (*cmd).bindVertexBuffers(0, {(vk::Buffer)sceneAugmentRenderer.overlayTextBuffer.get()},
                               offsets, ctx.dispatcher);

      if (sceneAugmentRenderer.numLetters)
        (*cmd).draw(6 * sceneAugmentRenderer.numLetters, 1, 0, 0, ctx.dispatcher);

      (*cmd).endRenderPass();
    }

    cmd.end();

    cmd.submit(fence.get(), /*reset fence*/ true, /*reset config*/ true);

    fence.get().wait();

#if ENABLE_PROFILE
    timer.tock("SceneEditor:: record:: render wireframe & overlay text");
#endif

/// @note query timestamps
#if ENABLE_PROFILE
    u64 timestamps[2];
    auto r = ctx.device.getQueryPoolResults(
        queryPool.get(), vkq_wireframe, 2, sizeof(timestamps), timestamps, sizeof(u64),
        vk::QueryResultFlagBits::eWait | vk::QueryResultFlagBits::e64, ctx.dispatcher);
    assert(r == vk::Result::eSuccess);
    auto period = ctx.deviceProperties.properties.limits.timestampPeriod;
    double duration = (timestamps[1] - timestamps[0]) * period * 1e-6;  // ns -> ms
    fmt::print("\t\t wireframe costs {} ms\n", duration);
#endif
  }

}  // namespace zs

#undef ENABLE_PROFILE