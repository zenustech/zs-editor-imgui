#include "SceneEditor.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/euler_angles.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "imgui.h"
//
#include "ImGuizmo.h"

//
#include "editor/widgets/TreeWidgetComponent.hpp"
#include "world/World.hpp"
#include "world/geometry/SimpleGeom.hpp"
#include "world/scene/Primitive.hpp"
#include "world/scene/PrimitiveConversion.hpp"
#include "world/scene/PrimitiveTransform.hpp"
#include "world/system/ZsExecSystem.hpp"
#include "zensim/ZpcFunctional.hpp"
#include "zensim/execution/Atomics.hpp"
#include "zensim/io/MeshIO.hpp"
#include "zensim/math/matrix/Transform.hpp"
#include "zensim/zpc_tpls/stb/stb_image.h"

#if ZS_ENABLE_OPENMP
#  include "zensim/omp/execution/ExecutionPolicy.hpp"
#else
#  include "zensim/execution/ExecutionPolicy.hpp"
#endif

#define ENABLE_PROFILE 0

namespace zs {

  struct SceneCameraParams {
    glm::mat4 projection;
    glm::mat4 view;
  };
  static const char g_mesh_vert_code[] = R"(
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

// layout(push_constant) uniform Camera {
// 	mat4 projection;
// 	mat4 model;
// } params;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
// layout (location = 2) out vec2 outUV;
layout (location = 2) out vec3 outViewVec;
layout (location = 3) out vec3 outLightVec;

void main() 
{
	outColor = inColor;
    // if (inVid < 50) outColor = vec3(0, 1, 0);
	// gl_Position = cameraUbo.projection * cameraUbo.view * vec4(inPos, 1.0);
	vec4 viewPos = cameraUbo.view * params.model * vec4(inPos, 1.0);
	gl_Position = cameraUbo.projection * viewPos;

  // outUV = inUV;
	outNormal = normalize(cameraUbo.view * params.model * vec4(inNormal, 0.0f)).xyz;

	vec3 lightPos = vec3(0.0f);
	outLightVec = lightPos.xyz - viewPos.xyz;
	outViewVec = viewPos.xyz - vec3(0.0f);
}
)";
  static const char g_mesh_frag_code[] = R"(
#version 450

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
// layout (location = 2) in vec2 inUV;
layout (location = 2) in vec3 inViewVec;
layout (location = 3) in vec3 inLightVec;

layout (push_constant) uniform fragmentPushConstants {
    layout(offset = 16 * 4) int objId;
    // layout(offset = 16 * 4 + 4) int useTexture;
} pushConstant;

// layout (set = 1, binding = 0) uniform sampler2D TEX_0;

layout (location = 0) out vec4 outFragColor;
layout (location = 1) out ivec3 outTag;

void main() 
{
    // if (pushConstant.objId == 0) outFragColor = vec4(vec3(0.5), 1.0);		
	vec3 N = normalize(inNormal);
	vec3 L = normalize(inLightVec);
	vec3 V = normalize(inViewVec);
	vec3 ambient = vec3(0.2);
	vec3 diffuse = max(dot(N, L), 0.0) * vec3(1.0);

  vec3 clr = inColor.rgb; //vec3(0.f);
  // if (pushConstant.useTexture > 0) {
  //   clr = texture(TEX_0, inUV).rgb;
  // } else {
  //   clr = inColor.rgb;
  // }
	outFragColor = vec4((ambient + diffuse) * clr, 0.5);
  outTag.r = pushConstant.objId;
  outTag.g = -1;
  outTag.b = gl_PrimitiveID;
}
)";

  static const char g_mesh_pbr_vert_code[] = R"(
#version 450
precision highp float;

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
// layout (location = 2) out vec2 outUV;
layout (location = 2) out vec4 outViewVec; // view offset and positive view depth
layout (location = 3) out vec3 outWorldPos;

void main() 
{
	outColor = inColor;
	vec4 worldPos = params.model * vec4(inPos, 1.0);
	gl_Position = cameraUbo.view * worldPos;
  outViewVec.w = -gl_Position.z; // get positive view space depth
  gl_Position = cameraUbo.projection * gl_Position;

	outNormal = normalize((params.model * vec4(inNormal, 0.0f)).xyz);
	outViewVec.xyz = ((inverse(cameraUbo.view) * vec4(0.0, 0.0, 0.0, 1.0)) - worldPos).xyz;
  outWorldPos = worldPos.xyz;
}
)";

  static const char g_mesh_pbr_frag_code[] = R"(
#version 450
precision highp float;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec4 inViewVec; // view offset and positive view depth
layout (location = 3) in vec3 inWorldPos;

struct LightInfo{
  vec4 sphere; // xyz: world space position, w: radius
  vec4 color; // rgb: color, a: intensity
};

struct ShadeContext{
  vec3 lambert;
  vec3 N;
  vec3 V;
  float NV;
  float a;
  float a2;
};

layout (push_constant) uniform fragmentPushConstants {
    layout(offset = 16 * 4) int objId;
    layout(offset = 16 * 4 + 8) ivec2 clusterCountVec; // x: cluster count per line, y: per depth
} pushConstant;
layout (set = 1, binding = 0) readonly buffer LightList {
  LightInfo lights[];
};
layout (set = 1, binding = 1) readonly buffer ClusterLightIndexInfo {
  int lightIndices[];
};

layout (location = 0) out vec4 outFragColor;
layout (location = 1) out ivec3 outTag;

const float PI = 3.1415926;
const float invPI = 1.0 / PI;

const float ambient = 0.02;
const float roughness = 0.9;
const float metallic = 0.1;

int getClusterIndex(){
  ivec3 cid_vec = ivec3(gl_FragCoord.xy / 32, floor(inViewVec.w / 625.0));
  return int(dot(cid_vec, ivec3(1, pushConstant.clusterCountVec.x, pushConstant.clusterCountVec.y)));
}

float GGX(float cosine, float k){
  return cosine / (cosine * (1.0 - k) + k);
}

vec3 CookTorrance(in LightInfo lightInfo, in ShadeContext context){
  // vector from fragment to light source in world space
  vec3 L = lightInfo.sphere.xyz - inWorldPos.xyz;
  float invLightDist2 = 1.0 / dot(L, L);
  L = normalize(L);

  /* Lambert */
  // this variable has been calculated in ShadeContext
  // vec3 lambert = inColor * invPI;

  /* Specular */
  // normal distribution
  vec3 HalfVec = normalize(L + context.V);
  float NH = max(0.0, dot(context.N, HalfVec));
  float D = context.a2 / (PI * pow(NH * NH * (context.a2 - 1.0) + 1.0, 2.0));
  // Geometry
  float k = pow(context.a + 1.0, 2.0) * 0.125;
  float NL = max(0.0, dot(context.N, L));
  float G = GGX(NL, k) * GGX(context.NV, k);
  // fresnel
  vec3 f0 = mix(vec3(0.05), inColor, metallic);
  float _temp = 1.0 - context.NV;
  _temp = _temp * _temp;
  _temp = _temp * _temp * (1.0 - context.NV); // pow(1.0 - context.NV, 5.0)
  vec3 F = f0 + (1.0 - f0) * _temp;
  vec3 specular = F * (D * G / max(0.002, 4.0 * context.NV * NL));

  vec3 Kd = clamp((1.0 - F), 0.0, 1.0) * (1.0 - metallic);
  vec3 light = (NL * lightInfo.color.a * invLightDist2) * lightInfo.color.rgb;

  return (Kd * context.lambert + specular) * light;
}

void main() {
  ShadeContext context;
  context.lambert = inColor * invPI;
  context.N = normalize(inNormal);
  context.V = normalize(inViewVec.xyz);
  // pre-calculation
  context.NV = max(0.0, dot(context.N, context.V));
  context.a = roughness * roughness;
  context.a2 = context.a * context.a;

  int cid_base = getClusterIndex() << 5;
  int sizeOfClusterLights = lightIndices[cid_base];
  outFragColor.rgb = vec3(0.0);
  for (int i = 1; i <= sizeOfClusterLights; ++i){
    int lid = lightIndices[cid_base + i];
    outFragColor.rgb += CookTorrance(lights[lid], context);
  }

  // fake environment lighting
  // outFragColor.rgb += context.lambert * ambient;
  // outFragColor.rgb = vec3(floor(gl_FragCoord.xy / 32) * 0.01, floor(inViewVec.w / 625.0) * 0.03);
  // outFragColor.rgb = vec3(lightIndices[cid_base + 29], lightIndices[cid_base + 30], lightIndices[cid_base + 31]) * 0.001;
  // outFragColor.rgb = vec3(sizeOfClusterLights / 20.0, sizeOfClusterLights / 20.0, inViewVec.w * 0.0025);

  // outFragColor.r = sizeOfClusterLights / 20.0;
  outFragColor.a = 1.0;

  outTag.r = pushConstant.objId;
  outTag.g = -1;
  outTag.b = gl_PrimitiveID;
}
)";

  static const char g_mesh_vert_bindless_code[] = R"(
#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in ivec2 inTexId;

layout (set = 0, binding = 0) uniform SceneCamera {
	mat4 projection;
	mat4 view;
} cameraUbo;

layout (push_constant) uniform Model {
 	mat4 model;
} params;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec2 outUV;
layout (location = 2) out vec3 outViewVec;
layout (location = 3) out vec3 outLightVec;
layout (location = 4) out ivec2 outTexId;

void main() 
{
	vec4 viewPos = cameraUbo.view * params.model * vec4(inPos, 1.0);
	gl_Position = cameraUbo.projection * viewPos;

  outUV = inUV;
  outTexId = inTexId;
	outNormal = normalize(cameraUbo.view * params.model * vec4(inNormal, 0.0f)).xyz;

	vec3 lightPos = vec3(0.0f);
	outLightVec = lightPos.xyz - viewPos.xyz;
	outViewVec = viewPos.xyz - vec3(0.0f);
}
)";
  static const char g_mesh_frag_bindless_code[] = R"(
#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inViewVec;
layout (location = 3) in vec3 inLightVec;
layout (location = 4) flat in ivec2 inTexId; // <texture type, texture id>

