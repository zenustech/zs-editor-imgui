#pragma once
#include <latch>
#include <optional>

#include "IconsMaterialDesign.h"
#include "editor/ImguiRenderer.hpp"
#include "editor/widgets/WidgetComponent.hpp"
#include "imgui.h"
#include "widgets/SceneWidgetComponent.hpp"
#include "world/async/Executor.hpp"
#include "world/async/StateMachine.hpp"
#include "world/scene/Camera.hpp"
#include "world/scene/Primitive.hpp"
#include "zensim/profile/CppTimers.hpp"
#include "zensim/types/ImplPattern.hpp"
#include "zensim/vulkan/VkModel.hpp"
#include "zensim/vulkan/Vulkan.hpp"
#include "zensim/zpc_tpls/moodycamel/concurrent_queue/concurrentqueue.h"

namespace zs {

  glm::mat4 get_transform(const vec<float, 3> &translation, const vec<float, 3> &eulerXYZ,
                          const vec<float, 3> &scale);
  glm::mat4 get_transform(const vec<float, 4, 4> &transform);

  struct OptionWidget {
    void paint() {
      // ImGui::Selectable();
      ;
    }

    std::string _label;  // usually an icon is enough
    std::optional<std::string> _tooltip;
  };

#define ENABLE_FRUSTUM_CULLING 1
#define ENABLE_OCCLUSION_QUERY 1

  struct CameraControl {
    void trackCamera(Camera &camera, SceneEditor &sceneEditor);

    enum key_direction_e : int {
      front,
      back,
      left,
      right,
      up,
      down,
      turn_left,
      turn_right,
      look_up,
      look_down,
      num_directions
    };
    enum mouse_action_e : int { rotate = 0, translate_side, translate_advance, num_mouse_bindings };

    void setKeyState(int idx, int state) noexcept;
    int findDirectionIndex(ImGuiKey key) const noexcept;
    int findMouseAction(ImGuiMouseButton key) const noexcept;

    Signal<void(const glm::vec3 &)> _positionChanged;
    Signal<void(const glm::vec3 &)> _orientationChanged;

    bool onEvent(GuiEvent *e) { return _cameraState.onEvent(e); }
    void update(float dt);

    Camera *_cam;
    SceneEditor *_editor;
    StateMachine _cameraState;
    bool _dirty{false};
    // mouse
    float _mouseRotationScale{0.1f}, _mouseSideTranslationScale{0.01f},
        _mouseAdvanceTranslationScale{0.2f};
    ImGuiMouseButton _mouseBindings[num_mouse_bindings]
        = {ImGuiMouseButton_Left, ImGuiMouseButton_Right, ImGuiMouseButton_Middle};
    // key
    float _rotationSpeed{100.f}, _sideTranslationSpeed{0.01f}, _advanceTranslationSpeed{10.f};
    int _keyStates[num_directions] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    ImGuiKey _keyBindings[num_directions]
        = {ImGuiKey_W, ImGuiKey_S, ImGuiKey_A, ImGuiKey_D, ImGuiKey_Space,
           ImGuiKey_C, ImGuiKey_Q, ImGuiKey_E, ImGuiKey_R, ImGuiKey_F};
  };
  ActionWidgetComponent get_widget(Camera &camera, void * = nullptr);

  struct SceneEditor {
    enum layer_e { _scene = 0, _interaction, _config, _num_layers };

    SceneEditor() = default;
    void setup(VulkanContext &ctx, ImguiVkRenderer &renderer);

    void renderFrame(int bufferNo, vk::CommandBuffer cmd);  // render scene

    void updateSceneSet();

    void frameStat();

    void drawPath();
    Shared<SceneEditorWidgetComponent> &getWidget();
    ActionWidgetComponent getCameraWidget() {
      if (sceneRenderData.camera.isValid())
        return get_widget(sceneRenderData.camera.get(), this);
      else
        return {};
    }

    /// resources

