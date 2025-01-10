#include "SceneEditor.hpp"

#include "world/scene/Primitive.hpp"

namespace zs{
  static const char g_cluster_light[] = R"(
#version 450

const int CLUSTER_PIXEL_SIZE = 32;
const int CLUSTER_Z_SIZE = 32;
const int CLUSTER_LIGHT_CAPACITY = 31;

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

const float Deg2Rad = 3.1415926535 / 180.0;

struct LightInfo{
  vec4 sphere; // xyz: world space position, w: radius
  vec4 color; // rgb: color, a: intensity
};

/* input */
layout(push_constant) uniform CameraInfo {
  layout (offset = 0) mat4 view;
  layout (offset = 64) vec4 cameraInfo; // fov, aspect, near, far
  layout (offset = 80) vec4 cameraPos; // world space camera position
  layout (offset = 96) ivec3 screenAndLightInfo; // screen width, screen height, light count
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
    planes[i].w = dot(planes[i].xyz, CameraUbo.cameraPos.xyz); // plane offset
  }
}

float intersect(in vec4 planes[5], in vec2 depthRange, in LightInfo lightInfo){
  float result = 1.0;

  // check interection with four planes for current cluster
  for (int i=0; i<4; ++i){
    result *= step(dot(planes[i].xyz, lightInfo.sphere.xyz) - planes[i].w, lightInfo.sphere.w);
  }

  // deal with forward direction
  float depth = dot(planes[4].xyz, lightInfo.sphere.xyz) - planes[4].w; // positive view space depth
  result *= step(depthRange.x, depth + lightInfo.sphere.w) * step(depth - lightInfo.sphere.w, depthRange.y); // sphere depth should be in depth range

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
  planes[4].w = dot(planes[4].xyz, CameraUbo.cameraPos.xyz); // plane offset

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
    int clusterLightIndexBase = cid << 5; // 1(size of index) + 31(index list) ints for each cluster
    for (int i=0; i<CameraUbo.screenAndLightInfo.z && clusterLightCount<CLUSTER_LIGHT_CAPACITY; ++i){
      if (intersect(planes, depthRange, lightList[i]) > 0.5){
        ++clusterLightCount;
        lightIndices[clusterLightIndexBase + clusterLightCount] = i;
      }
    }
    lightIndices[clusterLightIndexBase] = clusterLightCount;
    
    float dist = dot(planes[0].xyz, lightList[0].sphere.xyz) - planes[0].w;
    lightIndices[clusterLightIndexBase + 29] = int(planes[0].x * 1000.0);
    lightIndices[clusterLightIndexBase + 30] = int(planes[0].y * 1000.0);
    lightIndices[clusterLightIndexBase + 31] = int(dist * 2.0);
  }
}
)";

  void SceneEditor::setupLightingResources() {
    auto& ctx = this->ctx();

    ResourceSystem::load_shader(ctx, "default_cluster_light.comp", vk::ShaderStageFlagBits::eCompute, g_cluster_light);
    auto& clusterLightShader = ResourceSystem::get_shader("default_cluster_light.comp");
    ctx.acquireSet(clusterLightShader.layout(0), sceneLighting.clusterLightingSet);

    sceneLighting.lightList.reserve(32);

    sceneLighting.clusterLightPipeline = Pipeline{ clusterLightShader, sizeof(glm::mat4) + sizeof(glm::vec4) * 2 + sizeof(glm::ivec3)};

    rebuildLightingFBO();
  }

  void SceneEditor::ensureLightListBuffer() {
    size_t targetBufferBytes = std::max((size_t)32, sceneLighting.lightList.size()) * sizeof(SceneEditor::SceneLightInfo);
    if (!sceneLighting.lightInfoBuffer || sceneLighting.lightInfoBuffer.get().getSize() < targetBufferBytes) {
      sceneLighting.lightInfoBuffer = ctx().createBuffer(
        targetBufferBytes,
        vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached
      );
      sceneLighting.lightInfoBuffer.get().map();
    }
  }

  void SceneEditor::rebuildLightingFBO() {
    auto& ctx = this->ctx();

    // initialize lightInfoBuffer
    ensureLightListBuffer();

    // divide screen width per 32 pixels
    sceneLighting.clusterCountPerLine = (viewportPanelSize.width + SceneLighting::CLUSTER_SCREEN_SIZE - 1) / SceneLighting::CLUSTER_SCREEN_SIZE;
    // divide screen height per 32 pixels
    sceneLighting.clusterCountPerDepth = sceneLighting.clusterCountPerLine * ((viewportPanelSize.height + SceneLighting::CLUSTER_SCREEN_SIZE - 1) / SceneLighting::CLUSTER_SCREEN_SIZE);
    sceneLighting.clusterCount = sceneLighting.clusterCountPerDepth * SceneLighting::CLUSTER_Z_SLICE; // divide screen depth into 32 parts
    sceneLighting.clusterLightInfoBuffer = ctx.createBuffer(
      sceneLighting.clusterCount * SceneLighting::CLUSTER_LIGHT_INDEX_CAPACITY * sizeof(int), // cluster light index list
      vk::BufferUsageFlagBits::eStorageBuffer,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached
    );

    ctx.writeDescriptorSet(
      sceneLighting.lightInfoBuffer.get().descriptorInfo(),
      sceneLighting.clusterLightingSet,
      vk::DescriptorType::eStorageBuffer,
      0 /* binding */
    );
    ctx.writeDescriptorSet(
      sceneLighting.clusterLightInfoBuffer.get().descriptorInfo(),
      sceneLighting.clusterLightingSet,
      vk::DescriptorType::eStorageBuffer,
      1 /* binding */
    );

    ctx.writeDescriptorSet(
      sceneLighting.lightInfoBuffer.get().descriptorInfo(),
      sceneLighting.lightTableSet,
      vk::DescriptorType::eStorageBuffer,
      0 /* binding */
    );
    ctx.writeDescriptorSet(
      sceneLighting.clusterLightInfoBuffer.get().descriptorInfo(),
      sceneLighting.lightTableSet,
      vk::DescriptorType::eStorageBuffer,
      1 /* binding */
    );
  }

  void SceneEditor::registerLightSource(Shared<LightPrimContainer> lightContainer) {
    // the minimum contribute to fragment color should be 0.5 / 256.0, so the light range is intensity * sqrt(512.0)
    float lightRadius = sqrt(lightContainer->intensity() * 512.0f);
    auto& lightInfo = sceneLighting.lightList.emplace_back();

    lightInfo.sphere = glm::vec4(lightContainer->lightPosition(), lightRadius);
    lightInfo.color = glm::vec4(lightContainer->lightColor(), lightContainer->intensity());
  }

  inline void _getAABBFromSphere(const glm::vec3 center, float radius, glm::vec3& minPos, glm::vec3& maxPos) {
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
      const glm::vec3& center = lightInfo.sphere;
      if ((center - cameraPos).length() > lightInfo.sphere.w) { // camera is not inside light sphere
        _getAABBFromSphere(center, lightInfo.sphere.w, minPos, maxPos);
        if (!sceneRenderData.camera.get().isAABBVisible(minPos, maxPos)) { // light is not visible
          continue;
        }
      }

      memcpy(
        (SceneEditor::SceneLightInfo*)sceneLighting.lightInfoBuffer.get().mappedAddress() + renderingLights,
        &lightInfo, sizeof(SceneEditor::SceneLightInfo)
      );
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
      /*descriptor sets*/{ sceneLighting.clusterLightingSet },
      /*dynamic offset*/{}, ctx.dispatcher);
    (*cmd).bindPipeline(vk::PipelineBindPoint::eCompute, sceneLighting.clusterLightPipeline.get());

    const auto& cam = sceneRenderData.camera.get();
    // view
    (*cmd).pushConstants(sceneLighting.clusterLightPipeline.get(),
      vk::ShaderStageFlagBits::eCompute, 0, sizeof(glm::mat4),
      &cam.matrices.view);
    // camera info
    const glm::vec4 cameraInfo = glm::vec4(cam.getFov(), cam.getAspect(), cam.getNearClip(), cam.getFarClip());
    (*cmd).pushConstants(sceneLighting.clusterLightPipeline.get(),
      vk::ShaderStageFlagBits::eCompute, sizeof(glm::mat4), sizeof(glm::vec4),
      &cameraInfo);
    // camera position
    (*cmd).pushConstants(sceneLighting.clusterLightPipeline.get(),
      vk::ShaderStageFlagBits::eCompute, sizeof(glm::mat4) + sizeof(glm::vec4), sizeof(glm::vec4),
      &cameraPos);
    // screen size and lightCount
    const glm::ivec3 clusterInfo{ viewportPanelSize.width, viewportPanelSize.height, renderingLights };
    (*cmd).pushConstants(sceneLighting.clusterLightPipeline.get(),
      vk::ShaderStageFlagBits::eCompute, sizeof(glm::mat4) + sizeof(glm::vec4) * 2, sizeof(glm::ivec3),
      &clusterInfo);

    (*cmd).dispatch(32, 16, 1);

    (*cmd).end();
    cmd.submit(fence.get(), /*reset fence*/ true, /*reset config*/ true);
    fence.get().wait();
  }
}