layout (push_constant) uniform fragmentPushConstants {
    layout(offset = 16 * 4) int objId;
} pushConstant;

// layout (set = 1, binding = 0) uniform g_uniformBuffers[1000];
layout (set = 1, binding = 1) uniform sampler2D g_tex2ds[];
// layout (set = 1, binding = 2) buffer g_storageBuffers[1000];
layout (set = 1, binding = 3, r32f) uniform image2D g_storageImages[];
// layout (set = 1, binding = 4) uniform subpassInput g_inputs[1000];
// layout (set = 1, binding = 0) uniform sampler3D g_tex3ds[];

layout (location = 0) out vec4 outFragColor;
layout (location = 1) out ivec3 outTag;

void main() 
{
	vec3 N = normalize(inNormal);
	vec3 L = normalize(inLightVec);
	vec3 V = normalize(inViewVec);
	vec3 ambient = vec3(0.2);
	vec3 diffuse = max(dot(N, L), 0.0) * vec3(1.0);

  vec3 clr = vec3(0.f);
  if (nonuniformEXT(inTexId.x) == 0 && nonuniformEXT(inTexId.y) >= 0)
    clr = texture(g_tex2ds[nonuniformEXT(inTexId.y)], inUV).rgb;
  // if (pushConstant.useTexture > 0) {
  //   clr = texture(TEX_0, inUV).rgb;
  // } else {
  //   clr = inColor.rgb;
  // }
	outFragColor = vec4((ambient + diffuse) * clr, 0.5);
  outTag.r = pushConstant.objId;
  outTag.g = -1;
  outTag.b = gl_PrimitiveID;
}
)";

  static const char g_mesh_point_vert_code[] = R"(
#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inColor;

layout (set = 0, binding = 0) uniform SceneCamera {
	mat4 projection;
	mat4 view;
} cameraUbo;

layout(push_constant) uniform Model {
	mat4 model;
	float radius;
} params;

// layout(push_constant) uniform Params {
// 	mat4 projection;
// 	mat4 model;
//     float radius;
// } params;

layout (location = 0) out vec3 outColor;

void main() 
{
	outColor = inColor;
	gl_Position = cameraUbo.projection * cameraUbo.view * params.model * vec4(inPos, 1.0);

    vec3 posEye = vec3(cameraUbo.view * vec4(inPos, 1.0));
    float dist = length(posEye);
	gl_PointSize = max(1.0, params.radius * 1.0 / dist);
}
)";
  static const char g_mesh_point_frag_code[] = R"(
#version 450

layout (location = 0) in vec3 inColor;

layout (location = 0) out vec4 outFragColor;
layout (location = 1) out ivec3 outTag;

void main() 
{
    const vec3 lightDir = vec3(0.577, 0.577, 0.577);
    vec2 coor = gl_PointCoord * 2 - 1;
    float len2 = dot(coor, coor);
    if (len2 > 1)
        discard;
    
    vec3 oColor;
    {
        vec3 N;
        N.xy = gl_PointCoord * vec2(2.0, -2.0) + vec2(-1.0, 1.0);
        float mag = dot(N.xy, N.xy);
        N.z = sqrt(1.0-mag);

        // calculate lighting
        float diffuse = max(0.0, dot(lightDir, N) * 0.6 + 0.4);
        oColor = inColor * diffuse;
    }
	outFragColor = vec4(oColor, 0.5);		
    outTag.r = -1;
}
)";
  static const char g_grid_vert_code[] = R"(
#version 450
precision highp float;

// layout (location = 0) in vec3 inPos;

layout (location = 0) out vec3 nearPos;
layout (location = 1) out vec3 farPos;
layout (location = 2) out mat4 viewProj;

layout (set = 0, binding = 0) uniform SceneCamera {
	mat4 projection;
	mat4 view;
} camera;

layout(push_constant) uniform Context{
  float farDepth;
} context;

vec3 positions[4] = {
    vec3(-1.0, -1.0, 0.0),
    vec3(-1.0, 1.0, 0.0),
    vec3(1.0, 1.0, 0.0),
    vec3(1.0, -1.0, 0.0)
};

void main() 
{
    viewProj = camera.projection * camera.view;

    vec3 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 1.0);

    mat4 inv = inverse(camera.view) * inverse(camera.projection);
    vec4 origin = inv * vec4(pos.xy, 1.0 - context.farDepth, 1.0);
    nearPos = origin.xyz / origin.w;

    origin = inv * vec4(pos.xy, context.farDepth, 1.0);
    farPos = origin.xyz / origin.w;
}
    )";
  static const char g_grid_frag_code[] = R"(
#version 450
precision highp float;

layout (location = 0) in vec3 nearPos;
layout (location = 1) in vec3 farPos;
layout (location = 2) in mat4 viewProj;

layout (location = 0) out vec4 outFragColor;
layout (location = 1) out ivec3 outTag;

vec3 getClipSpace(vec3 pos){
    vec4 p = viewProj * vec4(pos, 1.0);
    return p.xyz / p.w;
}

vec3 getNearestGridPos(vec3 pos, float interval){
    pos = pos / interval;
    vec3 fpos = fract(pos);
    vec3 closeGrid = step(0.5, fpos);
    fpos = abs(closeGrid - fpos);
    pos.x = floor(pos.x) + closeGrid.x;
    pos.y = 0.0;
    pos.z = floor(pos.z) + closeGrid.z;
    return pos * interval;
}