    enum input_mode_e { _still = 0, _roaming, _select, _paint, _num_modes };
    struct SceneEditorRoamingMode {
      SceneEditorRoamingMode(SceneEditor &editor) : editor{editor} {}
      void paint();
      SceneEditor &editor;
    };
    struct SceneEditorSelectionMode {
      SceneEditorSelectionMode(SceneEditor &editor) : editor{editor} {}
      void paint();
      SceneEditor &editor;
    };
    struct SceneEditorPaintMode {
      SceneEditorPaintMode(SceneEditor &editor) : editor{editor} {}
      void paint();
      SceneEditor &editor;
    };

    struct SceneEditorInputMode {
      void paint() {
        match([](std::monostate) {}, [](auto &input) { input.get().paint(); })(_modes);
      }
      void turnTo(input_mode_e newMode, SceneEditor &editor);
      int numModes() const { return (int)_num_modes; }
      int currentMode() const { return (int)_index; }
      const char *getModeInfo(input_mode_e id) const;
      const char *getIconText(input_mode_e id) const;
      bool isPaintMode() const noexcept { return _index == input_mode_e::_paint; }
      bool isEditMode() const noexcept {
        return _index == input_mode_e::_select || _index == input_mode_e::_paint;
      }

      input_mode_e _index{_still};
      variant<std::monostate, Owner<SceneEditorRoamingMode>, Owner<SceneEditorSelectionMode>,
              Owner<SceneEditorPaintMode>>
          _modes;
    };

    inline static auto sampleBits = vk::SampleCountFlagBits::e4;
    inline static auto colorFormat = vk::Format::eR8G8B8A8Unorm;
    inline static auto depthFormat = vk::Format::eD32Sfloat;
    // inline static auto depthFormat = vk::Format::eD16Unorm;
    inline static auto pickFormat = vk::Format::eR32G32B32A32Sint;
    inline static constexpr bool reversedZ = true;

    enum icon_e { play = 0, pause, stop, simulate, speed_up, file, folder, num_icons };
    std::array<VkTexture, num_icons> iconTextures;

    ///
    /// viewport ui status
    ///
    bool viewportFocused{false}, viewportHovered{false};
    /// @note different from viewportHovered, mouse might hover overlaying widgets
    bool sceneHovered, sceneClicked;
    ImVec2 viewportMinScreenPos, viewportMaxScreenPos, imguiCanvasSize;
    bool guizmoUseSnap{true};
    float scaleSnap{0.1f}, rotSnap{10.f}, transSnap{0.1f};
    bool enableGuizmoScale{true}, enableGuizmoRot{true}, enableGuizmoTrans{true};
    bool primaryBeingDragged{false};
    ImVec2 primaryScenePanelOffset{0, 0};
    bool secondaryBeingDragged{false};
    ImVec2 secondaryScenePanelOffset{0, 0};

    float gizmoSize{100.f};

    std::optional<ImVec2> selectionStart{}, selectionEnd{};

    std::optional<glm::uvec2> paintCenter{};
    glm::vec3 paintColor{0.8f, 0.1f, 0.1f};
    float paintRadius = 10.f;

    zs::vec<float, 2> viewportMousePos;

    glm::vec3 getScreenPointCameraRayDirection() const;
    glm::vec3 getCameraPosition() const noexcept { return -sceneRenderData.camera.get().position; }

    int numFrameRenderModels;
    float framePerSecond = 0.0f;

    // [deprecated]
    SceneEditorInputMode inputMode;
    //
    // SceneEditorWidgetComponent _widget;
    CameraControl _camCtrl;

    bool showWireframe{true}, showIndex{true}, showCoordinate{true}, enableGuizmo{true},
        ignoreDepthTest{false}, showNormal{true}, showOutline{true}, drawTexture{false};
    enum draw_pipeline_e : u32 { draw_Color = 0, draw_Texture, num_DrawPipelines };
    int drawPipeline{draw_Color};  // color, texture
    struct SelectionRegion {
      glm::uvec2 offset, extent;
    };
    std::optional<SelectionRegion> selectionBox;
    std::vector<glm::uvec2> selectedIndices;

