#include "SceneWidgetComponent.hpp"

#include "../SceneEditor.hpp"
#include "editor/widgets/WidgetDrawUtilities.hpp"
#include "imgui_internal.h"
//
#include "IconsMaterialSymbols.h"
#include "editor/widgets/WidgetComponent.hpp"
#include "imgui.h"
//
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/transform.hpp>

#include "ImGuizmo.h"

namespace zs {

  void SceneEditorRoamingMode::paint() {}

  static StateMachine selection_operation_statemachine(SceneEditorSelectionMode &selectionMode) {
#if 0
    int activeMouseButton = 0;  // ImGuiMouseButton
    int mouseAction = -1;
    for (;;) {
      auto e__ = co_await zs::Event<GuiEvent *>{};
      auto e_ = std::get<GuiEvent *>(e__);

      switch (e_->getGuiEventType()) {
        case gui_event_mousePressed:
          // if (((WidgetNode *)cameraCtrl._editor->getWidget()->getZsUserPointer())
          //        ->windowHovered())
          if (cameraCtrl._editor->sceneHovered) {
            auto e = dynamic_cast<MousePressEvent *>(e_);
            auto idx = cameraCtrl.findMouseAction(e->button());
            if (idx != -1) {
              activeMouseButton = e->button();
              mouseAction = idx;
            }
          }
          break;
        case gui_event_mouseReleased: {
          auto e = dynamic_cast<MouseReleaseEvent *>(e_);
          if (activeMouseButton == e->button()) {
            activeMouseButton = -1;
            mouseAction = -1;
          }
          break;
        }
        case gui_event_mouseMoved: {
          auto e = dynamic_cast<MouseMoveEvent *>(e_);
          Camera &camera = *cameraCtrl._cam;
          auto delta = e->getDelta();
          switch (mouseAction) {
            case CameraControl::mouse_action_e::rotate: {
              camera.yaw(delta.x * cameraCtrl._mouseRotationScale);
              camera.pitch(-delta.y * cameraCtrl._mouseRotationScale);

              cameraCtrl._dirty = true;
              // camera.updateViewMatrix();
              // editor.sceneAugmentRenderer.overlayTextNeedUpdate = true;
              break;
            }
            case CameraControl::mouse_action_e::translate_side: {
              camera.translateHorizontal(-delta.x * cameraCtrl._mouseSideTranslationScale);
              camera.translateVertical(delta.y * cameraCtrl._mouseSideTranslationScale);

              cameraCtrl._dirty = true;
              break;
            }
            case CameraControl::mouse_action_e::translate_advance: {
              camera.translateForward(-delta.y * cameraCtrl._mouseAdvanceTranslationScale);
              camera.translateHorizontal(delta.x * cameraCtrl._mouseAdvanceTranslationScale);

              cameraCtrl._dirty = true;
              break;
            }
            default:;
          }
          break;
        }
        case gui_event_keyPressed:
          if (cameraCtrl._editor->viewportFocused) {
            auto e = dynamic_cast<KeyPressEvent *>(e_);
            auto idx = cameraCtrl.findDirectionIndex(e->key());
            if (idx >= 0) cameraCtrl.setKeyState(idx, 1);
          }
          break;
        case gui_event_keyReleased:
          if (cameraCtrl._editor->viewportFocused) {
            auto e = dynamic_cast<KeyReleaseEvent *>(e_);
            auto idx = cameraCtrl.findDirectionIndex(e->key());
            if (idx >= 0) cameraCtrl.setKeyState(idx, 0);
          }
          break;
        default:
          break;
      }  // end guievent switch
    }
#endif
  }
  void SceneEditorSelectionMode::paint() {
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSetCurrent(_interaction);

    /// selection
    if (editor.sceneClicked) editor.selectionStart = ImGui::GetMousePos();
    if (editor.selectionStart.has_value() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      // if (ImGui::IsItemHovered()) selectionEnd = ImGui::GetMousePos();
      editor.selectionEnd = ImGui::GetMousePos();
      (*editor.selectionEnd).x = std::clamp((*editor.selectionEnd).x, editor.canvasMinCursorPos.x,
                                            editor.canvasMaxCursorPos.x);
      (*editor.selectionEnd).y = std::clamp((*editor.selectionEnd).y, editor.canvasMinCursorPos.y,
                                            editor.canvasMaxCursorPos.y);

      drawList->AddRect(*editor.selectionStart, *editor.selectionEnd,
                        ImGui::GetColorU32(IM_COL32(0, 130, 216, 255)));  // Border
      drawList->AddRectFilled(*editor.selectionStart, *editor.selectionEnd,
                              ImGui::GetColorU32(IM_COL32(0, 130, 216, 50)));  // Background
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && editor.selectionStart.has_value()
        && editor.selectionEnd.has_value()) {
      // init selection box here for selection compute later
      editor.selectionBox = SelectionRegion{};
      glm::uvec2 mi{zs::min((*editor.selectionStart).x, (*editor.selectionEnd).x),
                    zs::min((*editor.selectionStart).y, (*editor.selectionEnd).y)},
          ma{1 + zs::max((*editor.selectionStart).x, (*editor.selectionEnd).x),
             1 + zs::max((*editor.selectionStart).y, (*editor.selectionEnd).y)};
      (*editor.selectionBox).extent = ma - mi;
      (*editor.selectionBox).offset
          = mi - glm::uvec2{editor.canvasMinCursorPos.x, editor.canvasMinCursorPos.y};

      editor.selectionStart = editor.selectionEnd = {};
    }

    ImGui::SetCursorScreenPos(editor.canvasMinCursorPos);
  }

