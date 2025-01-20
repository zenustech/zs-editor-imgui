#include "SceneEditor.hpp"
#include "world/scene/Primitive.hpp"

namespace zs {
  static const char g_cluster_light[] = R"(
#version 450

const int CLUSTER_PIXEL_SIZE = 32;
const int CLUSTER_Z_SIZE = 32;
const int CLUSTER_LIGHT_CAPACITY = 63;

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

const float Deg2Rad = 3.1415926535 / 180.0;

struct LightInfo{
  vec4 color; // rgb: color, a: intensity
  /*
  * DISTANT xyz: world space direction w: angle in degree
  * POINT xyz: world space position, w: radius
  */
  vec4 lightVec;
  ivec4 lightSourceType;
};

/* input */
layout(push_constant) uniform CameraInfo {
  layout (offset = 0) mat4 view;
  layout (offset = 64) vec4 cameraInfo; // fov, aspect, near, far
  layout (offset = 80) ivec3 screenAndLightInfo; // screen width, screen height, light count
} CameraUbo;
layout(std430, binding = 0) readonly buffer LightList {
  LightInfo lightList[];
};
/* output */
layout(std430, binding = 1) buffer ClusterLightList {
  // for each k in [0, clusterCount):
  // lightIndices[k*32]: size of light index, range [0, 31]
  // lightIndices[k*32+1 to k*32+31]: light indices
  int lightIndices[];
};

void getPlanes(vec2 nearPlaneUV, vec2 nearPlaneUVNext, in mat4 invView, out vec4 planes[5]){
  float halfNearHeight = CameraUbo.cameraInfo.z * tan(CameraUbo.cameraInfo.x * Deg2Rad * 0.5); // near-z * tan(0.5 * fov)
  float halfNearWidth = halfNearHeight * CameraUbo.cameraInfo.y; // half near-height * aspect
  // min/max view space vector of current cluster
  nearPlaneUV = nearPlaneUV * 2.0 - 1.0; // map to [-1, 1]
  nearPlaneUVNext = nearPlaneUVNext * 2.0 - 1.0; // map to [-1, 1]
  vec3 nearLeftDown = normalize(vec3(vec2(halfNearWidth, halfNearHeight) * nearPlaneUV, -CameraUbo.cameraInfo.z));
  vec3 nearRightUp = normalize(vec3(vec2(halfNearWidth, halfNearHeight) * nearPlaneUVNext, -CameraUbo.cameraInfo.z));
  const vec3 nearUp = vec3(0.0, 1.0, 0.0);
  const vec3 nearRight = vec3(1.0, 0.0, 0.0);
  // calculate plane normals base on current cluster
  planes[0].xyz = normalize(cross(nearUp, nearLeftDown)); // left
  planes[1].xyz = normalize(cross(nearRightUp, nearUp)); // right
  planes[2].xyz = normalize(cross(nearRight, nearRightUp)); // top
  planes[3].xyz = normalize(cross(nearLeftDown, nearRight)); // down

  // convert into world space
  for (int i=0; i<4; ++i){
    planes[i].w = 0.0;
    planes[i].xyz = normalize((invView * planes[i]).xyz); // convert normal from view space to world space
    planes[i].w = dot(planes[i].xyz, invView[3].xyz /* camera world pos */ ); // plane offset
  }
}

float intersect(in vec4 planes[5], in vec2 depthRange, in LightInfo lightInfo){
  float result = 1.0;

  // check interection with four planes for current cluster
  for (int i=0; i<4; ++i){
    result *= step(dot(planes[i].xyz, lightInfo.lightVec.xyz) - planes[i].w, lightInfo.lightVec.w);
  }

  // deal with forward direction
  float depth = dot(planes[4].xyz, lightInfo.lightVec.xyz) - planes[4].w; // positive view space depth
  result *= step(depthRange.x, depth + lightInfo.lightVec.w) * step(depth - lightInfo.lightVec.w, depthRange.y); // sphere depth should be in depth range

  return result;
}

void main() {
  // useful variables
  const ivec2 cluster_size = (CameraUbo.screenAndLightInfo.xy + CLUSTER_PIXEL_SIZE - 1) / CLUSTER_PIXEL_SIZE;
  const int cluster_screen_count = cluster_size.x * cluster_size.y;
  const int cluster_count = CLUSTER_Z_SIZE * cluster_screen_count;
  const float depthPerCluster = CameraUbo.cameraInfo.w / float(CLUSTER_Z_SIZE);

  const int TOTAL_THREAD_SIZE = int(gl_NumWorkGroups.x * gl_WorkGroupSize.x * gl_NumWorkGroups.y * gl_WorkGroupSize.y * gl_NumWorkGroups.z * gl_WorkGroupSize.z);
  int tid = int(gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x);

  vec4 planes[5]; // left, right, top, down and forward
  const mat4 invView = transpose(CameraUbo.view);

  // calculate forward direction once
  planes[4] = -invView[2]; // world space camera forward
  planes[4].xyz = normalize(planes[4].xyz);
  planes[4].w = dot(planes[4].xyz, invView[3].xyz /* world camera pos */); // plane offset

  for (int cid = tid; cid < cluster_count; cid += TOTAL_THREAD_SIZE){
    // convert cluster id from cid to (cidx, cidy, cidz)
    int cidz = cid / cluster_screen_count;
    int screen_cid = cid - cidz * cluster_screen_count;
    int cidy = screen_cid / cluster_size.x;
    int cidx = screen_cid - cidy * cluster_size.x;
    cidy = cluster_size.y - 1 - cidy; // y = 0 is at the top of screen

    vec2 depthRange = vec2(cidz, cidz + 1.0) * depthPerCluster;

    // construct cluster data for intersection check
    vec2 current_pixel = vec2(cidx, cidy) * CLUSTER_PIXEL_SIZE;
    getPlanes(
      current_pixel / vec2(CameraUbo.screenAndLightInfo.xy),
      (current_pixel + 32.0) / vec2(CameraUbo.screenAndLightInfo.xy),
      invView, planes
    );

    // traverse lights and check intersection
    int clusterLightCount = 0;
    int clusterLightIndexBase = cid << 6; // 1(size of index) + 31(index list) ints for each cluster
    for (int i=0; i<CameraUbo.screenAndLightInfo.z && clusterLightCount<CLUSTER_LIGHT_CAPACITY; ++i){
      if (lightList[i].lightSourceType.x == 0){ // distant light
        ++clusterLightCount;
        lightIndices[clusterLightIndexBase + clusterLightCount] = i;
      } else if (intersect(planes, depthRange, lightList[i]) > 0.5){ // point light
        ++clusterLightCount;
        lightIndices[clusterLightIndexBase + clusterLightCount] = i;
      }
    }
    lightIndices[clusterLightIndexBase] = clusterLightCount;
  }
}
)";

  void SceneEditor::setupLightingResources() {
    auto& ctx = this->ctx();

    ResourceSystem::load_shader(ctx, "default_cluster_light.comp",
                                vk::ShaderStageFlagBits::eCompute, g_cluster_light);
    auto& clusterLightShader = ResourceSystem::get_shader("default_cluster_light.comp");
    ctx.acquireSet(clusterLightShader.layout(0), sceneLighting.clusterLightingSet);

    sceneLighting.lightList.reserve(32);

    sceneLighting.clusterLightPipeline = Pipeline{
        clusterLightShader, sizeof(glm::mat4) + sizeof(glm::vec4) + sizeof(glm::ivec3)};

    sceneLighting.clusterDimUbo = ctx.createBuffer(
        sizeof(glm::ivec2), vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eDeviceLocal);
    sceneLighting.clusterDimUbo.get().map();

    rebuildLightingFBO();
  }

  void SceneEditor::ensureLightListBuffer() {
    size_t targetBufferBytes = std::max((size_t)32, sceneLighting.lightList.size())
                               * sizeof(SceneEditor::SceneLightInfo);
    if (!sceneLighting.lightInfoBuffer
        || sceneLighting.lightInfoBuffer.get().getSize() < targetBufferBytes) {
      sceneLighting.lightInfoBuffer = ctx().createBuffer(
          targetBufferBytes, vk::BufferUsageFlagBits::eStorageBuffer,
          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
      sceneLighting.lightInfoBuffer.get().map();


      ctx().writeDescriptorSet(
        sceneLighting.lightInfoBuffer.get().descriptorInfo(),
        sceneLighting.clusterLightingSet,
        vk::DescriptorType::eStorageBuffer,
        0 /* binding */
      );
      ctx().writeDescriptorSet(
        sceneLighting.lightInfoBuffer.get().descriptorInfo(),
        sceneLighting.lightTableSet,
        vk::DescriptorType::eStorageBuffer,
        0 /* binding */
      );
    }
  }

  void SceneEditor::rebuildLightingFBO() {
    auto& ctx = this->ctx();

    // initialize lightInfoBuffer
    ensureLightListBuffer();

    // divide screen width per 32 pixels
    sceneLighting.clusterCountPerLine
        = (vkCanvasExtent.width + SceneLighting::CLUSTER_SCREEN_SIZE - 1)
          / SceneLighting::CLUSTER_SCREEN_SIZE;
    // divide screen height per 32 pixels
    sceneLighting.clusterCountPerDepth
        = sceneLighting.clusterCountPerLine
          * ((vkCanvasExtent.height + SceneLighting::CLUSTER_SCREEN_SIZE - 1)
             / SceneLighting::CLUSTER_SCREEN_SIZE);

    {
      glm::ivec2 tmp{sceneLighting.clusterCountPerLine, sceneLighting.clusterCountPerDepth};
      std::memcpy(sceneLighting.clusterDimUbo.get().mappedAddress(), &tmp, sizeof(tmp));
    }

    sceneLighting.clusterCount
        = sceneLighting.clusterCountPerDepth
          * SceneLighting::CLUSTER_Z_SLICE;  // divide screen depth into 32 parts
    sceneLighting.clusterLightInfoBuffer = ctx.createBuffer(
        sceneLighting.clusterCount * SceneLighting::CLUSTER_LIGHT_INDEX_CAPACITY
            * sizeof(int),  // cluster light index list
        vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);

    ctx.writeDescriptorSet(sceneLighting.lightInfoBuffer.get().descriptorInfo(),
                           sceneLighting.clusterLightingSet, vk::DescriptorType::eStorageBuffer,
                           0 /* binding */
    );
    ctx.writeDescriptorSet(sceneLighting.clusterLightInfoBuffer.get().descriptorInfo(),
                           sceneLighting.clusterLightingSet, vk::DescriptorType::eStorageBuffer,
                           1 /* binding */
    );

    ctx.writeDescriptorSet(sceneLighting.lightInfoBuffer.get().descriptorInfo(),
                           sceneLighting.lightTableSet, vk::DescriptorType::eStorageBuffer,
                           0 /* binding */
    );
    ctx.writeDescriptorSet(sceneLighting.clusterLightInfoBuffer.get().descriptorInfo(),
                           sceneLighting.lightTableSet, vk::DescriptorType::eStorageBuffer,
                           1 /* binding */
    );
    ctx.writeDescriptorSet(sceneLighting.clusterDimUbo.get().descriptorInfo(),
                           sceneLighting.lightTableSet, vk::DescriptorType::eUniformBufferDynamic,
                           2 /* binding */
    );
  }

  glm::vec3 colorTemperatureToRGB(float temperatureInKelvins) {
    glm::vec3 retColor;

    temperatureInKelvins = std::clamp(temperatureInKelvins, 1000.0f, 40000.0f) / 100.0f;

    if (temperatureInKelvins <= 66.0f) {
      retColor[0] = 1.0f;
      retColor[1] = std::clamp(0.39008157876901960784f * log(temperatureInKelvins) - 0.63184144378862745098f, 0.0f, 1.0f);
    } else {
      float t = temperatureInKelvins - 60.0f;
      retColor[0] = std::clamp(1.29293618606274509804f * pow(t, -0.1332047592f), 0.0f, 1.0f);
      retColor[1] = std::clamp(1.12989086089529411765f * pow(t, -0.0755148492f), 0.0f, 1.0f);
    }

    if (temperatureInKelvins >= 66.0f)
      retColor[2] = 1.0;
    else if (temperatureInKelvins <= 19.0f)
      retColor[2] = 0.0;
    else
      retColor[2] = std::clamp(0.54320678911019607843f * log(temperatureInKelvins - 10.0f) - 1.19625408914f, 0.0f, 1.0f);

    return retColor;
  }

  void SceneEditor::registerLightSource(Shared<LightPrimContainer> lightContainer) {
    if (lightContainer->lightType() == LightSourceType::NONE) return;
    if (lightContainer->lightType() != LightSourceType::POINT && lightContainer->lightType() != LightSourceType::DISTANT) return; // temp code

    auto& lightInfo = sceneLighting.lightList.emplace_back();

    lightInfo.lightSourceType = glm::ivec4(int(lightContainer->lightType()), 0, 0, 0);

    float intensity = lightContainer->intensity() * pow(2.0f, lightContainer->exposure());

    if (lightContainer->enableColorTemperature()) {
      glm::vec3 col = colorTemperatureToRGB(lightContainer->colorTemperature());
      lightInfo.color = glm::vec4(col, intensity);
    } else {
      lightInfo.color = glm::vec4(lightContainer->lightColor(), intensity);
    }
    if (lightContainer->lightType() == LightSourceType::DISTANT) {
      lightInfo.lightVec = lightContainer->lightVector();
    } else if (lightContainer->lightType() == LightSourceType::POINT) {
      lightInfo.lightVec = lightContainer->lightVector();
    } else {
      lightInfo.lightSourceType = glm::ivec4(int(LightSourceType::NONE), 0, 0, 0);
    }
  }

  inline void _getAABBFromSphere(const glm::vec3 center, float radius, glm::vec3& minPos,
                                 glm::vec3& maxPos) {
    minPos = center - radius;
    maxPos = center + radius;
  }

  void SceneEditor::updateClusterLighting() {
    ensureLightListBuffer();

    // check frustum visibility and emplace lights into
    glm::vec3 minPos, maxPos;
    const glm::vec3& cameraPos = -sceneRenderData.camera.get().position;
    int renderingLights = 0;
    for (auto& lightInfo : sceneLighting.lightList) {
      if (lightInfo.lightSourceType.x == 0) { // distant light
        ; // always pass
      } else if (lightInfo.lightSourceType.x == 1) { // point light
        // need to check sphere intersection
        const glm::vec3& center = lightInfo.lightVec;
        // if camera is not inside light sphere, then check if the sphere is visible in view
        if ((center - cameraPos).length() > lightInfo.lightVec.w) {
          _getAABBFromSphere(center, lightInfo.lightVec.w, minPos, maxPos);
          if (!sceneRenderData.camera.get().isAABBVisible(minPos, maxPos)) {  // light is not visible
            continue;
          }
        }
      } else {
        continue;
      }

      memcpy((SceneEditor::SceneLightInfo*)sceneLighting.lightInfoBuffer.get().mappedAddress()
                 + renderingLights,
             &lightInfo, sizeof(SceneEditor::SceneLightInfo));
      ++renderingLights;
    }
    if (renderingLights == 0) {
      return;
    }

    // Start running cluster lighting
    auto& ctx = this->ctx();
    fence.get().wait();
    auto& cmd = this->cmd.get();

    cmd.begin();

    (*cmd).bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                              /*pipeline layout*/ sceneLighting.clusterLightPipeline.get(),
                              /*firstSet*/ 0,
                              /*descriptor sets*/ {sceneLighting.clusterLightingSet},
                              /*dynamic offset*/ {}, ctx.dispatcher);
    (*cmd).bindPipeline(vk::PipelineBindPoint::eCompute, sceneLighting.clusterLightPipeline.get());

    const auto& cam = sceneRenderData.camera.get();
    // view
    (*cmd).pushConstants(sceneLighting.clusterLightPipeline.get(),
                         vk::ShaderStageFlagBits::eCompute, 0, sizeof(glm::mat4),
                         &cam.matrices.view);
    // camera info
    const glm::vec4 cameraInfo
        = glm::vec4(cam.getFov(), cam.getAspect(), cam.getNearClip(), cam.getFarClip());
    (*cmd).pushConstants(sceneLighting.clusterLightPipeline.get(),
                         vk::ShaderStageFlagBits::eCompute, sizeof(glm::mat4), sizeof(glm::vec4),
                         &cameraInfo);
    // screen size and lightCount
    const glm::ivec3 clusterInfo{vkCanvasExtent.width, vkCanvasExtent.height, renderingLights};
    (*cmd).pushConstants(
        sceneLighting.clusterLightPipeline.get(), vk::ShaderStageFlagBits::eCompute,
        sizeof(glm::mat4) + sizeof(glm::vec4), sizeof(glm::ivec3), &clusterInfo);

    (*cmd).dispatch(32, 16, 1);

    (*cmd).end();
    cmd.submit(fence.get(), /*reset fence*/ true, /*reset config*/ true);
    fence.get().wait();
  }
}  // namespace zs