    // int focusObjId = -1, hoveredObjId = -1;
    Weak<ZsPrimitive> focusPrimPtr = {}, hoveredPrimPtr = {};
    glm::vec3 hoveredHitPt = {};

    inline std::string_view getFocusPrimLabel() const;
    inline std::string_view getHoveredPrimLabel() const;
    inline PrimIndex getFocusPrimId() const;
    inline PrimIndex getHoveredPrimId() const;

    struct {
      Signal<void(std::string_view fileLabel, std::string_view nodePath)> _transform;
    } _signals;

    Shared<SceneEditorWidgetComponent> _widget;

    ///
    /// vulkan
    ///

    VulkanContext &ctx() { return const_cast<VulkanContext &>(guiRenderer->ctx()); }
    const VulkanContext &ctx() const { return guiRenderer->ctx(); }

    ImguiVkRenderer *guiRenderer{nullptr};

    vk::Extent2D vkCanvasExtent;

    /// @note profiling
    enum vk_query_type_e : u32 {
      vkq_scene = 0,
      vkq_pick = 2,
      vkq_grid = 4,
      vkq_text_comp = 6,
      vkq_text_render = 8,
      vkq_wireframe = 100,
      vkq_total = 1000
    };
    Owner<QueryPool> queryPool;  // for vulkan profiling
    double scenePassTime, pickPassTime, gridPassTime, textPassTime, textRenderPassTime;

    Owner<Fence> fence;
    Owner<VkCommand> cmd;
    UniquePtr<Scheduler> renderScheduler;
    std::vector<int> currentlyAllocatedSecondaryCmdNum;
    std::vector<int> currentlyUsedSecondaryCmdNum;
    std::vector<vk::CommandBuffer> currentRenderVkCmds;
    Owner<ImageSampler> sampler;

    void initialRenderSetup() {
      auto numWorkers = 4;
      renderScheduler = UniquePtr<Scheduler>(new Scheduler(numWorkers));
      // auto &pool = ctx.env().pools(vk_queue_e::graphics);
      // vkCmd.push_back(pool.acquireSecondaryVkCommand());
      // for (int i = 0; i < renderScheduler->numWorkers(); ++i) {
      //   renderScheduler->enqueue([&]() {/*setup*/}, i);
      currentlyAllocatedSecondaryCmdNum.resize(numWorkers, 0);
      currentlyUsedSecondaryCmdNum.resize(numWorkers, 0);
    }
    /// @note might be called several times per frame, but draw tags are usually
    /// cleared once per frame
    void resetDrawStates() {
      for (auto &[_, drawn] : currentVisiblePrimsDrawn) drawn = 0;
      sceneRenderData.sceneCtx = &getCurrentScene();
      sceneRenderData.currentTimeCode = sceneRenderData.sceneCtx->getCurrentTimeCode();
    }
    void resetVkCmdCounter() {
      for (auto &num : currentlyUsedSecondaryCmdNum) num = 0;
    }
    /// @brief must be called within a certain worker thread
    VkCommand &nextVkCommand();
    std::vector<vk::CommandBuffer> getCurrentSecondaryVkCmds();
    inline auto evalRenderChunkSize(int nWork) const noexcept {
      return std::max(
          1, (int)((nWork + renderScheduler->numWorkers() - 1) / renderScheduler->numWorkers()));
    }

    /// scene data and renderer
    struct SceneRenderTargets {
      // pass 1
      Owner<Image> msaaColor, msaaDepth, msaaPick;
      Owner<Image> color, depth;
      // pass 2
      Owner<Image> postFxVis;

      Owner<Framebuffer> fbo;
      // for imgui image display
      vk::DescriptorSet renderedSceneColorSet, renderedSceneDepthStencilSet;
      vk::DescriptorSet postFxColorSet;
    };
    SceneRenderTargets sceneAttachments;