void main() 
{
    float t = -nearPos.y / (farPos.y - nearPos.y);
    const float frontSide = step(0.0, t);
    float onThePlane = 1.0 - step(0.00001, abs(nearPos.y));
    float alpha = frontSide;
    vec3 color = vec3(0.2);

    vec3 zeroPos = nearPos + t * (farPos - nearPos); // world position on y = 0
    vec3 zeroClipPos = getClipSpace(zeroPos); // clip space position of y = 0
    gl_FragDepth = zeroClipPos.z;

    // view fading
    const vec3 viewOffset = zeroPos - nearPos;
    const float viewCos = abs(normalize(viewOffset).y);
    alpha *= smoothstep(0.08716, 0.5, viewCos);

    // calculate current grid level
    float height = abs(nearPos.y);
    float threshold = 30.0;
    float level = 1.0;
    while (threshold < height){
      threshold *= 8.0;
      level *= 10.0;
    }

    // compute distance to the nearest grid line
    const float lineWidth = 1.2;
    vec3 derivative = abs(fwidth(zeroPos));
    vec3 gridPos = getNearestGridPos(zeroPos, level);
    vec2 offsetToNearestGrid = abs(zeroPos - gridPos).xz / (derivative.xz * lineWidth);
    float gridDist = min(offsetToNearestGrid.x, offsetToNearestGrid.y);
    const float onTheLine = 1.0 - smoothstep(0.667, 1.0, gridDist); // anti-aliasing with smoothstep
    alpha *= onTheLine;

    // grid level fading
    vec3 nextLevelGridPos = abs(gridPos) / (level * 10.0);
    if ((fract(nextLevelGridPos.x) >= 0.01 || offsetToNearestGrid.x > 1.0)
      && (fract(nextLevelGridPos.z) >= 0.01 || offsetToNearestGrid.y > 1.0)){
      alpha *= 1.0 - smoothstep(0.1, threshold, height);
    }

    // axis color
    if (abs(gridPos.x) <= 0.5 && offsetToNearestGrid.x <= 1.0){
        color.rgb = vec3(0.1, 0.1, 1.0);
        alpha = frontSide * onTheLine;
    } else if (abs(gridPos.z) <= 0.5 && offsetToNearestGrid.y <= 1.0){
        color.rgb = vec3(1.0, 0.1, 0.1);
        alpha = frontSide * onTheLine;
    }

    // special judge when camera on y = 0
    if (onThePlane > 0.5){
        color = vec3(0.2);
        gl_FragDepth = 0.0;
        alpha = 1.0;
    }

    outFragColor = vec4(color, alpha);
}
    )";

  void SceneEditor::setup(VulkanContext &ctx, ImguiVkRenderer &renderer) {
    guiRenderer = &renderer;

    // interactionMode = SceneEditorRoamingMode{*this};
    interactionMode.turnTo(_roaming, *this);
    // imgui
    guizmoUseSnap = true;
    viewportFocused = viewportHovered = false;
    canvasLocalMousePos = zs::vec<float, 2>::zeros();
    vkCanvasExtent.width = 1280;
    vkCanvasExtent.height = 720;
    focusPrimPtr = {}, hoveredPrimPtr = {};
    // setup interaction
    onVisiblePrimsChanged.connect([this](const std::vector<Weak<ZsPrimitive>> &prims) {
      currentVisiblePrimsSet.clear();
      zs::function<void(Weak<ZsPrimitive>)> gatherChildren = [&](Weak<ZsPrimitive> prim_) {
        auto prim = prim_.lock();
        if (prim) {
          currentVisiblePrimsSet.emplace(prim);
          auto nChilds = prim->numChildren();
          for (int j = 0; j < nChilds; ++j) {
            auto ch = prim->getChild(j);
            if (currentVisiblePrimsSet.emplace(ch).second) gatherChildren(ch);
          }
        }
      };
      for (auto &prim : prims) gatherChildren(prim);
      currentVisiblePrims.clear();
      currentVisiblePrims.reserve(currentVisiblePrimsSet.size());
      currentVisiblePrimsDrawnTags.resize(currentVisiblePrimsSet.size());
      primIdToVisPrimId.clear();
      currentVisiblePrimsDrawn.clear();
      sceneLighting.lightList.clear();
      // i32 visCnt = 0;
      for (auto &&prim : currentVisiblePrimsSet) {
        auto p = prim.lock();
        if (p->_localPrims.contains(zs::prim_type_e::Light_)) {
          // emplace light source into light list
          registerLightSource(p->localLightPrims());
        } else {
          // map discrete prim ids to continuous
          primIdToVisPrimId[p->id()] = currentVisiblePrims.size();
          currentVisiblePrims.push_back(p.get());
          currentVisiblePrimsDrawn[p.get()] = 0;
          // if (!p->empty()) visCnt++;
        }
      }

      // setup flag for sync
    });

    // setup vis contents state machine
    _visBufferStatus.reset(new VisBufferHandler);
    _visBufferStatus->_state = DisplayingVisBuffers{this};
    zs_resources().onPrimInFlightChanged().assign(
        _visBufferStatus->_id, [this, status = _visBufferStatus.get()](i32 cntDelta) {
          _visBufferStatus->process(VisBufferTaskEvent{cntDelta});
        });

    // vk objs
    fence = Fence(ctx, true);
    cmd = ctx.createCommandBuffer(vk_cmd_usage_e::reset, vk_queue_e::graphics, false);
    // TODO: secondary cmds for parallel rendering
    queryPool = ctx.createQueryPool(vk::QueryType::eTimestamp, vkq_total);

    initialRenderSetup();

    /// shaders
    sceneRenderer.vertShader
        = ctx.createShaderModuleFromGlsl(g_mesh_pbr_vert_code /*g_mesh_vert_code*/,
                                         vk::ShaderStageFlagBits::eVertex, "default_mesh_vert");
    sceneRenderer.fragShader
        = ctx.createShaderModuleFromGlsl(g_mesh_pbr_frag_code /*g_mesh_frag_code*/,
                                         vk::ShaderStageFlagBits::eFragment, "default_mesh_frag");
    ctx.acquireSet(sceneRenderer.fragShader.get().layout(1), sceneLighting.lightTableSet);

    // texture
    ResourceSystem::load_shader(ctx, "default_texture_preview.vert",
                                vk::ShaderStageFlagBits::eVertex, g_mesh_vert_bindless_code);
    ResourceSystem::load_shader(ctx, "default_texture_preview.frag",
                                vk::ShaderStageFlagBits::eFragment, g_mesh_frag_bindless_code);

    sceneRenderer.pointVertShader = ctx.createShaderModuleFromGlsl(
        g_mesh_point_vert_code, vk::ShaderStageFlagBits::eVertex, "default_mesh_point_vert");
    sceneRenderer.pointFragShader = ctx.createShaderModuleFromGlsl(
        g_mesh_point_frag_code, vk::ShaderStageFlagBits::eFragment, "default_mesh_point_frag");

    sceneRenderer.gridVertShader = ctx.createShaderModuleFromGlsl(
        g_grid_vert_code, vk::ShaderStageFlagBits::eVertex, "default_grid_vert");
    sceneRenderer.gridFragShader = ctx.createShaderModuleFromGlsl(
        g_grid_frag_code, vk::ShaderStageFlagBits::eFragment, "default_grid_frag");

    /// sampler
    sampler = ctx.createDefaultSampler();

    setupRenderResources();
    ResourceSystem::load_missing_texture(ctx, zs_resources().get_shader("imgui.frag"), /*set no*/ 0,
                                         /*binding*/ 0);
    loadSampleModels();

#if ZS_ENABLE_USD
    auto fn = abs_exe_directory() + "/resource/usd/" + g_defaultUsdFile;
    auto defaultScene = ResourceSystem::load_usd(fn, g_defaultUsdLabel);
    ResourceSystem::register_widget(
        /*label*/ g_defaultUsdLabel, ui::build_usd_tree_node(defaultScene->getRootPrim().get()));
#endif

#if ZS_ENABLE_USD
    // loadSampleScene();
    // loadUVTestScene();
#endif

    ///
    setupLightingResources();
    setupOITResources();
    setupPickResources();
    setupAugmentResources();
    setupOutlineResources();
#if ENABLE_OCCLUSION_QUERY
    setupOcclusionQueryResouces();
#endif  // ENABLE_OCCLUSION_QUERY

    /// update ui-related states
    onVisiblePrimsChanged.emit(getCurrentScenePrims());
  }

  void SceneEditor::setupRenderResources() {
    auto &renderer = *guiRenderer;
    auto &ctx = this->ctx();

    // descriptor set
    vk::DescriptorSet &renderedSceneColorSet = sceneAttachments.renderedSceneColorSet;
    ctx.acquireSet(renderer.getFragShader().layout(0), renderedSceneColorSet);
    /// @note for imgui to validate this image resource
    renderer.registerImage(renderedSceneColorSet);

    // renderpass
    // assert(sampleBits != vk::SampleCountFlagBits::e1 && "only support multi-sampling ftm");
    auto rpBuilder = ctx.renderpass().setNumPasses(1);
    if (sampleBits != vk::SampleCountFlagBits::e1) {
      rpBuilder
          // 0
          .addAttachment(colorFormat, vk::ImageLayout::eUndefined,
                         vk::ImageLayout::eColorAttachmentOptimal, true, sampleBits)
          // 1
          .addAttachment(depthFormat, vk::ImageLayout::eUndefined,
                         vk::ImageLayout::eDepthStencilAttachmentOptimal, true, sampleBits)
          // 2
          .addAttachment(colorFormat, vk::ImageLayout::eUndefined,
                         vk::ImageLayout::eShaderReadOnlyOptimal, false,
                         vk::SampleCountFlagBits::e1)
          // 3
          .addAttachment(depthFormat, vk::ImageLayout::eUndefined,
                         vk::ImageLayout::eShaderReadOnlyOptimal,  // used as input attachment
                         false, vk::SampleCountFlagBits::e1)
          // 4
          .addAttachment(pickFormat, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, true,
                         sampleBits)
          // 5
          .addAttachment(pickFormat, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, false,
                         vk::SampleCountFlagBits::e1)
          .addSubpass({0, 4}, /*depthStencilRef*/ 1, /*colorResolveRef*/ {2, 5},
                      /*depthStencilResolveRef*/ 3, /*inputAttachments*/ {});
    } else {
      rpBuilder
          // color
          .addAttachment(colorFormat, vk::ImageLayout::eUndefined,
                         vk::ImageLayout::eShaderReadOnlyOptimal, true, vk::SampleCountFlagBits::e1)
          // depth
          .addAttachment(depthFormat, vk::ImageLayout::eUndefined,
                         vk::ImageLayout::eShaderReadOnlyOptimal, true, vk::SampleCountFlagBits::e1)
          // pick
          .addAttachment(pickFormat, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, true,
                         vk::SampleCountFlagBits::e1)
          .addSubpass({0, 2}, /*depthStencilRef*/ 1, /*colorResolveRef*/ {},
                      /*depthStencilResolveRef*/ -1, /*inputAttachments*/ {});
    }
    sceneRenderer.renderPass = rpBuilder.build();

    // opaque pipeline
    auto pipelineBuilder = ctx.pipeline();
    pipelineBuilder.setRenderPass(sceneRenderer.renderPass.get(), 0)
        .setRasterizationSamples(sampleBits)
        .setCullMode(vk::CullModeFlagBits::eBack)
#if 1
        .enableDepthBias(SceneEditor::reversedZ ? -2.f : 1.25f,
                         SceneEditor::reversedZ ? -2.f : 1.75f)  // *
#else
        .enableDepthBias()
#endif
        // .setTopology(vk::PrimitiveTopology::ePointList)
        // .setTopology(vk::PrimitiveTopology::eLineList)
        .setTopology(vk::PrimitiveTopology::eTriangleList)
        .setShader(sceneRenderer.vertShader.get())
        .setShader(sceneRenderer.fragShader.get())
        .setBlendEnable(false)
        .setDepthCompareOp(SceneEditor::reversedZ ? vk::CompareOp::eGreaterOrEqual
                                                  : vk::CompareOp::eLessOrEqual)
        .setPushConstantRanges(
            {vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)},
             vk::PushConstantRange{vk::ShaderStageFlagBits::eFragment, sizeof(glm::mat4),
                                   sizeof(i32)},
             vk::PushConstantRange{vk::ShaderStageFlagBits::eFragment,
                                   sizeof(glm::mat4) + 2 * sizeof(i32), sizeof(glm::ivec2)}})
        .setBindingDescriptions(VkModel::get_binding_descriptions_normal_color(VkModel::tri))
        .setAttributeDescriptions(VkModel::get_attribute_descriptions_normal_color(VkModel::tri));
    sceneRenderer.opaquePipeline = pipelineBuilder.build();

    // texture preview pipeline
    {
      auto &texturePreviewVertShader = ResourceSystem::get_shader("default_texture_preview.vert");
      auto &texturePreviewFragShader = ResourceSystem::get_shader("default_texture_preview.frag");

      sceneRenderer.bindlessPipeline
          = pipelineBuilder.setShader(texturePreviewFragShader)
                .setDescriptorSetLayouts({}, true)
                .addDescriptorSetLayout(ctx.bindlessDescriptorSetLayout, 1)
                .setShader(texturePreviewVertShader)
                .setDepthCompareOp(SceneEditor::reversedZ ? vk::CompareOp::eGreaterOrEqual
                                                          : vk::CompareOp::eLessOrEqual)
                .setPushConstantRanges(
                    {vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)},
                     vk::PushConstantRange{vk::ShaderStageFlagBits::eFragment, sizeof(glm::mat4),
                                           sizeof(i32)}})
                .setBindingDescriptions(VkModel::get_binding_descriptions_uv(VkModel::tri))
                .setAttributeDescriptions(VkModel::get_attribute_descriptions_uv(VkModel::tri))
                .build();

#if 0
      const auto &t = texturePreviewFragShader.layouts();
      for (const auto &[no, layout] : t) {
        fmt::print("iterating set {}, which has {} bindings.\n", no, layout.numBindings());
      }
      texturePreviewFragShader.displayLayoutInfo();
#endif
    }
    // puts("checking...");
    // getchar();

    // transparent pipeline
    pipelineBuilder.setRenderPass(sceneRenderer.renderPass.get(), 0)
        .setRasterizationSamples(sampleBits)
        .setCullMode(vk::CullModeFlagBits::eBack)
#if 1
        .enableDepthBias(SceneEditor::reversedZ ? -2.f : 1.25f,
                         SceneEditor::reversedZ ? -2.f : 1.75f)  // *
#else
        .enableDepthBias()