  /// paint
  void SceneEditorPaintMode::paint() {
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSetCurrent(_interaction);

    /// selection
    if (editor.sceneHovered) {
      editor.paintRadius
          += ImGui::GetIO().MouseWheel * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 5.f : 1.f);
      if (editor.paintRadius <= 1.f + detail::deduce_numeric_epsilon<float>() * 10)

        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
      if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        editor.paintCenter = glm::uvec2{ImGui::GetMousePos().x - editor.canvasMinCursorPos.x,
                                        ImGui::GetMousePos().y - editor.canvasMinCursorPos.y};
        drawList->AddCircle(ImGui::GetMousePos(), editor.paintRadius, IM_COL32_WHITE, 0, 2.f);
      } else {
        drawList->AddCircle(ImGui::GetMousePos(), editor.paintRadius, IM_COL32_WHITE, 0, 1.f);
      }
    }
  }

  void SceneEditorInteractionMode ::paint() {
    match([](std::monostate) {}, [](auto &input) { input.get().paint(); })(_modes);
  }

  void SceneEditorInteractionMode::turnTo(input_mode_e newMode, SceneEditor &editor) {
    _index = newMode;
    switch (newMode) {
      case input_mode_e::_roaming:
        _modes = Owner<SceneEditorRoamingMode>{editor};
        break;
      case input_mode_e::_select:
        _modes = Owner<SceneEditorSelectionMode>{editor};
        break;
      case input_mode_e::_paint:
        _modes = Owner<SceneEditorPaintMode>{editor};
        break;
      default:
        _modes = std::monostate{};
    }
  }
  const char *SceneEditorInteractionMode::getModeInfo(input_mode_e id) const {
    switch (id) {
      case input_mode_e::_roaming:
        return (const char *)u8"漫游";
      case input_mode_e::_select:
        return (const char *)u8"编辑";
      case input_mode_e::_paint:
        return (const char *)u8"笔刷";
      default:
        return (const char *)u8"观察";
    }
  }
  const char *SceneEditorInteractionMode::getIconText(input_mode_e id) const {
    switch (id) {
      case input_mode_e::_roaming:
        return ICON_MD_PAGEVIEW;
      case input_mode_e::_select:
        return ICON_MD_SELECT_ALL;
      case input_mode_e::_paint:
        return ICON_MD_BRUSH;
      default:
        return ICON_MD_PHOTO;
    }
  }

  void SceneEditorWidgetComponent::drawPath() {
    // helper
    auto searchIndex = [](const auto &candidates, const auto &candidate) -> int {
      for (int i = 0; i < candidates.size(); ++i)
        if (candidates[i] == candidate) return i;
      return -1;
    };

    bool selectionChanged = false;

    const ImGuiComboFlags flags = ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_WidthFitPreview;
    /// primary (scene context)
    auto sceneLabels = zs_resources().get_scene_context_labels();
    // find current selected
    int idx = searchIndex(sceneLabels, sceneEditor->sceneRenderData.sceneLabel);
    if (ImGui::BeginCombo("##scene_context_selection", (const char *)ICON_MD_ARROW_FORWARD,
                          flags)) {
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
      for (int i = 0; i < sceneLabels.size(); ++i) {
        const bool selected = idx == i;
        if (ImGui::Selectable(sceneLabels[i].c_str(), selected)) {
          idx = i;
          sceneEditor->sceneRenderData.sceneLabel = sceneLabels[idx];
          sceneEditor->sceneRenderData.path.resize(0);

          selectionChanged = true;  // *
        }
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::PopStyleVar(1);
      ImGui::EndCombo();
    }
    if (idx != -1) {
      ImGui::SameLine();
      bool selected = sceneEditor->sceneRenderData.selectedObj == -1;
      if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 1.f, 0, 1.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2);
      }
      if (ImGui::Button(sceneLabels[idx].c_str())) {
        sceneEditor->sceneRenderData.selectedObj = -1;
        selectionChanged = true;  // *
        // sceneRenderData.path.resize(0);
      }
      if (selected) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
      }
    }
    /// secondary (scene prims)
    if (idx != -1) {
      SceneContext &currentScene
          = zs_resources().get_scene_context(sceneEditor->sceneRenderData.sceneLabel);
      auto primLabels = currentScene.getPrimitiveLabels();
      // find current selected
      int primIdx = -1;
      if (sceneEditor->sceneRenderData.path.size() > 0)
        primIdx = searchIndex(primLabels, sceneEditor->sceneRenderData.path[0]);

      ImGui::SameLine();
      auto label = fmt::format("##scene_prim_selection_{}", 0);
      if (ImGui::BeginCombo(label.c_str(), (const char *)ICON_MD_ARROW_FORWARD, flags)) {
        for (int i = 0; i < primLabels.size(); ++i) {
          const bool selected = primIdx == i;
          if (ImGui::Selectable(primLabels[i].c_str(), selected)) {
            primIdx = i;
            sceneEditor->sceneRenderData.path.resize(1);
            sceneEditor->sceneRenderData.path[0] = primLabels[i];
            if (sceneEditor->sceneRenderData.selectedObj > 0)
              sceneEditor->sceneRenderData.selectedObj = -1;
            selectionChanged = true;  // *
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      ZsPrimitive *prim = nullptr;
      std::string parentLabel;
      if (primIdx != -1) {
        parentLabel = primLabels[primIdx];
        prim = currentScene.getPrimitive(parentLabel).lock().get();
        primLabels = prim->getChildLabels();
      }

      /// drawing l-th level, primIdx-th prim
      auto drawLevel = [&searchIndex, &selectionChanged, this](int l, std::string &parentLabel,
                                                               int &primIdx, auto &primLabels,
                                                               ZsPrimitive *&prim) {
        assert(primIdx != -1 && prim);
        ImGui::SameLine();
        auto buttonLabel = fmt::format("{}##scene_prim_popup_{}", parentLabel, l);
        // sceneRenderData.selectedObj;

        bool selected = sceneEditor->sceneRenderData.selectedObj == l;
        if (selected) {
          ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
          ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 1.f, 0, 1.f));
          ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2);
        }
        if (ImGui::Button(buttonLabel.c_str())) {
          sceneEditor->sceneRenderData.selectedObj = selected ? -1 : l;
          // sceneRenderData.path.resize(l + 1);
          selectionChanged = true;  // *
        }
        if (selected) {
          ImGui::PopStyleVar();
          ImGui::PopStyleColor(2);
        }

        // moving forward to next level, update primLabels, primIdx
        ++l;
        primIdx = -1;
        if (sceneEditor->sceneRenderData.path.size() > l)
          primIdx = searchIndex(primLabels, sceneEditor->sceneRenderData.path[l]);

        ImGui::SameLine();
        auto label = fmt::format("##scene_prim_selection_{}", l);
        if (ImGui::BeginCombo(label.c_str(), (const char *)ICON_MD_ARROW_FORWARD, flags)) {
          for (int i = 0; i < primLabels.size(); ++i) {
            const bool selected = primIdx == i;
            if (ImGui::Selectable(primLabels[i].c_str(), selected)) {
              primIdx = i;
              sceneEditor->sceneRenderData.path.resize(l + 1);
              sceneEditor->sceneRenderData.path[l] = primLabels[i];
              if (sceneEditor->sceneRenderData.selectedObj > l)
                sceneEditor->sceneRenderData.selectedObj = -1;
              selectionChanged = true;  // *
            }
            if (selected) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }

        if (primIdx != -1) {
          parentLabel = primLabels[primIdx];
          prim = prim->getChild(primIdx).lock().get();
          primLabels = prim->getChildLabels();
        } else
          prim = nullptr;
      };
      int l = 0;
      while (primIdx != -1 && l < sceneEditor->sceneRenderData.path.size())
        drawLevel(l++, parentLabel, primIdx, primLabels, prim);
    }

    if (selectionChanged)
      sceneEditor->onVisiblePrimsChanged.emit(sceneEditor->getCurrentScenePrims());
  }
  void SceneEditorWidgetComponent::paint() {
    auto &interactionMode = sceneEditor->interactionMode;

    auto &viewportFocused = sceneEditor->viewportFocused;
    auto &viewportHovered = sceneEditor->viewportHovered;
    auto &canvasLocalMousePos = sceneEditor->canvasLocalMousePos;

    auto &imguiCanvasSize = sceneEditor->imguiCanvasSize;  // scene

    auto &canvasMinCursorPos = sceneEditor->canvasMinCursorPos;
    auto &canvasMaxCursorPos = sceneEditor->canvasMaxCursorPos;

    auto &primaryScenePanelOffset = sceneEditor->primaryScenePanelOffset;
    auto &secondaryScenePanelOffset = sceneEditor->secondaryScenePanelOffset;
    auto &primaryBeingDragged = sceneEditor->primaryBeingDragged;
    auto &secondaryBeingDragged = sceneEditor->secondaryBeingDragged;

    // options & settings
    auto &enableGuizmo = sceneEditor->enableGuizmo;
    auto &enableGuizmoScale = sceneEditor->enableGuizmoScale;
    auto &enableGuizmoTrans = sceneEditor->enableGuizmoTrans;
    auto &enableGuizmoRot = sceneEditor->enableGuizmoRot;

    auto &drawTexture = sceneEditor->drawTexture;
    auto &showWireframe = sceneEditor->showWireframe;
    auto &showIndex = sceneEditor->showIndex;
    auto &showCoordinate = sceneEditor->showCoordinate;
    auto &ignoreDepthTest = sceneEditor->ignoreDepthTest;

    auto &rotSnap = sceneEditor->rotSnap;
    auto &scaleSnap = sceneEditor->scaleSnap;
    auto &transSnap = sceneEditor->transSnap;
    auto &gizmoSize = sceneEditor->gizmoSize;
    auto &guizmoUseSnap = sceneEditor->guizmoUseSnap;

    // interaction state
    auto &sceneClicked = sceneEditor->sceneClicked;

    // render data
    auto &vkCanvasExtent = sceneEditor->vkCanvasExtent;
    auto &focusPrimPtr = sceneEditor->focusPrimPtr;
    auto &sceneRenderData = sceneEditor->sceneRenderData;
    auto &sceneAttachments = sceneEditor->sceneAttachments;

    sceneEditor->viewportFocused = ImGui::IsWindowFocused();
    sceneEditor->viewportHovered = ImGui::IsWindowHovered();

    auto &io = ImGui::GetIO();
    auto &style = ImGui::GetStyle();

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSplit(layer_e::_num_layers);  // 0: scene, 1: interaction (selection, ...), 2:
                                                    // overlay config buttons/selectables

    bool modeChanged = false;

    /// layer 0
    drawList->ChannelsSetCurrent(layer_e::_scene);

    ///
    /// bar area
    ///
    // ImGui::BeginGroup();

    // ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(10, 10, 10, 160));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, g_darker_color);

    // ImGui::SetCursorPosX(ImGui::GetCursorPosX() - ImGui::GetStyle().FramePadding.x * 2);
    // ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().FramePadding.y * 2);
    ImGui::BeginChild("scene_toolbar",
                      ImVec2(ImGui::GetContentRegionAvail().x,
                             ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().FramePadding.y
                                 + ImGui::GetStyle().ItemSpacing.y),
                      ImGuiChildFlags_FrameStyle, ImGuiWindowFlags_None);

    ImGui::PopStyleColor();

    ImGuiComboFlags flags = ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_WidthFitPreview;
    int idx = interactionMode.currentMode();
    std::vector<const char *> items(interactionMode.numModes());
    for (int i = 0; i < interactionMode.numModes(); ++i) {
      items[i] = interactionMode.getIconText((input_mode_e)i);
    }
    if (ImGui::BeginCombo("##mode_selection", items[idx], flags)) {
      for (int i = 0; i < interactionMode.numModes(); ++i) {
        const bool selected = i == idx;
        if (ImGui::Selectable(items[i], selected)) {
          idx = i;
          interactionMode.turnTo((input_mode_e)i, *sceneEditor);
          modeChanged = true;
        }
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Text(interactionMode.getModeInfo((input_mode_e)idx));

    /// @brief path/address display
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    drawPath();

    ImGui::EndChild();

    ///
    /// scene area
    ///
    imguiCanvasSize = ImGui::GetContentRegionAvail();
    ImGui::SetNextItemAllowOverlap();
    ImGui::Image(
        (ImU64) reinterpret_cast<VkDescriptorSet *>(&sceneAttachments.renderedSceneColorSet),
        imguiCanvasSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImColor(0, 0, 0, 0));

    canvasMinCursorPos = ImGui::GetItemRectMin();
    canvasMaxCursorPos = ImGui::GetItemRectMax();
    ImVec2 windowPos_ = ImGui::GetWindowPos();

    auto mousePos_ = ImGui::GetMousePos();
    canvasLocalMousePos
        = zs::vec<float, 2>{mousePos_.x - canvasMinCursorPos.x, mousePos_.y - canvasMinCursorPos.y};
    canvasLocalMousePos[0] = std::clamp(canvasLocalMousePos[0], 0.f, (float)vkCanvasExtent.width);
    canvasLocalMousePos[1] = std::clamp(canvasLocalMousePos[1], 0.f, (float)vkCanvasExtent.height);

    bool imageHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
    sceneClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

    // ImGui::EndGroup();

    /// guizmo
    if (auto focusPrim = focusPrimPtr.lock();
        enableGuizmo && interactionMode.isEditMode() && focusPrim) {
      ImGuizmo::SetOrthographic(false);
      ImGuizmo::SetDrawlist();
      ImGuizmo::SetRect(windowPos_.x, windowPos_.y, imguiCanvasSize.x, imguiCanvasSize.y);
      auto projection = sceneRenderData.camera.get().matrices.perspective;
      auto view = sceneRenderData.camera.get().matrices.view;

#if 0
        auto &model = sceneRenderData.models[focusObjId];
        auto transform = model.useTransform
                             ? get_transform(model.transform)
                             : get_transform(model.translate, model.rotate, model.scale);
#else
      auto &prim = focusPrim;
      // auto &model = prim->vkTriMesh(sceneEditor->ctx());
      auto pModel = prim->queryVkTriMesh(sceneEditor->ctx(), sceneRenderData.currentTimeCode);
      if (pModel && sceneEditor->currentVisiblePrimsDrawn.at(prim.get())) {
        const auto &model = *pModel;
        // auto transform = prim->visualTransform(sceneRenderData.currentTimeCode);  // local copy
        auto transform = prim->currentTimeVisualTransform();  // local copy
#endif

        // if (guizmoUseSnap)
        constexpr float translationSnapValues[3] = {0.1f, 0.1f, 0.1f};
        constexpr float scaleSnapValues[3] = {0.5f, 0.5f, 0.5f};
        constexpr float rotationSnapValues[3] = {10.f, 10.f, 10.f};

        float snapValues[5] = {rotSnap, scaleSnap, transSnap, transSnap, transSnap};

        u32 op = 0;
        if (enableGuizmoRot) op = op | (u32)ImGuizmo::OPERATION::ROTATE;
        if (enableGuizmoScale) op = op | (u32)ImGuizmo::OPERATION::SCALE;
        if (enableGuizmoTrans) op = op | (u32)ImGuizmo::OPERATION::TRANSLATE;
        if (op) {
          ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
                               static_cast<ImGuizmo::OPERATION>(op), ImGuizmo::LOCAL,
                               glm::value_ptr(transform), nullptr,
                               guizmoUseSnap ? snapValues : nullptr);
        }

        if (ImGuizmo::IsUsing()) {
          glm::vec3 scale;
          glm::quat rotation;
          glm::vec3 translation;
          glm::vec3 skew;
          glm::vec4 perspective;
          glm::decompose(transform, scale, rotation, translation, skew, perspective);
          rotation = glm::conjugate(rotation);
          auto rot = glm::eulerAngles(rotation);
          rot = -rot;

#if 0
          if (!model.useTransform) {
            auto tozsvec = [](const glm::vec3 &v) { return vec<float, 3>{v[0], v[1], v[2]}; };
            model.translate = tozsvec(translation);
            model.rotate = tozsvec(rot);
            model.scale = tozsvec(scale);
          } else {
            static_assert(sizeof(model.transform) == sizeof(transform),
                          "transform should be 4x4 floats");
            std::memcpy(&model.transform, &transform, sizeof(float) * 16);
          }
#else
          prim->details().setTransform(
              glm::inverse(prim->details().toNativeCoordTransform()
                           * prim->parentTransform(sceneRenderData.currentTimeCode))
              * transform);
          // std::memcpy(&prim->transform(), &transform, sizeof(transform));
#endif
        }
      }
    }

    /// overlay
    {
      drawList->ChannelsSetCurrent(layer_e::_config);

      /// coord gizmo
      const glm::vec4 xAxis{1, 0, 0, 1}, yAxis{0, 1, 0, 1}, zAxis{0, 0, 1, 1};

      // view
      auto &sceneCam = sceneRenderData.camera.get();
      glm::mat4 view = glm::lookAt(glm::vec3(0), sceneCam.getCameraFront(), sceneCam._roughUp());
      // proj
      glm::mat4 proj = glm::ortho(-2.f, 2.f, -2.f, 2.f, -2.f, 2.f);

      ImVec2 center{canvasMaxCursorPos.x - style.FramePadding.x * 2 - gizmoSize / 2 - 8,
                    canvasMinCursorPos.y + style.FramePadding.y * 2 + gizmoSize / 2 + 8};
      auto pv = proj * view;
      // draw to screen (not sure why -(neg) is required here)
      glm::vec4 tips[3] = {-pv * xAxis, -pv * yAxis, -pv * zAxis};
      std::array<int, 3> inds{0, 1, 2};  // fron to back
      if (tips[inds[0]].z < tips[inds[1]].z) zs_swap(inds[0], inds[1]);
      if (tips[inds[1]].z < tips[inds[2]].z) zs_swap(inds[1], inds[2]);
      if (tips[inds[0]].z < tips[inds[1]].z) zs_swap(inds[0], inds[1]);

      // fmt::print("z(012): {}, {}, {}\n", tips[0][2], tips[1][2], tips[2][2]);
      for (int d = 0; d < 3; ++d) {
        auto id = inds[d];
        auto tip = tips[id];
        auto clr = IM_COL32(0, 0, 0, 255);
        clr |= 255 << (id * 8);
        if (tip[2] < -0.5) {
          drawList->AddCircle(center - ImVec2(tip[0], tip[1]) * gizmoSize, gizmoSize / 8, clr);
        } else {
          drawList->AddLine(center, center + ImVec2(tip[0], tip[1]) * gizmoSize, clr, 8.f);
          drawList->AddCircleFilled(center + ImVec2(tip[0], tip[1]) * gizmoSize, gizmoSize / 8,
                                    clr);
        }
      }
      for (int d = 0; d < 3; ++d) {
        auto id = inds[d];
        auto tip = tips[id];
        if (tip[2] >= -0.5) continue;
        auto clr = IM_COL32(0, 0, 0, 255);
        clr |= 255 << (id * 8);
        drawList->AddLine(center, center + ImVec2(tip[0], tip[1]) * gizmoSize, clr, 8.f);
      }
      drawList->AddCircleFilled(center, 8.f / 2, IM_COL32_WHITE);
      for (int d = 0; d < 3; ++d) {
        auto id = inds[d];
        auto tip = tips[id];
        auto clr = IM_COL32(0, 0, 0, 255);
        clr |= 255 << (id * 8);
        if (tip[2] >= -0.5) {
          drawList->AddCircle(center - ImVec2(tip[0], tip[1]) * gizmoSize, gizmoSize / 8, clr);
        } else {
          drawList->AddCircleFilled(center + ImVec2(tip[0], tip[1]) * gizmoSize, gizmoSize / 8,
                                    clr);
        }
      }

      /// PRIMARY PANEL : mode selection (top right)
      int currentSelection = interactionMode.currentMode();
      {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.8f);
        ImGui::SetWindowFontScale(2.f);

        // primary options
        ImVec2 iconCursorPos{canvasMaxCursorPos.x - style.FramePadding.x * 2,
                             canvasMinCursorPos.y + gizmoSize + 8 + style.FramePadding.y * 2};
        iconCursorPos += primaryScenePanelOffset;

        auto sz = ImGui::CalcTextSize(interactionMode.getIconText((input_mode_e)0));
        ImGui::SetCursorScreenPos(ImVec2{iconCursorPos.x - sz.x, iconCursorPos.y});

        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
        for (int i = 0; i < interactionMode.numModes(); ++i) {
          const char *iconText = interactionMode.getIconText((input_mode_e)i);
          auto sz = ImGui::CalcTextSize(iconText);
          if (ImGui::Selectable(iconText, currentSelection == i, 0, sz)) {
            if (!modeChanged) {
              interactionMode.turnTo(static_cast<input_mode_e>(i), *sceneEditor);
              modeChanged = true;
            }
          }
          if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_DelayNone)) {
            ImGui::SetTooltip(interactionMode.getModeInfo(static_cast<input_mode_e>(i)));
          }
        }
        ImGui::PopStyleVar();
        ImGui::EndGroup();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone)) {
          imageHovered = false;
          if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) primaryBeingDragged = true;
        }
        if (primaryBeingDragged) {
          if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            auto delta = io.MouseDelta;
            if (delta.x + ImGui::GetItemRectMax().x > canvasMaxCursorPos.x)
              delta.x = canvasMaxCursorPos.x - ImGui::GetItemRectMax().x;
            if (delta.x + ImGui::GetItemRectMin().x < canvasMinCursorPos.x)
              delta.x = canvasMinCursorPos.x - ImGui::GetItemRectMin().x;
            if (delta.y + ImGui::GetItemRectMax().y > canvasMaxCursorPos.y)
              delta.y = canvasMaxCursorPos.y - ImGui::GetItemRectMax().y;
            if (delta.y + ImGui::GetItemRectMin().y < canvasMinCursorPos.y)
              delta.y = canvasMinCursorPos.y - ImGui::GetItemRectMin().y;
            primaryScenePanelOffset += delta;

            drawList->AddRect(ImGui::GetItemRectMin() - style.ItemSpacing / 2,
                              ImGui::GetItemRectMax(), IM_COL32(255, 255, 0, 255));
            // primaryScenePanelOffset = io.MousePos -
            // io.MouseClickedPos[ImGuiMouseButton_Middle];
          }
          if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
            primaryBeingDragged = false;
          }
        }

        if (viewportFocused && !modeChanged) {
          if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) {
            int numIndexPressed = 0;
            ImGuiKey id;
            int pressedId = 0;
            for (int i = 0; i < 9 && i < interactionMode.numModes(); ++i) {
              id = static_cast<ImGuiKey>((int)ImGuiKey_0 + i + 1);
              if (ImGui::IsKeyPressed(id)) {
                pressedId = i;
                if (numIndexPressed++ > 0) break;
              }
            }
            if (numIndexPressed == 1) {
              interactionMode.turnTo(static_cast<input_mode_e>(pressedId), *sceneEditor);
              modeChanged = true;
            }
          };
        }
        // secondary options
        currentSelection = interactionMode.currentMode();
        ;

        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleVar();
      }
      /// SECONDARY PANEL : visualization options (top left)
      {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.8f);
        ImGui::SetWindowFontScale(2.f);

        // primary options
        ImVec2 iconCursorPos{canvasMinCursorPos.x + style.FramePadding.x * 2,
                             canvasMinCursorPos.y + style.FramePadding.y * 2};
        auto drawOption = [&iconCursorPos](const char *label, bool &enable,
                                           const char *hint = nullptr) {
          auto sz = ImGui::CalcTextSize(label);
          if (ImGui::Selectable(label, enable, 0, sz)) {
            enable ^= 1;
          }
          if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_DelayNone)) {
            if (hint) ImGui::SetTooltip(hint);
          }
        };

        ImGui::SetCursorScreenPos(iconCursorPos + secondaryScenePanelOffset);
        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
        drawOption(ICON_MD_PICTURE_IN_PICTURE, drawTexture, (const char *)u8"显示纹理");
        drawOption(ICON_MD_TEXTURE, showWireframe, (const char *)u8"显示线框");
        drawOption(ICON_MD_PIN, showIndex, (const char *)u8"显示索引");  // 123
        drawOption(ICON_MS_BACKGROUND_GRID_SMALL, showCoordinate, (const char *)u8"显示坐标系");
        drawOption(ICON_MS_TRANSITION_FADE, ignoreDepthTest, (const char *)u8"穿透拾取");
        drawOption(ICON_MS_DRAG_PAN, enableGuizmo, (const char *)u8"仿射转换控制");

        ImGui::PopStyleVar();
        ImGui::EndGroup();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone)) {
          imageHovered = false;
          if (!primaryBeingDragged && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            secondaryBeingDragged = true;
        }
        if (secondaryBeingDragged) {
          if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            auto delta = io.MouseDelta;
            if (delta.x + ImGui::GetItemRectMax().x > canvasMaxCursorPos.x)
              delta.x = canvasMaxCursorPos.x - ImGui::GetItemRectMax().x;
            if (delta.x + ImGui::GetItemRectMin().x < canvasMinCursorPos.x)
              delta.x = canvasMinCursorPos.x - ImGui::GetItemRectMin().x;
            if (delta.y + ImGui::GetItemRectMax().y > canvasMaxCursorPos.y)
              delta.y = canvasMaxCursorPos.y - ImGui::GetItemRectMax().y;
            if (delta.y + ImGui::GetItemRectMin().y < canvasMinCursorPos.y)
              delta.y = canvasMinCursorPos.y - ImGui::GetItemRectMin().y;
            secondaryScenePanelOffset += delta;

            drawList->AddRect(ImGui::GetItemRectMin() - style.ItemSpacing / 2,
                              ImGui::GetItemRectMax(), IM_COL32(255, 255, 0, 255));
            // secondaryScenePanelOffset = io.MousePos -
            // io.MouseClickedPos[ImGuiMouseButton_Middle];
          }
          if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
            secondaryBeingDragged = false;
          }
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
          ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
          secondaryScenePanelOffset += io.MouseDelta;
        }

        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleVar();
      }

      // visualization options (top left)
      {
        ImGui::SetWindowFontScale(1.2f);

        // primary options
        ImVec2 iconCursorPos{canvasMinCursorPos.x + style.FramePadding.x * 2,
                             canvasMaxCursorPos.y - style.FramePadding.y * 2};
        auto drawInfo = [&iconCursorPos, &style](const char *label) {
          auto sz = ImGui::CalcTextSize(label);
          iconCursorPos.y -= sz.y + style.ItemSpacing.y;
          ImGui::SetCursorScreenPos(iconCursorPos);
          ImGui::Text(label);
        };
        drawInfo(fmt::format("hovered obj label: {}, id: {}", sceneEditor->getHoveredPrimLabel(),
                             sceneEditor->getHoveredPrimId())
                     .c_str());
        drawInfo(fmt::format("focus obj label: {}, id: {}", sceneEditor->getFocusPrimLabel(),
                             sceneEditor->getFocusPrimId())
                     .c_str());

        ImGui::SetWindowFontScale(1.f);
      }

      // header (top middle)
      {
        ImGui::SetWindowFontScale(1.5f);
        auto iconCursorPos = ImVec2{(canvasMaxCursorPos.x + canvasMinCursorPos.x) / 2,
                                    canvasMinCursorPos.y + style.FramePadding.y * 2};
        auto msg
            = fmt::format(fmt::runtime((const char *)u8"模式: {}\n"),
                          interactionMode.getModeInfo(static_cast<input_mode_e>(currentSelection)));
        auto sz = ImGui::CalcTextSize(msg.c_str()).x;
        drawList->AddText(iconCursorPos - ImVec2{sz / 2, 0}, IM_COL32_WHITE, msg.c_str());
        ImGui::SetWindowFontScale(1.f);
      }

      // text overlay (bottom right)
      {
        ImGui::SetWindowFontScale(1.2f);
        auto msg = fmt::format("viewport mouse pos: {}, {}\n", canvasLocalMousePos[0],
                               canvasLocalMousePos[1]);
        auto iconCursorPos = canvasMaxCursorPos - style.FramePadding * 2;
        drawList->AddText(iconCursorPos - ImGui::CalcTextSize(msg.c_str()), IM_COL32_WHITE,
                          msg.c_str());

        iconCursorPos.y -= ImGui::CalcTextSize(msg.c_str()).y;
        msg = fmt::format("use (L)Ctrl + index to switch interaction mode\n");
        drawList->AddText(iconCursorPos - ImGui::CalcTextSize(msg.c_str()), IM_COL32_WHITE,
                          msg.c_str());

        iconCursorPos.y -= ImGui::CalcTextSize(msg.c_str()).y;
        msg = fmt::format("rendering objects: {}\n", sceneEditor->numFrameRenderModels);
        drawList->AddText(iconCursorPos - ImGui::CalcTextSize(msg.c_str()), IM_COL32_WHITE,
                          msg.c_str());

        iconCursorPos.y -= ImGui::CalcTextSize(msg.c_str()).y;
        msg = fmt::format("frame per second: {}\n", sceneEditor->framePerSecond);
        drawList->AddText(iconCursorPos - ImGui::CalcTextSize(msg.c_str()), IM_COL32_WHITE,
                          msg.c_str());
        ImGui::SetWindowFontScale(1.f);
      }
    }

    /// upper layers
    sceneEditor->sceneHovered = imageHovered;
    interactionMode.paint();

    drawList->ChannelsMerge();
  }
  bool SceneEditorWidgetComponent::onEvent(GuiEvent *e) {
    if (sceneEditor->_camCtrl.onEvent(e)) return e->isAccepted();
    return false;
  }

}  // namespace zs