    struct SceneRenderer {
      Owner<RenderPass> renderPass;
      // first pass
      struct {
        glm::mat4 proj, model;
      } params;
      Owner<ShaderModule> vertShader, fragShader;
      Owner<Pipeline> opaquePipeline, transparentPipeline;
      // second pass
      Owner<ShaderModule> postFxVertShader, postFxFragShader;
      Owner<Pipeline> postFxPipeline;
      // coordinate grid
      Owner<ShaderModule> gridVertShader, gridFragShader;
      Owner<Pipeline> gridPipeline;
      // for texture preview
      Owner<Pipeline> bindlessPipeline;

      struct {
        glm::mat4 model;
        float radius = 10;
      } pointVertParams;

      Owner<ShaderModule> pointVertShader, pointFragShader;
      Owner<Pipeline> pointOpaquePipeline, pointTransparentPipeline;
    };
    SceneRenderer sceneRenderer;

    // std::atomic<u32> _numVisBuffersTBD;  // initialized upon
    // 'onVisiblePrimsChanged'

    struct VisBufferTaskEvent {
      int _delta{0};
    };
    struct VisBufferReadyEvent {};

    struct DisplayingVisBuffers;
    struct PreProcessingVisBuffers;
    struct PostProcessingVisBuffers;
    using VisBufferStates
        = std::variant<DisplayingVisBuffers, PreProcessingVisBuffers, PostProcessingVisBuffers>;
    using OptionalState = std::optional<VisBufferStates>;

    struct VisBufferStateBase {
      VisBufferStateBase(SceneEditor *se = nullptr) noexcept : sceneEditor{se} {}
      template <typename Event> OptionalState process(const Event &event) { return {}; }
      SceneEditor *sceneEditor;
    };

    struct DisplayingVisBuffers : VisBufferStateBase {
      DisplayingVisBuffers(SceneEditor *se = nullptr) noexcept;

      using VisBufferStateBase::process;
      OptionalState process(const VisBufferTaskEvent &event);
    };
    struct PreProcessingVisBuffers : VisBufferStateBase {
      PreProcessingVisBuffers(SceneEditor *se = nullptr, int n = 0) noexcept;

      using VisBufferStateBase::process;
      OptionalState process(const VisBufferTaskEvent &event);

      int _numPendingPreprocessTasks;
    };
    struct PostProcessingVisBuffers : VisBufferStateBase {
      PostProcessingVisBuffers(SceneEditor *se = nullptr) noexcept;

      using VisBufferStateBase::process;
      OptionalState process(const VisBufferReadyEvent &event);
    };

    /// @note state machine
    struct VisBufferHandler {
      VisBufferHandler() : _id{zs_resources().next_widget_id()} {}
      ~VisBufferHandler() { reset(); }
      void reset() {
        if (_id) {
          zs_resources().onPrimInFlightChanged().removeSlot(_id);
          zs_resources().recycle_widget_id(_id);
          _id = 0;
        }
      }
      VisBufferHandler(VisBufferHandler &&o) noexcept
          : _id{zs::exchange(o._id, 0)}, _state{zs::exchange(o._state, DisplayingVisBuffers{})} {}
      VisBufferHandler &operator=(VisBufferHandler &&o) noexcept {
        if (this == zs::addressof(o)) return *this;
        // no swap idiom here to manage signal
        reset();
        _id = zs::exchange(o._id, 0);
        _state = zs::exchange(o._state, DisplayingVisBuffers{});
        return *this;
      }

      template <typename Event> void process(const Event &event) {
        auto ret = std::visit(
            [&event](auto &state) -> OptionalState { return state.process(event); }, _state);
        if (ret.has_value()) _state = zs::move(*ret);
      }
      VisBufferStates _state{DisplayingVisBuffers{}};
      u32 _id;
    };
    void issueVisBufferUpdateEvents();