#endif
        .setTopology(vk::PrimitiveTopology::eTriangleList)
        // clear existing descriptor set layouts
        .setDescriptorSetLayouts({}, true)
        .setShader(sceneRenderer.vertShader.get())
        .setShader(sceneRenderer.fragShader.get())
        .setDepthCompareOp(SceneEditor::reversedZ ? vk::CompareOp::eGreaterOrEqual
                                                  : vk::CompareOp::eLessOrEqual)
        .setDepthWriteEnable(false)
        .setDepthTestEnable(true)
        .setBlendEnable(true)
        .setColorBlendOp(vk::BlendOp::eAdd)
        .setColorBlendFactor(vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha)
        .setAlphaBlendOp(vk::BlendOp::eAdd)
        .setAlphaBlendFactor(vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha)
        .setPushConstantRanges(
            {vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)},
             vk::PushConstantRange{vk::ShaderStageFlagBits::eFragment, sizeof(glm::mat4),
                                   sizeof(i32)}})
        .setBindingDescriptions(VkModel::get_binding_descriptions_normal_color(VkModel::tri))
        .setAttributeDescriptions(VkModel::get_attribute_descriptions_normal_color(VkModel::tri));
    sceneRenderer.transparentPipeline = pipelineBuilder.build();

    sceneRenderer.pointOpaquePipeline
        = pipelineBuilder.setTopology(vk::PrimitiveTopology::ePointList)
              .setShader(sceneRenderer.pointVertShader.get())
              .setShader(sceneRenderer.pointFragShader.get())
              .setBlendEnable(false)
              .setDepthCompareOp(SceneEditor::reversedZ ? vk::CompareOp::eGreaterOrEqual
                                                        : vk::CompareOp::eLessOrEqual)
              .setPushConstantRange(vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0,
                                                          sizeof(sceneRenderer.pointVertParams)})
              .setBindingDescriptions(VkModel::get_binding_descriptions(VkModel::point))
              .setAttributeDescriptions(VkModel::get_attribute_descriptions(VkModel::point))
              .build();
    sceneRenderer.pointTransparentPipeline
        = pipelineBuilder.setTopology(vk::PrimitiveTopology::ePointList)
              .setShader(sceneRenderer.pointVertShader.get())
              .setShader(sceneRenderer.pointFragShader.get())
              .setBlendEnable(true)
              .setDepthWriteEnable(false)
              .setDepthCompareOp(SceneEditor::reversedZ ? vk::CompareOp::eGreaterOrEqual
                                                        : vk::CompareOp::eLessOrEqual)
              .setPushConstantRange(vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0,
                                                          sizeof(sceneRenderer.pointVertParams)})
              .setBindingDescriptions(VkModel::get_binding_descriptions(VkModel::point))
              .setAttributeDescriptions(VkModel::get_attribute_descriptions(VkModel::point))
              .build();

    {
      // Grid Rendering Pipeline
      auto pipelineBuilder = ctx.pipeline();
      pipelineBuilder.setRenderPass(sceneRenderer.renderPass.get(), 0)
          .setRasterizationSamples(sampleBits)
          .setCullMode(vk::CullModeFlagBits::eNone)
          .setTopology(vk::PrimitiveTopology::eTriangleFan)
          .setShader(sceneRenderer.gridVertShader.get())
          .setShader(sceneRenderer.gridFragShader.get())
          .setDepthWriteEnable(false)
          .setDepthCompareOp(SceneEditor::reversedZ ? vk::CompareOp::eGreaterOrEqual
                                                    : vk::CompareOp::eLessOrEqual)
          .setPushConstantRanges(
              {vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0, sizeof(float)}});
      sceneRenderer.gridPipeline = pipelineBuilder.build();
    }

    rebuildSceneFbos();

    ///
    /// scene camera setup
    ///
    sceneRenderData.camera = Camera();
    auto &camera = sceneRenderData.camera.get();
    // assert(getWidget()->getZsUserPointer());  // widget already initialized
    getWidget();
    _camCtrl.trackCamera(camera, *this);

    // camera.type = Camera::CameraType::lookat;
    camera.type = Camera::CameraType::firstperson;
    camera.setReversedZ(SceneEditor::reversedZ);
    camera.setPosition(glm::vec3(0.0f, 0.0f, -3.4f));
    camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.setPerspective(45.0f, (float)vkCanvasExtent.width / (float)vkCanvasExtent.height, 0.01f,
                          20000.0f);
    camera.updateViewMatrix();

    {
      // camera uniform buffer setup
      SceneCameraParams params;
      params.projection = camera.matrices.perspective;
      params.view = camera.matrices.view;

      sceneRenderData.sceneCameraUbo = ctx.createBuffer(
          sizeof(SceneCameraParams), vk::BufferUsageFlagBits::eUniformBuffer,
          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eDeviceLocal);
      sceneRenderData.sceneCameraUbo.get().map();

      std::memcpy(sceneRenderData.sceneCameraUbo.get().mappedAddress(), &params, sizeof(params));

      vk::DescriptorSet &sceneCameraSet = sceneRenderData.sceneCameraSet;
      ctx.acquireSet(sceneRenderer.vertShader.get().layout(0), sceneCameraSet);

      vk::DescriptorBufferInfo bufferInfo = sceneRenderData.sceneCameraUbo.get().descriptorInfo();
      zs::DescriptorWriter writer{ctx, sceneRenderer.vertShader.get().layout(0)};
      writer.writeBuffer(0, &bufferInfo);
      writer.overwrite(sceneCameraSet);
    }
  }

  void SceneEditor::loadSampleModels() {
    auto &ctx = this->ctx();
    auto loadModel = [&](const std::string &name, const glm::mat4 &trans, bool opaque = true) {
#if RESOURCE_AT_RELATIVE_PATH
      std::string fn = fmt::format("{}/resource/models/{}.obj", abs_exe_directory(), name);
#else
      std::string fn = fmt::format("{}/resource/models/{}.obj", std::string{AssetDirPath}, name);
#endif
      Mesh<float, 3, u32, 3> triMesh;
      if (!read_tri_mesh_obj(fn, triMesh))
        throw std::runtime_error(fmt::format("load obj mesh [{}] failed!", fn));
      fmt::print("done loading model from {}.\n", fn);

      glm::vec3 scale;
      glm::quat rotation;
      glm::vec3 translation;
      glm::vec3 skew;
      glm::vec4 perspective;
      glm::decompose(trans, scale, rotation, translation, skew, perspective);
      rotation = glm::conjugate(rotation);
      auto rot = glm::eulerAngles(rotation);

      auto tozsvec = [](const glm::vec3 &v) { return vec<float, 3>{v[0], v[1], v[2]}; };
      // sceneRenderData.models.push_back(
      //    VkModel(ctx, triMesh, tozsvec(translation), tozsvec(rot),
      //    tozsvec(scale)));

      // auto prim = std::make_shared<ZsPrimitive>(trimesh_to_primitive(triMesh));
      auto prim = Shared<ZsPrimitive>(build_primitive_from_trimesh(triMesh));
      zs_resources().register_scene_primitive(g_defaultSceneLabel, name, prim);
      prim->label() = name;
      prim->details().setTransform(trans);
      prim->details().refIsOpaque() = opaque;
    };

    loadModel("monkey", glm::mat4(1.f), false);
    loadModel("Sharkey", glm::translate(glm::mat4(1.f), glm::vec3{-2, 0, 0})
                             * glm::eulerAngleXYZ<float>(0.1, 0.2, 0.3)
                             * glm::scale(glm::mat4(1.f), glm::vec3(1, 1.1, 1.2)));
    loadModel("torus-tri-10k", glm::translate(glm::mat4(1.f), glm::vec3{2.5, 0, 0}));
  }

  void SceneEditor::loadSampleScene() {
    auto &ctx = this->ctx();
    auto plugin = zs_get_scene_manager(zs_get_world());

    zs::SceneConfig conf;
    // "/home/mine/Codes/zpc_poc/assets/HumanFemale/HumanFemale.walk.usd"
    // "E:/Kitchen_set/Kitchen_set.usd"
    // conf.setString("srcPath", "E:/home_usd/result/start.usd");
    conf.setString("srcPath", "E:/Kitchen_set/Kitchen_set.usd");

    auto &cam = sceneRenderData.camera.get();
    cam.position = glm::vec3(-154.649, -138.206, -291.733);
    cam.rotation = glm::vec3(-39.5, -1.4, 0.0);
    cam.updateViewMatrix();

    auto scene = plugin->createScene("test");
    scene->openScene(conf);
    auto root = scene->getRootPrim();
    auto geom = Shared<ZsPrimitive>(build_primitive_from_usdprim(root));
    zs_resources().register_scene_primitive(g_defaultSceneLabel, "test", geom);

    // rootHolder->getName()
    ResourceSystem::register_widget("test_usd",
                                    ui::build_usd_tree_node(scene->getRootPrim().get()));
  }

  void SceneEditor::loadUVTestScene() {
    auto &ctx = this->ctx();
    auto plugin = zs_get_scene_manager(zs_get_world());
    auto scene = plugin->createScene("testUV");

#define GEOM_PRIM_TEST(name, path, T)                                                     \
  auto prim##name = scene->createPrim(path);                                              \
  auto _geom##name = trimesh_to_primitive(create##T());                                   \
  assign_primitive_to_usdprim(_geom##name, prim##name, 0.0);                              \
  auto geom##name = std::make_shared<ZsPrimitive>(usdmesh_to_primitive(prim##name, 0.0)); \
  geom##name->details().setTransform(eye);                                                \
  zs_resources().register_scene_primitive(g_defaultSceneLabel, prim##name->getName(), geom##name);

    zs::SceneConfig conf;
    // conf.set("dstPath", "E:/testUV.usda");
    conf.set("dstPath", "/home/mine/Codes/zpc_poc/testUV.usda");

    glm::mat4 eye(0.0f);
    eye[0][0] = eye[1][1] = eye[2][2] = eye[3][3] = 1.0f;

    eye[3][0] = -6.0f;
#if 0
    {
      auto primPlane = scene->createPrim("/plane_");
      auto _geomPlane = trimesh_to_primitive(createPlane());
      puts("===================");
      _geomPlane.points().printDbg("points");
      _geomPlane.verts().printDbg("verts");
      _geomPlane.localTriPrims()->prims().printDbg("tris");

      _geomPlane.points().printAttrib(ATTRIB_UV_TAG, wrapt<float>{});
      puts("-------------------");

      assign_primitive_to_usdprim(_geomPlane, primPlane, 0.0);
      auto geomPlane = std::make_shared<ZsPrimitive>(usdmesh_to_primitive(primPlane, 0.0));
      (*geomPlane).points().printDbg("points");
      (*geomPlane).verts().printDbg("verts");
      (*geomPlane).localTriPrims()->prims().printDbg("tris");
      (*geomPlane).points().printAttrib(ATTRIB_UV_TAG, wrapt<float>{});
      puts("======simple=======");

      geomPlane->setLocalTransform(eye);

      setup_simple_mesh_for_poly_mesh(*geomPlane);

      (*geomPlane).points().printDbg("points");
      (*geomPlane).verts().printDbg("verts");
      (*geomPlane).localTriPrims()->prims().printDbg("tris");
      (*geomPlane).points().printAttrib(ATTRIB_UV_TAG, wrapt<float>{});
      puts("===================");
      ZsPrimitive visMesh;
      assign_simple_mesh_to_visual_mesh(*geomPlane, visMesh);

      visMesh.points().printDbg("points");
      visMesh.verts().printDbg("verts");
      visMesh.localTriPrims()->prims().printDbg("tris");
      visMesh.points().printAttrib(ATTRIB_UV_TAG, wrapt<float>{});
      visMesh.verts().printAttrib(POINT_ID_TAG, prim_id_c);
      puts("===================");
    }
    exit(0);
#endif
    {
      GEOM_PRIM_TEST(plane, "/plane", Plane);
      geomplane->details().texturePath() = "C:/codes/zpc_poc/test.png";
      ResourceSystem::load_texture(ctx, "C:/codes/zpc_poc/test.png", sceneRenderer.fragShader, 1,
                                   0);
    }

    {
      eye[3][0] = -4.0f;
      GEOM_PRIM_TEST(sphere, "/sphere", Sphere);
    }

    {
      eye[3][0] = -2.0f;
      GEOM_PRIM_TEST(capsule, "/capsule", Capsule);
    }

    {
      eye[3][0] = 0.0f;
      GEOM_PRIM_TEST(cube, "/cube", Cube);
    }

    {
      eye[3][0] = 2.0f;
      GEOM_PRIM_TEST(cylinder, "/cylinder", Cylinder);
    }

    {
      eye[3][0] = 4.0f;
      GEOM_PRIM_TEST(cone, "/cone", Cone);
    }
#undef GEOM_PRIM_TEST
  }

  void SceneEditor::rebuildAttachments() {
    rebuildSceneFbos();
    rebuildLightingFBO();
    rebuildOITFBO();

    rebuildPickFbos();
    rebuildAugmentFbo();
    rebuildOutlineFbo();
#if ENABLE_OCCLUSION_QUERY
    rebuildOcclusionQueryFbo();
#endif
  }

  void SceneEditor::updateSceneSet() {
    vk::DescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler.get();
    imageInfo.imageView = sceneAttachments.color.get();
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx().writeDescriptorSet(imageInfo, sceneAttachments.renderedSceneColorSet,
                             vk::DescriptorType::eCombinedImageSampler, 0);
  }

  void SceneEditor::rebuildSceneFbos() {
    // 2
    sceneAttachments.color
        = ctx().create2DImage(vkCanvasExtent, colorFormat,
                              /* combined image sampler */ vk::ImageUsageFlagBits::eSampled |
                                  /* storage image */ vk::ImageUsageFlagBits::eStorage
                                  | vk::ImageUsageFlagBits::eColorAttachment |
                                  /* input attachment */ vk::ImageUsageFlagBits::eInputAttachment);
    // 3
    sceneAttachments.depth
        = ctx().create2DImage(vkCanvasExtent, depthFormat,
                              vk::ImageUsageFlagBits::eSampled |
                                  // vk::ImageUsageFlagBits::eTransientAttachment |
                                  vk::ImageUsageFlagBits::eDepthStencilAttachment
                                  | (sampleBits != vk::SampleCountFlagBits::e1
                                         ? vk::ImageUsageFlagBits::eDepthStencilAttachment
                                               | vk::ImageUsageFlagBits::eTransferDst
                                         : vk::ImageUsageFlagBits::eDepthStencilAttachment)
                                  |
                                  /* input attachment */ vk::ImageUsageFlagBits::eInputAttachment,
                              vk::MemoryPropertyFlagBits::eDeviceLocal, false, true, false);
    // 5
    scenePickPass.pickBuffer
        = ctx().create2DImage(vkCanvasExtent, pickFormat,
                              /* combined image sampler */ vk::ImageUsageFlagBits::eSampled |
                                  /* storage image */ vk::ImageUsageFlagBits::eStorage
                                  | vk::ImageUsageFlagBits::eColorAttachment |
                                  /* input attachment */ vk::ImageUsageFlagBits::eInputAttachment,
                              vk::MemoryPropertyFlagBits::eDeviceLocal);

    if (sampleBits != vk::SampleCountFlagBits::e1) {
      // 0
      sceneAttachments.msaaColor = ctx().create2DImage(
          vkCanvasExtent, colorFormat,
          vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
          vk::MemoryPropertyFlagBits::eDeviceLocal,
          /*mipmap*/ false, true, /*transfer*/ false, sampleBits);
      // 1
      sceneAttachments.msaaDepth
          = ctx().create2DImage(vkCanvasExtent, depthFormat,
                                vk::ImageUsageFlagBits::eTransientAttachment
                                    | vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                vk::MemoryPropertyFlagBits::eDeviceLocal, /*mipmap*/ false, true,
                                /*transfer, for extra depth resolve*/ false, sampleBits);
      // 4
      sceneAttachments.msaaPick = ctx().create2DImage(
          vkCanvasExtent, pickFormat,
          vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
          vk::MemoryPropertyFlagBits::eDeviceLocal,
          /*mipmap*/ false, true,
          /*transfer, for extra depth resolve*/ false, sampleBits);
      sceneAttachments.fbo = ctx().createFramebuffer(
          {(vk::ImageView)sceneAttachments.msaaColor.get(),
           (vk::ImageView)sceneAttachments.msaaDepth.get(),
           (vk::ImageView)sceneAttachments.color.get(), (vk::ImageView)sceneAttachments.depth.get(),
           (vk::ImageView)sceneAttachments.msaaPick.get(),
           (vk::ImageView)scenePickPass.pickBuffer.get()},
          vkCanvasExtent, sceneRenderer.renderPass.get());
    } else {
      sceneAttachments.fbo = ctx().createFramebuffer(
          {(vk::ImageView)sceneAttachments.color.get(), (vk::ImageView)sceneAttachments.depth.get(),
           (vk::ImageView)scenePickPass.pickBuffer.get()},
          vkCanvasExtent, sceneRenderer.renderPass.get());
    }
  }

  void SceneEditor::frameStat() {
    static auto _t = std::chrono::high_resolution_clock::now();
    static int _count = 0;
    ++_count;
    auto now = std::chrono::high_resolution_clock::now();
    auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - _t).count();
    if (delta >= 1000) {
      framePerSecond = 1000.0f / (1.0f * delta / _count);
      _count = 0;
      _t = now;
    }
  }

  void SceneEditor::renderFrame(int bufferNo, vk::CommandBuffer cmd) {
    auto &ctx = this->ctx();

#if ENABLE_PROFILE
    CppTimer timer;
    timer.tick();
#endif
    frameStat();
#if ENABLE_PROFILE
    timer.tock("SceneEditor::frameStat");
#endif

    resetDrawStates();

    /// @note profiling is now mandatory
    // ctx.device.resetQueryPool(queryPool.get(), 0, vkq_total, ctx.dispatcher);
    scenePassTime = pickPassTime = gridPassTime = textPassTime = textRenderPassTime = 0;

    /// update camera ubo
    SceneCameraParams params;
    params.projection = sceneRenderData.camera.get().matrices.perspective;
    params.view = sceneRenderData.camera.get().matrices.view;
    std::memcpy(sceneRenderData.sceneCameraUbo.get().mappedAddress(), &params, sizeof(params));

#if ENABLE_PROFILE
    timer.tick();
#endif
    prepareRender();
#if ENABLE_PROFILE
    timer.tock("SceneEditor:: prepare render");
#endif

#if ENABLE_PROFILE
    timer.tick();
#endif
    updateClusterLighting();
#if ENABLE_PROFILE
    timer.tock("SceneEditor:: update cluster lighting");
#endif

#if ENABLE_PROFILE
    timer.tick();
#endif
    renderSceneBuffers();
#if ENABLE_PROFILE
    timer.tock("SceneEditor:: render scene buffer");
#endif

#if ENABLE_PROFILE
    timer.tick();
#endif
    renderTransparent();
#if ENABLE_PROFILE
    timer.tock("SceneEditor:: order-independent transparency");
#endif

#if ENABLE_OCCLUSION_QUERY
#  if ENABLE_PROFILE
    timer.tick();
#  endif
    runOcclusionQuery();
    getOcclusionQueryResults();
#  if ENABLE_PROFILE
    timer.tock("SceneEditor:: occlusion query");
#  endif
#endif  // ENABLE_OCCLUSION_QUERY

    if (interactionMode.isEditMode()) {
#if ENABLE_PROFILE
      timer.tick();
#endif
      renderFramePickBuffers();
#if ENABLE_PROFILE
      timer.tock("SceneEditor:: render pick buffer");
#endif

#if ENABLE_PROFILE
      timer.tick();
#endif
      renderSceneAugmentView();
#if ENABLE_PROFILE
      timer.tock("SceneEditor:: render augment view");
#endif

#if ENABLE_PROFILE
      timer.tick();
#endif
      renderOutline();
#if ENABLE_PROFILE
      timer.tock("SceneEditor:: render outline");
#endif
    }

  }  // render scene

  void SceneEditor::prepareRender() {
    auto &ctx = this->ctx();

    int renderingModelsInFrame = 0;

    /// initiate
#if ZS_ENABLE_OPENMP
    auto pol = omp_exec();
    auto execTag = exec_omp;
#else
    auto pol = seq_exec();
    auto execTag = exec_seq;
#endif

#if ENABLE_FRUSTUM_CULLING
    std::vector<int> currentVisiblePrimsCulledByFrustum(getCurrentVisiblePrims().size());
#endif
    hoveredHitPt = glm::vec3(detail::deduce_numeric_infinity<f32>());
    pol(enumerate(getCurrentVisiblePrims()), [&](PrimIndex i, const auto &primPtr) {
      auto &prim = primPtr;
      currentVisiblePrimsDrawnTags[i] = 0;
      if (!prim || prim->empty()) return;
      auto pModel = prim->queryVkTriMesh(ctx, sceneRenderData.currentTimeCode);
      if (!pModel || !pModel->isValid()) return;

      // update visual transform before any visualTransform() at the beginning of
      // render frame
      prim->updateTransform(sceneRenderData.currentTimeCode);

      // auto transform = prim->visualTransform(sceneRenderData.currentTimeCode);
      const auto &transform = prim->currentTimeVisualTransform();

      prim->details().updateWorldBoundingBox(transform);

#if ENABLE_FRUSTUM_CULLING
      // Frustum culling
      const auto &aabb = prim->details().worldBoundingBox();
      // transform local space center to world space center
      const bool isCulled = !sceneRenderData.camera.get().isAABBVisible(aabb.minPos, aabb.maxPos);
      currentVisiblePrimsCulledByFrustum[i] = isCulled;
      if (isCulled) {
        return;
      }
#endif

#if ENABLE_OCCLUSION_QUERY
      auto it = sceneOcclusionQuery.primToQueryIndex.find(prim);
      if (it != sceneOcclusionQuery.primToQueryIndex.end()
          && sceneOcclusionQuery.occlusionResults[it->second] == 0) {
        return;
      }
#endif  // ENABLE_OCCLUSION_QUERY

      /// @note mark prim drawn
      // make sure this write op is thread-safe
      currentVisiblePrimsDrawnTags[i] = 1;
      // currentVisiblePrimsDrawn[prim] = 1;
      // ++renderingModelsInFrame;
      zs::atomic_add(execTag, &renderingModelsInFrame, 1);
    });
    /// @note currentVisiblePrimsDrawnTags->currentVisiblePrimsDrawn
    for (const auto &[i, prim, tag] :
         enumerate(getCurrentVisiblePrims(), currentVisiblePrimsDrawnTags)) {
      currentVisiblePrimsDrawn[prim] = tag;

#if ENABLE_FRUSTUM_CULLING
      isCulledByFrustum[prim] = currentVisiblePrimsCulledByFrustum[i];
#endif
    }

    numFrameRenderModels = renderingModelsInFrame;
  }

  void SceneEditor::renderSceneBuffers() {
    auto &ctx = this->ctx();

    // struct {
    //   glm::mat4 projection;
    //   glm::mat4 view;
    // } params;
    // params.projection = sceneRenderData.camera.get().matrices.perspective;
    // params.view = sceneRenderData.camera.get().matrices.view;
    // std::memcpy(sceneRenderData.sceneCameraUbo.get().mappedAddress(), &params,
    // sizeof(params));

#if 0
    if (ctx.device.waitForFences({(vk::Fence)fence.get()}, VK_TRUE, std::numeric_limits<u64>::max(),
                                 ctx.dispatcher)
        != vk::Result::eSuccess)
      throw std::runtime_error("error waiting for fences");
#endif

    auto &cmd = this->cmd.get();
    cmd.begin();

    vk::Rect2D rect = vk::Rect2D(vk::Offset2D(), vkCanvasExtent);
    std::array<vk::ClearValue, 6> clearValues{};
    clearValues[0].color = vk::ClearColorValue{std::array<float, 4>{0.51f, 0.51f, 0.51f, 0.0f}};
    clearValues[1].depthStencil
        = vk::ClearDepthStencilValue{SceneEditor::reversedZ ? 0.0f : 1.0f, 0};
    clearValues[3].color = vk::ClearColorValue{std::array<int, 4>{-1, -1, -1, -1}};
    clearValues[4].color = vk::ClearColorValue{std::array<int, 4>{-1, -1, -1, -1}};

    ///
    /// first subpass
    ///
    auto renderPassInfo = vk::RenderPassBeginInfo()
                              .setRenderPass(sceneRenderer.renderPass.get())
                              .setFramebuffer(sceneAttachments.fbo.get())
                              .setRenderArea(rect)
                              .setClearValueCount((zs::u32)clearValues.size())
                              .setPClearValues(clearValues.data());

    auto viewport
        = vk::Viewport()
              .setX(0 /*offsetx*/)
              .setY(vkCanvasExtent.height /*-offsety*/)
              .setWidth(float(vkCanvasExtent.width))
              .setHeight(-float(vkCanvasExtent.height))  // negative viewport, opengl conformant
              .setMinDepth(0.0f)
              .setMaxDepth(1.0f);

#if 0
    (*cmd).beginRenderPass(renderPassInfo, vk::SubpassContents::eSecondaryCommandBuffers);

#  if ENABLE_PROFILE
    CppTimer timer;
    timer.tick();
#  endif
    /// @brief parallel rendering command recording
    resetVkCmdCounter();
    auto visiblePrimChunks = chunk_view{getCurrentVisiblePrims(),
                                        evalRenderChunkSize(getCurrentVisiblePrims().size())};
    int j = 0;
    auto inheritance = vk::CommandBufferInheritanceInfo{sceneRenderer.renderPass.get(),
                                                        /*subpass*/ 0, sceneAttachments.fbo.get()};

    float cullingTime = 0.f;

    for (auto it = visiblePrimChunks.begin(); it < visiblePrimChunks.end(); ++it, ++j) {
      renderScheduler->enqueue(
          [&, it]() {
            auto &renderCmd = nextVkCommand();
            (*renderCmd)
                .begin(
                    vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eRenderPassContinue,
                                               &inheritance},
                    ctx.dispatcher);

            (*renderCmd).setViewport(0, {viewport});
            (*renderCmd).setScissor(0, {vk::Rect2D(vk::Offset2D(), vkCanvasExtent)});

            for (const auto &primPtr : *it) {
              // auto prim = primPtr.lock();
              auto &prim = primPtr;
#  if 0
              if (!prim || prim->empty()) continue;
              auto pModel = prim->queryVkTriMesh(ctx, sceneRenderData.currentTimeCode);
              if (!pModel || !pModel->isValid()) continue;
              // if (!zs_resources().vis_prims_ready()) continue;
              // if (!_visBufferReady) continue;

              // update visual transform before any visualTransform() at the beginning of render
              // frame
              prim->updateTransform(sceneRenderData.currentTimeCode);

              // auto transform = prim->visualTransform(sceneRenderData.currentTimeCode);
              const auto &transform = prim->currentTimeVisualTransform();

              // prim->details().updateWorldBoundingBox(transform);

#    if ENABLE_FRUSTUM_CULLING
              // Frustum culling
              const auto &aabb = prim->details().worldBoundingBox();
          // transform local space center to world space center
#      if ENABLE_PROFILE
              CppTimer loctimer;
              loctimer.tick();
#      endif
              const bool isCulled = isCulledByFrustum[prim]
                  = !sceneRenderData.camera.get().isAABBVisible(aabb.minPos, aabb.maxPos);
              if (isCulled) {
                continue;
              }
#      if ENABLE_PROFILE
              loctimer.tock();
              atomic_add(exec_omp, &cullingTime, loctimer.elapsed());
#      endif
#    endif

#    if ENABLE_OCCLUSION_QUERY
              auto it = sceneOcclusionQuery.primToQueryIndex.find(prim);
              if (it != sceneOcclusionQuery.primToQueryIndex.end()
                  && sceneOcclusionQuery.occlusionResults[it->second] == 0) {
                continue;
              }
#    endif  // ENABLE_OCCLUSION_QUERY

              /// @note mark prim drawn
              // make sure this write op is thread-safe
              currentVisiblePrimsDrawn[prim] = 1;
#  else
              if (!currentVisiblePrimsDrawn[prim]) continue;
              const auto &transform = prim->currentTimeVisualTransform();
#  endif

              auto pModel = prim->queryVkTriMesh(ctx, sceneRenderData.currentTimeCode);
              const auto &model = *pModel;
              // const auto &model = prim->vkTriMesh(ctx);
              if (model.isParticle()) {
                (*renderCmd)
                    .bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                        /*pipeline layout*/ sceneRenderer.pointPipeline.get(),
                                        /*firstSet*/ 0,
                                        /*descriptor sets*/ {sceneRenderData.sceneCameraSet},
                                        /*dynamic offset*/ {0}, ctx.dispatcher);
                // use ubo instead of push constant for camera

                (*renderCmd)
                    .bindPipeline(vk::PipelineBindPoint::eGraphics,
                                  sceneRenderer.pointPipeline.get());

                sceneRenderer.pointVertParams.model = transform;
                (*renderCmd)
                    .pushConstants(
                        sceneRenderer.pointPipeline.get(), vk::ShaderStageFlagBits::eVertex, 0,
                        sizeof(sceneRenderer.pointVertParams), &sceneRenderer.pointVertParams);
                model.bind((*renderCmd), VkModel::point);
                model.draw((*renderCmd), VkModel::point);
              } else {
                const auto &texSet = ResourceSystem::get_texture_descriptor_set(model.texturePath);
                // use ubo instead of push constant for camera
                (*renderCmd)
                    .bindDescriptorSets(
                        vk::PipelineBindPoint::eGraphics,
                        /*pipeline layout*/ sceneRenderer.opaquePipeline.get(),
                        /*firstSet*/ 0,
                        /*descriptor sets*/ {sceneRenderData.sceneCameraSet, texSet},
                        /*dynamic offset*/ {0}, ctx.dispatcher);

                (*renderCmd)
                    .bindPipeline(vk::PipelineBindPoint::eGraphics, sceneRenderer.opaquePipeline.get());

                (*renderCmd)
                    .pushConstants(sceneRenderer.opaquePipeline.get(), vk::ShaderStageFlagBits::eVertex,
                                   0, sizeof(transform), &transform);
                glm::ivec2 id;
                id.x = prim->id();
                // id.y = drawPipeline == draw_Texture || !model.texturePath.empty() ? 1 : 0;
                id.y = drawTexture || !model.texturePath.empty() ? 1 : 0;
                (*renderCmd)
                    .pushConstants(sceneRenderer.opaquePipeline.get(), vk::ShaderStageFlagBits::eFragment,
                                   sizeof(transform), sizeof(id), &id);
                model.bind((*renderCmd), VkModel::tri);
                model.draw((*renderCmd), VkModel::tri);
              }
            }
            (*renderCmd).end();
          },
          j);
    }
    renderScheduler->wait();