    u32 _visBufferReady{1};  // only render if this is signaled
    UniquePtr<VisBufferHandler> _visBufferStatus{};

    struct ScenePickPass {
      // render
      Owner<RenderPass> renderPass;

      // pass 1
      Owner<Image> pickBuffer;
      // Owner<ShaderModule> vertShader, fragShader;
      Owner<Pipeline> pipeline;
      // pass 2
      Owner<Image> postFxVis;
      // Owner<ShaderModule> postFxVertShader, postFxFragShader;
      Owner<Pipeline> postFxPipeline;

      vk::DescriptorSet postFxInputAttachmentSet;
      Owner<Framebuffer> fbo;
      // Owner<ImageSampler> sampler;

      // Owner<Buffer> pickBufferHost;

      // for imgui image display
      vk::DescriptorSet pickSet;
    };
    ScenePickPass scenePickPass;

    struct SceneAugmentRenderer {
      enum TextAlign { alignLeft, alignCenter, alignRight };
      /// overlay contents render
      // overlay text render
      Owner<RenderPass> renderPass;
      // Owner<ShaderModule> overlayVertShader, overlayFragShader;
      Owner<Pipeline> overlayPipeline;
      vk::DescriptorSet overlayFontSet;
      Owner<ImageSampler> sampler;
      VkTexture fontTexture;
      u32 numLetters;

      /// wireframe
      // Owner<ShaderModule> wiredVertShader, wiredFragShader;
      Owner<Pipeline> wiredPipeline;

      /// overlay text generation (compute)
      // Owner<ShaderModule> genTextShader;
      Owner<Pipeline> genTextPipeline;
      Owner<Buffer> fontTemplateBuffer;  // stb_fontdata
      vk::DescriptorSet textGenSet;
      bool overlayTextNeedUpdate = true;

      // Owner<Buffer> textPosBuffer, textUvBuffer;
      Owner<Buffer> overlayTextBuffer, counterBuffer;

      // Owner<ShaderModule> selectionShader;
      Owner<Pipeline> selectionPipeline;
      Owner<Buffer> selectedIndicesBuffer;
      vk::DescriptorSet selectionSet;

      Owner<Pipeline> paintPipeline;
      vk::DescriptorSet paintSet;

      Owner<Framebuffer> fbo;

      float scale = 1.f;
    };
    SceneAugmentRenderer sceneAugmentRenderer;

    struct SceneGridRenderer {
      Owner<RenderPass> renderPass;
      Owner<Framebuffer> fbo;
      Owner<Pipeline> gridPipeline;
    } sceneGridRenderer;

    struct SceneOutlineRenderer {
      Owner<RenderPass> baseRenderPass;
      Owner<RenderPass> renderPass;
      Owner<RenderPass> swapRenderPass;
      Owner<Framebuffer> baseFBO;
      Owner<Framebuffer> FBO;
      Owner<Framebuffer> swapFBO;
      Owner<Pipeline> outlineBasePipeline;
      Owner<Pipeline> outlinePipeline;
      Owner<Pipeline> outlineSwapPipeline;

      Owner<ImageSampler> sampler;
      Owner<Image> outlineImage;
      Owner<Image> outlineSwapImage;
      vk::DescriptorSet outlineImageDescriptorSet;
      vk::DescriptorSet outlineSwapImageDescriptorSet;
    } sceneOutlineRenderer;

    // Order-Independent Transparency
    struct SceneOITRenderer {
      Owner<ImageSampler> sampler;

      // accumulation
      Owner<RenderPass> accumRenderPass;
      Owner<Pipeline> accumPipeline;
      Owner<Framebuffer> accumFBO;
      Owner<Image> accumImage0;
      Owner<Image> accumImage1;
      vk::DescriptorSet accumImageDescriptorSet;

      // post process blending
      Owner<RenderPass> postRenderPass;
      Owner<Pipeline> postPipeline;
      Owner<Framebuffer> postFBO;
    } sceneOITRenderer;