#  if ENABLE_PROFILE
    timer.tock("\t -> draw scene buffer");
    fmt::print("culling time accum {}.\n", cullingTime);
#  endif

    // rendering grid
    if (showCoordinate) {
      renderScheduler->enqueue(
          [&]() {
            auto &renderCmd = nextVkCommand();
            (*renderCmd)
                .begin(
                    vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eRenderPassContinue,
                                               &inheritance},
                    ctx.dispatcher);

            (*renderCmd).setViewport(0, {viewport});
            (*renderCmd).setScissor(0, {vk::Rect2D(vk::Offset2D(), vkCanvasExtent)});

            (*renderCmd)
                .bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                    /*pipeline layout*/ sceneRenderer.gridPipeline.get(),
                                    /*firstSet*/ 0,
                                    /*descriptor sets*/ {sceneRenderData.sceneCameraSet},
                                    /*dynamic offset*/ {0}, ctx.dispatcher);
            constexpr float farPlaneDepth = SceneEditor::reversedZ ? 0.0f : 1.0f;
            (*renderCmd)
                .pushConstants(sceneRenderer.gridPipeline.get(), vk::ShaderStageFlagBits::eVertex,
                               0, sizeof(float), &farPlaneDepth);
            (*renderCmd)
                .bindPipeline(vk::PipelineBindPoint::eGraphics, sceneRenderer.gridPipeline.get());

            (*renderCmd).draw(4, 1, 0, 0, ctx.dispatcher);
            // sceneRenderData.grid.bind((*cmd), VkModel::tri);
            // sceneRenderData.grid.draw((*cmd), VkModel::tri);
            (*renderCmd).end();
          },
          0);
    }

    renderScheduler->wait();

    (*cmd).executeCommands(getCurrentSecondaryVkCmds(), ctx.dispatcher);
#else

#  if ENABLE_PROFILE
    CppTimer timer;
    timer.tick();
#  endif
    (*cmd).beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    (*cmd).setViewport(0, {viewport});
    (*cmd).setScissor(0, {vk::Rect2D(vk::Offset2D(), vkCanvasExtent)});

    /*
     * Opaque Pass
     */
    for (const auto &primPtr : getCurrentVisiblePrims()) {
      // auto prim = primPtr.lock();
      auto &prim = primPtr;
#  if 0
      if (!prim || prim->empty()) continue;
      auto pModel = prim->queryVkTriMesh(ctx, sceneRenderData.currentTimeCode);
      if (!pModel || !pModel->isValid()) continue;
      const auto &model = *pModel;

      // prim->updateTransform(sceneRenderData.currentTimeCode);

      // auto transform = prim->visualTransform(sceneRenderData.currentTimeCode);
      const auto &transform = prim->currentTimeVisualTransform();

      // prim->details().updateWorldBoundingBox(transform);

#    if ENABLE_FRUSTUM_CULLING
      // Frustum culling
      const auto &aabb = prim->details().worldBoundingBox();
      // transform local space center to world space center
#      if ENABLE_PROFILE
      CppTimer loctimer;
      loctimer.tick();
#      endif
      const bool isCulled = isCulledByFrustum[prim]
          = !sceneRenderData.camera.get().isAABBVisible(aabb.minPos, aabb.maxPos);
      if (isCulled) {
        continue;
      }
#      if ENABLE_PROFILE
      loctimer.tock();
#      endif
#    endif

#    if ENABLE_OCCLUSION_QUERY
      auto it = sceneOcclusionQuery.primToQueryIndex.find(prim);
      if (it != sceneOcclusionQuery.primToQueryIndex.end()
          && sceneOcclusionQuery.occlusionResults[it->second] == 0) {
        continue;
      }
#    endif  // ENABLE_OCCLUSION_QUERY

      /// @note mark prim drawn
      // make sure this write op is thread-safe
      currentVisiblePrimsDrawn[prim] = 1;
#  else
      if (!prim->details().refIsOpaque()) continue;
      if (!currentVisiblePrimsDrawn[prim]) continue;
      const auto &transform = prim->currentTimeVisualTransform();
#  endif
      auto pModel = prim->queryVkTriMesh(ctx, sceneRenderData.currentTimeCode);
      const auto &model = *pModel;

      // const auto &model = prim->vkTriMesh(ctx);
      if (model.isParticle()) {
        (*cmd).bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                  /*pipeline layout*/ sceneRenderer.pointOpaquePipeline.get(),
                                  /*firstSet*/ 0,
                                  /*descriptor sets*/ {sceneRenderData.sceneCameraSet},
                                  /*dynamic offset*/ {0}, ctx.dispatcher);
        // use ubo instead of push constant for camera

        (*cmd).bindPipeline(vk::PipelineBindPoint::eGraphics,
                            sceneRenderer.pointOpaquePipeline.get());

        sceneRenderer.pointVertParams.model = transform;
        (*cmd).pushConstants(sceneRenderer.pointOpaquePipeline.get(),
                             vk::ShaderStageFlagBits::eVertex, 0,
                             sizeof(sceneRenderer.pointVertParams), &sceneRenderer.pointVertParams);
        model.bind((*cmd), VkModel::point);
        model.draw((*cmd), VkModel::point);
      } else {
        if (!drawTexture) {
          const auto &texSet = ResourceSystem::get_texture_descriptor_set(model.texturePath);
// use ubo instead of push constant for camera
#  if 0
          (*cmd).bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                    /*pipeline layout*/ sceneRenderer.opaquePipeline.get(),
                                    /*firstSet*/ 0,
                                    /*descriptor sets*/ {sceneRenderData.sceneCameraSet, texSet},
                                    /*dynamic offset*/ {0}, ctx.dispatcher);
#  else
          (*cmd).bindDescriptorSets(
              vk::PipelineBindPoint::eGraphics,
              /*pipeline layout*/ sceneRenderer.opaquePipeline.get(),
              /*firstSet*/ 0,
              /*descriptor sets*/ {sceneRenderData.sceneCameraSet, sceneLighting.lightTableSet},
              /*dynamic offset*/ {0}, ctx.dispatcher);
#  endif

          (*cmd).bindPipeline(vk::PipelineBindPoint::eGraphics, sceneRenderer.opaquePipeline.get());

          (*cmd).pushConstants(sceneRenderer.opaquePipeline.get(), vk::ShaderStageFlagBits::eVertex,
                               0, sizeof(transform), &transform);
#  if 0
          glm::ivec2 id;
          id.x = prim->id();
          // id.y = drawPipeline == draw_Texture || !model.texturePath.empty() ? 1 : 0;
          // id.y = drawTexture || !model.texturePath.empty() ? 1 : 0;
          id.y = 0;
#  endif
          auto id = prim->id();
          (*cmd).pushConstants(sceneRenderer.opaquePipeline.get(),
                               vk::ShaderStageFlagBits::eFragment, sizeof(transform), sizeof(id),
                               &id);
          const glm::ivec2 clusterCountVec
              = {sceneLighting.clusterCountPerLine, sceneLighting.clusterCountPerDepth};
          (*cmd).pushConstants(
              sceneRenderer.opaquePipeline.get(), vk::ShaderStageFlagBits::eFragment,
              sizeof(transform) + 2 * sizeof(id), sizeof(glm::ivec2), &clusterCountVec);
          model.bindNormalColor((*cmd), VkModel::tri);
          model.drawNormalColor((*cmd), VkModel::tri);
        } else {
          const auto &bindlessSet = ctx.bindlessSet();
          (*cmd).bindDescriptorSets(
              vk::PipelineBindPoint::eGraphics,
              /*pipeline layout*/ sceneRenderer.bindlessPipeline.get(),
              /*firstSet*/ 0,
              /*descriptor sets*/ {sceneRenderData.sceneCameraSet, bindlessSet},
              /*dynamic offset*/ {0}, ctx.dispatcher);
          (*cmd).bindPipeline(vk::PipelineBindPoint::eGraphics,
                              sceneRenderer.bindlessPipeline.get());

          (*cmd).pushConstants(sceneRenderer.bindlessPipeline.get(),
                               vk::ShaderStageFlagBits::eVertex, 0, sizeof(transform), &transform);
          int id = prim->id();
          (*cmd).pushConstants(sceneRenderer.bindlessPipeline.get(),
                               vk::ShaderStageFlagBits::eFragment, sizeof(transform), sizeof(id),
                               &id);
          model.bindUV((*cmd), VkModel::tri);
          model.drawUV((*cmd), VkModel::tri);
        }
      }
    }