    struct SceneLightInfo {
      glm::vec4 sphere; // xyz: world space position, w: radius
      glm::vec4 color; // rgb: color, a: intensity
    };

    struct SceneLighting {
      Owner<Pipeline> clusterLightPipeline;
      vk::DescriptorSet clusterLightingSet;
      vk::DescriptorSet lightTableSet;

      Owner<zs::Buffer> screenInfoBuffer;
      Owner<zs::Buffer> lightInfoBuffer;
      Owner<zs::Buffer> clusterLightInfoBuffer;

      std::vector<SceneLightInfo> lightList;
      size_t clusterCountPerLine;
      size_t clusterCountPerDepth;
      size_t clusterCount;

      static const size_t CLUSTER_SCREEN_SIZE = 32;
      static const size_t CLUSTER_Z_SLICE = 32;
      static const size_t CLUSTER_LIGHT_INDEX_CAPACITY = 32;
    } sceneLighting;

    struct SceneOcclusionQuery {
      Owner<QueryPool> queryPool;
      Owner<zs::Buffer> queryBuffer;
      std::vector<int> occlusionResults;
      Owner<RenderPass> renderPass;
      Owner<Pipeline> renderPipeline;
      Owner<Framebuffer> occlusionFBO;
      int actualQueryCount;
      std::map<void *, int> primToQueryIndex;
    } sceneOcclusionQuery;

    glm::vec4 *beginText() {
      sceneAugmentRenderer.numLetters = 0;
      sceneAugmentRenderer.counterBuffer.get().map();
      auto counter = (u32 *)sceneAugmentRenderer.counterBuffer.get().mappedAddress();
      *counter = 0;
      // *(counter + 1) = ~((u32)0);
      sceneAugmentRenderer.counterBuffer.get().unmap();
      sceneAugmentRenderer.counterBuffer.get().flush();
      return nullptr;
    }
    void endText() {}

    struct SceneRenderData {
      // std::vector<VkModel> models;

      std::string sceneLabel{g_defaultSceneLabel};  // through this the scene is drawn
      std::vector<std::string> path{""};
      int selectedObj{-1};
      // refreshed & updated per draw
      SceneContext *sceneCtx;
      TimeCode currentTimeCode;

      Owner<Buffer> sceneCameraUbo;
      vk::DescriptorSet sceneCameraSet;
      vk::DescriptorSet postFxInputAttachmentSet;
      ValueOrRef<Camera> camera;
    };
    SceneRenderData sceneRenderData;
    struct ZsPrimComparator {
      bool operator()(Weak<ZsPrimitive> a, Weak<ZsPrimitive> b) const {
        return a.lock().get() < b.lock().get();
      }
    };
    std::set<Weak<ZsPrimitive>, ZsPrimComparator> currentVisiblePrimsSet;
    std::vector<ZsPrimitive *> currentVisiblePrims;
    std::vector<int> currentVisiblePrimsDrawnTags;
    // LBvh<3> currentVisiblePrimsBvh;
    std::map<PrimIndex, size_t> primIdToVisPrimId;
    std::map<void *, int> currentVisiblePrimsDrawn;
    std::map<void *, bool> isCulledByFrustum;
    Signal<void(const std::vector<Weak<ZsPrimitive>> &)> onVisiblePrimsChanged;

    SceneContext &getCurrentScene() {
      return zs_resources().get_scene_context(sceneRenderData.sceneLabel);
    }
    std::vector<Weak<ZsPrimitive>> getCurrentScenePrims() {
      auto &scene = getCurrentScene();
      if (sceneRenderData.selectedObj == -1) {
        return scene.getPrimitives();
      } else if (sceneRenderData.path.size() != 0) {
        std::vector<std::string> path = sceneRenderData.path;
        path.resize(sceneRenderData.selectedObj + 1);
        // fmt::print("searching path: {}, path len: {}\n", path[0], path.size());
        return {scene.getPrimitiveByPath(path)};
      }
      return {};
    }
    auto &getCurrentVisiblePrims() noexcept { return currentVisiblePrims; }
    const auto &getCurrentVisiblePrims() const noexcept { return currentVisiblePrims; }