#  if ENABLE_PROFILE
    timer.tock("\t -> draw scene buffer");
#  endif
    if (showCoordinate) {
      constexpr float farPlaneDepth = SceneEditor::reversedZ ? 0.0f : 1.0f;
      (*cmd).bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                /*pipeline layout*/ sceneRenderer.gridPipeline.get(),
                                /*firstSet*/ 0,
                                /*descriptor sets*/ {sceneRenderData.sceneCameraSet},
                                /*dynamic offset*/ {0}, ctx.dispatcher);

      (*cmd).pushConstants(sceneRenderer.gridPipeline.get(), vk::ShaderStageFlagBits::eVertex, 0,
                           sizeof(float), &farPlaneDepth);
      (*cmd).bindPipeline(vk::PipelineBindPoint::eGraphics, sceneRenderer.gridPipeline.get());

      (*cmd).draw(4, 1, 0, 0, ctx.dispatcher);
      // sceneRenderData.grid.bind((*cmd), VkModel::tri);
      // sceneRenderData.grid.draw((*cmd), VkModel::tri);
    }

#endif

#if 0
    ///
    /// second subpass
    ///
    (*cmd).nextSubpass(vk::SubpassContents::eInline);

    glm::vec2 visRange{0.0, 1.0};
    (*cmd).pushConstants(sceneRenderer.postFxPipeline.get(), vk::ShaderStageFlagBits::eFragment, 0,
                         sizeof(visRange), &visRange);
    (*cmd).bindPipeline(vk::PipelineBindPoint::eGraphics, sceneRenderer.postFxPipeline.get());
    (*cmd).bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                              /*pipeline layout*/ sceneRenderer.postFxPipeline.get(),
                              /*firstSet*/ 0,
                              /*descriptor sets*/ {sceneRenderData.postFxInputAttachmentSet},
                              /*dynamic offset*/ {}, ctx.dispatcher);
    (*cmd).draw(3, 1, 0, 0);