    auto getCurrentScenePrimsRecurse() {
      auto prims = getCurrentScenePrims();
      auto comp = [](Weak<ZsPrimitive> a, Weak<ZsPrimitive> b) {
        return a.lock().get() < b.lock().get();
      };
      std::set<Weak<ZsPrimitive>, RM_CVREF_T(comp)> ret;
      zs::function<void(Weak<ZsPrimitive>)> gatherChildren = [&](Weak<ZsPrimitive> prim_) {
        auto prim = prim_.lock();
        if (prim) {
          ret.emplace(prim);
          auto nChilds = prim->numChildren();
          for (int j = 0; j < nChilds; ++j) {
            auto ch = prim->getChild(j);
            if (ret.emplace(ch).second) gatherChildren(ch);
          }
        }
      };
      for (auto &prim : prims) gatherChildren(prim);
      return ret;
    }
    Weak<ZsPrimitive> getScenePrimById(PrimIndex id) {
      if (id == -1) return {};
      return getCurrentScene().getPrimitiveByIdRecurse(id);
    }
    ZsPrimitive *getScenePrimByIndex(int i) { return getCurrentScene().getPrimByIndex(i); }
    // ZsPrimitive *getScenePrimById(PrimIndex id) { return
    // getCurrentScene().getPrimById(id); }

    void rebuildAttachments();
    void update(float dt);

  private:
    void setupRenderResources();
    void rebuildSceneFbos();
    void prepareRender();
    void renderSceneBuffers();  // orthogonal to renderFrame

    void loadSampleModels();
    void loadSampleScene();
    void loadUVTestScene();
    // void loadGrid();

    void setupPickResources();
    void rebuildPickFbos();
    void renderFramePickBuffers();  // orthogonal to renderFrame

    void setupAugmentResources();
    void rebuildAugmentFbo();
    void renderSceneAugmentView();

    // outline rendering
    void setupOutlineResources();
    void rebuildOutlineFbo();
    void renderOutline();
    void _renderOutlineForOneModel(VulkanContext &, VkCommand &, const VkModel &,
                                   const glm::mat4 &transform, const float *);

    // occlusion query
    void setupOcclusionQueryResouces();
    void ensureOcclusionQueryBuffer(const VkCommand &cmd, size_t size);
    void prepareAndDrawAABB(const VkCommand &renderCmd, const glm::vec3 &minPos,
                            const glm::vec3 &maxPos);
    void rebuildOcclusionQueryFbo();
    void runOcclusionQuery();
    void getOcclusionQueryResults();

    // Order-Independent Transparency
    void setupOITResources();
    void rebuildOITFBO();
    void renderTransparent();

    // cluster based lighting
    void setupLightingResources();
    void rebuildLightingFBO();
    void ensureLightListBuffer();
    void registerLightSource(Shared<LightPrimContainer> lightContainer);
    void updateClusterLighting();
  };

  std::string_view SceneEditor::getFocusPrimLabel() const {
    if (auto prim = focusPrimPtr.lock()) return prim->label();
    return (const char *)u8"<空>";
  }
  std::string_view SceneEditor::getHoveredPrimLabel() const {
    if (auto prim = hoveredPrimPtr.lock()) return prim->label();
    return (const char *)u8"<空>";
  }
  PrimIndex SceneEditor::getFocusPrimId() const {
    if (auto prim = focusPrimPtr.lock()) return prim->id();
    return -1;
  }
  PrimIndex SceneEditor::getHoveredPrimId() const {
    if (auto prim = hoveredPrimPtr.lock()) return prim->id();
    return -1;
  }

}  // namespace zs