#endif

    (*cmd).endRenderPass();

    (*cmd).end();

    cmd.submit(fence.get(), /*reset fence*/ true, /*reset config*/ true);

    if (ctx.device.waitForFences({(vk::Fence)fence.get()}, VK_TRUE, std::numeric_limits<u64>::max(),
                                 ctx.dispatcher)
        != vk::Result::eSuccess)
      throw std::runtime_error("error waiting for fences");

    updateSceneSet();
  }

  SceneEditor::DisplayingVisBuffers::DisplayingVisBuffers(SceneEditor *se) noexcept
      : SceneEditor::VisBufferStateBase{se} {
    if (se) zs::atomic_exch(exec_omp, &se->_visBufferReady, 1u);
    // puts("\tin DisplayingVisBuffers STATE!");
  }
  SceneEditor::PreProcessingVisBuffers::PreProcessingVisBuffers(SceneEditor *se, int n) noexcept
      : SceneEditor::VisBufferStateBase{se}, _numPendingPreprocessTasks{n} {
    zs::atomic_exch(exec_omp, &se->_visBufferReady, 0u);
    // puts("\tin PreProcessingVisBuffers STATE!");
  }
  SceneEditor::PostProcessingVisBuffers::PostProcessingVisBuffers(SceneEditor *se) noexcept
      : SceneEditor::VisBufferStateBase{se} {
    zs::atomic_exch(exec_omp, &se->_visBufferReady, 0u);
    // puts("\tis PostProcessingVisBuffers STATE!");
    se->issueVisBufferUpdateEvents();  // batched visprim copy-update vulkan cmds
                                       // submission

#if 0
/// @brief rebuild prim level bvh (transform dirty may trigger bvh maintenance)
#  if ZS_ENABLE_OPENMP
    auto pol = omp_exec();
#  else
    auto pol = seq_exec();
#  endif
    Vector<AABBBox<3, f32>> bvs{se->currentVisiblePrims.size()};
    pol(enumerate(se->currentVisiblePrims), [&pol, &bvs](int primI, auto pPrim) {
      auto bv = pPrim->details().localBoundingBox();
      auto mi = zs::vec<f32, 3>{bv.minPos[0], bv.minPos[1], bv.minPos[2]};
      auto ma = zs::vec<f32, 3>{bv.maxPos[0], bv.maxPos[1], bv.maxPos[2]};
      bvs[primI] = AABBBox<3, f32>{mi, ma};
    });
    auto &bvh = se->currentVisiblePrimsBvh;
    bvh.buildRefit(pol, bvs);
#endif
  }

  SceneEditor::OptionalState SceneEditor::DisplayingVisBuffers::process(
      const VisBufferTaskEvent &event) {
    if (event._delta > 0)
      return VisBufferStates{SceneEditor::PreProcessingVisBuffers{sceneEditor, event._delta}};
    return {};
  }
  SceneEditor::OptionalState SceneEditor::PreProcessingVisBuffers::process(
      const VisBufferTaskEvent &event) {
    if (event._delta < 0)
      return VisBufferStates{SceneEditor::PostProcessingVisBuffers{sceneEditor}};
    return {};
  }
  SceneEditor::OptionalState SceneEditor::PostProcessingVisBuffers::process(
      const VisBufferReadyEvent &event) {
    return VisBufferStates{SceneEditor::DisplayingVisBuffers{sceneEditor}};
  }

  void SceneEditor::issueVisBufferUpdateEvents() {
    ZS_EVENT_SCHEDULER().emplace([this]() {
      auto &ctx = this->ctx();
      auto &env = ctx.env();
      auto preferredQueueType = ctx.isQueueValid(zs::vk_queue_e::dedicated_transfer)
                                    ? zs::vk_queue_e::dedicated_transfer
                                    : zs::vk_queue_e::transfer;
      auto &pool = env.pools(preferredQueueType);
      auto cmd = ctx.createCommandBuffer(vk_cmd_usage_e::single_use, preferredQueueType, false);
      auto copyQueue = ctx.getLastQueue(preferredQueueType);
      // vk::CommandBuffer cmd =
      // pool.createCommandBuffer(vk::CommandBufferLevel::ePrimary, false,
      //     nullptr, zs::vk_cmd_usage_e::single_use);
      cmd.begin(vk::CommandBufferBeginInfo{});

      auto &taskQueue = zs_execution().refVkCmdTaskQueue();
      constexpr int N = 512;
      std::array<zs::function<void(vk::CommandBuffer)>, N> tasks;
      while (auto n = taskQueue.try_dequeue_bulk(tasks.begin(), N)) {
        for (int i = 0; i < n; ++i) {
          tasks[i](cmd);
          // zs_resources().dec_inflight_prim_cnt();
        }
      }

      cmd.end();
      auto &fence = *pool.fence;
      ctx.device.resetFences({fence}, ctx.dispatcher);
      vk::CommandBuffer cmd_ = *cmd;
      auto submitInfo = vk::SubmitInfo().setCommandBufferCount(1).setPCommandBuffers(&cmd_);
      auto res = copyQueue.submit(1, &submitInfo, fence, ctx.dispatcher);
      fence.wait();

      // issue a finish event to the state machine
      _visBufferStatus->process(VisBufferReadyEvent{});
    });
  }

  void SceneEditor::update(float dt) {
    /// handle input events
    // if (viewportFocused) {
    //   interactionMode.update(dt);
    // }

    _camCtrl.update(dt);

    /// handle resize
    {
      vk::Extent2D currentDim{(u32)imguiCanvasSize.x, (u32)imguiCanvasSize.y};
      if (currentDim.width != 0 && currentDim.height != 0
          && (currentDim.width != vkCanvasExtent.width
              || currentDim.height != vkCanvasExtent.height)) {
        ctx().sync();

        fmt::print("viewport resizing from <{}, {}> to <{}, {}>\n", vkCanvasExtent.width,
                   vkCanvasExtent.height, currentDim.width, currentDim.height);

        vkCanvasExtent = currentDim;

        rebuildAttachments();

        sceneRenderData.camera.get().setPerspective(
            sceneRenderData.camera.get().getFov(),
            (float)vkCanvasExtent.width / (float)vkCanvasExtent.height,
            sceneRenderData.camera.get().getNearClip(), sceneRenderData.camera.get().getFarClip());
      }
    }
  }

  VkCommand &SceneEditor::nextVkCommand() {
    auto j = renderScheduler->getWorkerIdMapping().at(std::this_thread::get_id());
    auto k = currentlyUsedSecondaryCmdNum[j]++;
    if (k + 1 > currentlyAllocatedSecondaryCmdNum[j]) currentlyAllocatedSecondaryCmdNum[j] = k + 1;
    return this->ctx().env().pools(vk_queue_e::graphics).acquireSecondaryVkCommand(k);
  }

  std::vector<vk::CommandBuffer> SceneEditor::getCurrentSecondaryVkCmds() {
    std::vector<int> offsets(currentlyUsedSecondaryCmdNum.size());
    exclusive_scan(seq_exec(), zs::begin(currentlyUsedSecondaryCmdNum),
                   zs::end(currentlyUsedSecondaryCmdNum), zs::begin(offsets));
    currentRenderVkCmds.resize(currentlyUsedSecondaryCmdNum.back() + offsets.back());
    // assert(renderScheduler->numWorkers() ==
    // currentlyUsedSecondaryCmdNum.size());
    /// @note make sure every worker participates this cmd gathering
    for (int i = 0; i < renderScheduler->numWorkers(); ++i) {
      renderScheduler->enqueue(
          [&offsets, this]() {
            auto j = renderScheduler->getWorkerIdMapping().at(std::this_thread::get_id());
            auto num = currentlyUsedSecondaryCmdNum[j];
            for (int k = 0; k < num; ++k)
              currentRenderVkCmds[offsets[j] + k]
                  = this->ctx().env().pools(vk_queue_e::graphics).acquireSecondaryVkCommand(k);
          },
          i);
    }
    renderScheduler->wait();
    return currentRenderVkCmds;
  }

  glm::vec3 SceneEditor::getScreenPointCameraRayDirection() const {
    return sceneRenderData.camera.get().getCameraRayDirection(
        canvasLocalMousePos[0], canvasLocalMousePos[1], (float)vkCanvasExtent.width,
        (float)vkCanvasExtent.height);
  }

  glm::mat4 get_transform(const vec<float, 3> &translation, const vec<float, 3> &eulerXYZ,
                          const vec<float, 3> &scale) {
    return glm::translate(glm::mat4(1.f), glm::vec3(translation[0], translation[1], translation[2]))
           * glm::eulerAngleXYZ<float>(eulerXYZ[0], eulerXYZ[1], eulerXYZ[2])
           * glm::scale(glm::mat4(1.f), glm::vec3(scale[0], scale[1], scale[2]));
  }

  glm::mat4 get_transform(const vec<float, 4, 4> &transform) {
    glm::mat4 ret;
    for (int i = 0; i < 4; ++i) {
      ret[i][0] = transform[i][0];
      ret[i][1] = transform[i][1];
      ret[i][2] = transform[i][2];
      ret[i][3] = transform[i][3];
    }
    return ret;
  }

}  // namespace zs