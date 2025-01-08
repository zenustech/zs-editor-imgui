#include "SceneEditor.hpp"
#include "editor/widgets/WidgetDrawUtilities.hpp"
#include "imgui_internal.h"
//
#include "IconsMaterialSymbols.h"
#include "editor/widgets/WidgetComponent.hpp"
#include "imgui.h"
//
#include "zensim/ZpcFunctional.hpp"
//
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/transform.hpp>

#include "ImGuizmo.h"

namespace zs {

  void CameraControl::setKeyState(int idx, int state) noexcept {
    assert(idx >= 0 && idx < num_directions);
    _keyStates[idx] = state;
  }
  int CameraControl::findDirectionIndex(ImGuiKey key) const noexcept {
    for (int idx = 0; idx < num_directions; ++idx)
      if (_keyBindings[idx] == key) return idx;
    return -1;
  }
  int CameraControl::findMouseAction(ImGuiMouseButton mouse) const noexcept {
    for (int idx = 0; idx < num_mouse_bindings; ++idx)
      if (_mouseBindings[idx] == mouse) return idx;
    return -1;
  }

  static StateMachine camera_control_statemachine(CameraControl &cameraCtrl) {
    int activeMouseButton = 0;  // ImGuiMouseButton
    int mouseAction = -1;
    for (;;) {
      auto e = co_await zs::Event<KeyPressEvent *, KeyReleaseEvent *, MousePressEvent *,
                                  MouseReleaseEvent *, MouseMoveEvent *>{};
      match(
          [&cameraCtrl](KeyPressEvent *e) {
            auto idx = cameraCtrl.findDirectionIndex(e->key());
            cameraCtrl.setKeyState(idx, 1);
          },
          [&cameraCtrl](KeyReleaseEvent *e) {
            auto idx = cameraCtrl.findDirectionIndex(e->key());
            cameraCtrl.setKeyState(idx, 0);
          },
          [&cameraCtrl, &mouseAction, &activeMouseButton](MousePressEvent *e) {
            auto idx = cameraCtrl.findMouseAction(e->button());
            if (idx != -1) {
              activeMouseButton = e->button();
              mouseAction = idx;
            }
          },
          [&cameraCtrl, &mouseAction, &activeMouseButton](MouseReleaseEvent *e) {
            if (activeMouseButton == e->button()) {
              activeMouseButton = -1;
              mouseAction = -1;
            }
          },
          [&cameraCtrl, &mouseAction](MouseMoveEvent *e) {
            Camera &camera = *cameraCtrl._cam;
            auto delta = e->getDelta();
            switch (mouseAction) {
              case CameraControl::mouse_action_e::rotate: {
                camera.yaw(delta.x * cameraCtrl._rotationSpeed);
                camera.pitch(-delta.y * cameraCtrl._rotationSpeed);
                // camera.updateViewMatrix();
                // editor.sceneAugmentRenderer.overlayTextNeedUpdate = true;
                break;
              }
              case CameraControl::mouse_action_e::translate_side: {
                camera.translateHorizontal(-delta.x * cameraCtrl._sideTranslationSpeed);
                camera.translateVertical(delta.y * cameraCtrl._sideTranslationSpeed);
                break;
              }
              case CameraControl::mouse_action_e::translate_advance: {
                camera.translateForward(-delta.x * cameraCtrl._advanceTranslationSpeed);
                camera.translateHorizontal(delta.y * cameraCtrl._advanceTranslationSpeed);
                break;
              }
              default:;
            }
          })(e);
    }
  }
  void CameraControl::trackCamera(Camera &camera) {
    _cam = zs::addressof(camera);
    _cameraState = camera_control_statemachine(*this);  // reset state
  }
  ActionWidgetComponent get_widget(Camera &camera, void *sceneEditor_) {
    SceneEditor *sceneEditor = static_cast<SceneEditor *>(sceneEditor_);
    return [&camera, zclip = zs::vec<float, 2>{camera.getNearClip(), camera.getFarClip()},
            fov = (float)camera.getFov(), aspect = (float)camera.getAspect(),
            position = camera.getPosition(), rotation = camera.getRotation(), posUnit = 1.f,
            rotUnit = 1.f, flipY = camera.getFlipY(), sceneEditor]() mutable {
      if (camera.isUpdated(true)) {
        fov = camera.getFov();
        zclip[0] = camera.getNearClip();
        zclip[1] = camera.getFarClip();
        aspect = camera.getAspect();
        position = camera.getPosition();
        rotation = camera.getRotation();
        flipY = camera.getFlipY();
      }
      ImGui::SeparatorText("Camera");

      ImGui::AlignTextToFramePadding();
      ImGui::Text("flip Y");
      ImGui::SameLine();
      if (ImGui::Checkbox("checkbox", &flipY)) {
        camera.setFlipY(flipY);
      }
      bool changed = false;
      changed |= ImGui::SliderFloat("fov", &fov, 0.f, 180.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      changed |= ImGui::DragFloat2("clip", zclip.data(), 1.f, 0.f, FLT_MAX, "%.3f",
                                   ImGuiSliderFlags_AlwaysClamp);
      if (ImGui::Button("Reset")) {
        fov = 45.f;
        zclip[0] = 0.0001f;
        zclip[1] = 2048.f;
        changed = true;
      }
      if (changed) {
        camera.setPerspective(fov, aspect, zclip[0], zclip[1]);
      }

      ImGui::DragFloat("unit_offset", &posUnit, 0.1f, 0.1f, 10.f, "%.3f", 0);
      if (ImGui::DragFloat3("position", (float *)&position, posUnit, -FLT_MAX, FLT_MAX, "%.3f", 0))
        camera.setPosition(position);
      ImGui::DragFloat("unit_degree", &rotUnit, 0.1f, 0.1f, 10.f, "%.3f", 0);
      if (ImGui::DragFloat3("rotation", (float *)&rotation, rotUnit, -FLT_MAX, FLT_MAX, "%.3f", 0))
        camera.setRotation(rotation);
      ImGui::Separator();
      auto dir = camera.getCameraRayDirection(sceneEditor->viewportMousePos[0],
                                              sceneEditor->viewportMousePos[1],
                                              (float)sceneEditor->viewportPanelSize.width,
                                              (float)sceneEditor->viewportPanelSize.height);
      auto dirText
          = fmt::format("mouse-pointing camera ray: \n{}, {}, {}\n", dir[0], dir[1], dir[2]);
      auto wrapWidth = ImGui::GetContentRegionAvail().x;
      ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrapWidth);
      ImGui::TextUnformatted(dirText.c_str());
      ImGui::Text("camera location: %.3f, %.3f, %.3f", camera.position.x, camera.position.y,
                  camera.position.z);
      ImGui::Text("mouse hovered prim hit point: %.3f, %.3f, %.3f", sceneEditor->hoveredHitPt[0],
                  sceneEditor->hoveredHitPt[1], sceneEditor->hoveredHitPt[2]);
      if (sceneEditor->hoveredHitPt[0] != detail::deduce_numeric_infinity<f32>()) {
        auto xy = camera.getScreenPoint(sceneEditor->hoveredHitPt,
                                        (float)sceneEditor->viewportPanelSize.width,
                                        (float)sceneEditor->viewportPanelSize.height);
        ImGui::Text("(validation) hit screen point: %.3f, %.3f", xy.x, xy.y);
      }
      ImGui::PopTextWrapPos();
    };
  }

  void SceneEditor::SceneEditorRoamingMode::update(float dt) {
    /// camera
    ImGuiIO &io = ImGui::GetIO();
    bool needUpdate = false;
    auto &camera = editor.sceneRenderData.camera.get();
    float intensity = 10.0f;
    /// key
    if (ImGui::IsKeyDown(ImGuiKey_W)) {
      camera.translateForward(dt * intensity);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_S)) {
      camera.translateForward(-dt * intensity);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_A)) {
      camera.translateHorizontal(-dt * intensity);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_D)) {
      camera.translateHorizontal(dt * intensity);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_C)) {
      camera.translateVertical(-dt * intensity);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_V) || ImGui::IsKeyDown(ImGuiKey_Space)) {
      camera.translateVertical(dt * intensity);
      needUpdate = true;
    }

    if (ImGui::IsKeyDown(ImGuiKey_Q)) {
      camera.yaw(-dt * 100);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_E)) {
      camera.yaw(dt * 100);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_R)) {
      camera.pitch(dt * 100);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_F)) {
      camera.pitch(-dt * 100);
      needUpdate = true;
    }
    if (!ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
      bool moved = io.MouseDelta.x != 0 || io.MouseDelta.y != 0;
      /// mouse
      if (ImGui::IsMouseDown(0)) {  // left
        camera.yaw(io.MouseDelta.x * 0.1);
        camera.pitch(-io.MouseDelta.y * 0.1);
        if (moved) needUpdate = true;
      }
      if (ImGui::IsMouseDown(1)) {  // right
        camera.translateHorizontal(-io.MouseDelta.x * 0.01);
        camera.translateVertical(io.MouseDelta.y * 0.01);
        if (moved) needUpdate = true;
      }
      if (ImGui::IsMouseDown(2)) {  // middle
        camera.translateForward(-io.MouseDelta.y * 0.02);
        camera.translateHorizontal(io.MouseDelta.x * 0.02);
        if (moved) needUpdate = true;
      }
    }
    // if (io.MouseWheel != 0.f)
    //   camera.translateForward(io.MouseWheel * 0.6);
    ///
    if (needUpdate) {
      camera.updateViewMatrix();
      editor.sceneAugmentRenderer.overlayTextNeedUpdate = true;
    }
  }

  void SceneEditor::SceneEditorRoamingMode::paint() {}

  void SceneEditor::SceneEditorSelectionMode::update(float dt) {
    /// camera
    ImGuiIO &io = ImGui::GetIO();
    bool needUpdate = false;
    auto &camera = editor.sceneRenderData.camera.get();
    /// key
    if (ImGui::IsKeyDown(ImGuiKey_W)) {
      camera.translateForward(dt * 1.5);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_S)) {
      camera.translateForward(-dt * 1.5);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_A)) {
      camera.translateHorizontal(-dt * 1.5);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_D)) {
      camera.translateHorizontal(dt * 1.5);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_C)) {
      camera.translateVertical(-dt * 1.5);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_V) || ImGui::IsKeyDown(ImGuiKey_Space)) {
      camera.translateVertical(dt * 1.5);
      needUpdate = true;
    }

    if (ImGui::IsKeyDown(ImGuiKey_Q)) {
      camera.yaw(-dt * 100);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_E)) {
      camera.yaw(dt * 100);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_R)) {
      camera.pitch(dt * 100);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_F)) {
      camera.pitch(-dt * 100);
      needUpdate = true;
    }
    if (!ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
      ;
    }
    if (needUpdate) {
      camera.updateViewMatrix();
      editor.sceneAugmentRenderer.overlayTextNeedUpdate = true;
    }
  }
  void SceneEditor::SceneEditorSelectionMode::paint() {
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSetCurrent(_interaction);

    /// selection
    if (editor.sceneClicked) editor.selectionStart = ImGui::GetMousePos();
    if (editor.selectionStart.has_value() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      // if (ImGui::IsItemHovered()) selectionEnd = ImGui::GetMousePos();
      editor.selectionEnd = ImGui::GetMousePos();
      (*editor.selectionEnd).x = std::clamp((*editor.selectionEnd).x, editor.viewportMinScreenPos.x,
                                            editor.viewportMaxScreenPos.x);
      (*editor.selectionEnd).y = std::clamp((*editor.selectionEnd).y, editor.viewportMinScreenPos.y,
                                            editor.viewportMaxScreenPos.y);

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
          = mi - glm::uvec2{editor.viewportMinScreenPos.x, editor.viewportMinScreenPos.y};

      editor.selectionStart = editor.selectionEnd = {};
    }

    ImGui::SetCursorScreenPos(editor.viewportMinScreenPos);
  }

  /// paint
  void SceneEditor::SceneEditorPaintMode::update(float dt) {
    /// camera
    ImGuiIO &io = ImGui::GetIO();
    bool needUpdate = false;
    auto &camera = editor.sceneRenderData.camera.get();
    /// key
    if (ImGui::IsKeyDown(ImGuiKey_W)) {
      camera.translateForward(dt * 1.5);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_S)) {
      camera.translateForward(-dt * 1.5);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_A)) {
      camera.translateHorizontal(-dt * 1.5);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_D)) {
      camera.translateHorizontal(dt * 1.5);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_C)) {
      camera.translateVertical(-dt * 1.5);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_V) || ImGui::IsKeyDown(ImGuiKey_Space)) {
      camera.translateVertical(dt * 1.5);
      needUpdate = true;
    }

    if (ImGui::IsKeyDown(ImGuiKey_Q)) {
      camera.yaw(-dt * 100);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_E)) {
      camera.yaw(dt * 100);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_R)) {
      camera.pitch(dt * 100);
      needUpdate = true;
    }
    if (ImGui::IsKeyDown(ImGuiKey_F)) {
      camera.pitch(-dt * 100);
      needUpdate = true;
    }
    if (!ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
      ;
    }
    if (needUpdate) {
      camera.updateViewMatrix();
      editor.sceneAugmentRenderer.overlayTextNeedUpdate = true;
    }
  }
  void SceneEditor::SceneEditorPaintMode::paint() {
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSetCurrent(_interaction);

    /// selection
    if (editor.sceneHovered) {
      editor.paintRadius
          += ImGui::GetIO().MouseWheel * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 5.f : 1.f);
      if (editor.paintRadius <= 1.f + detail::deduce_numeric_epsilon<float>() * 10)

        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
      if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        editor.paintCenter = glm::uvec2{ImGui::GetMousePos().x - editor.viewportMinScreenPos.x,
                                        ImGui::GetMousePos().y - editor.viewportMinScreenPos.y};
        drawList->AddCircle(ImGui::GetMousePos(), editor.paintRadius, IM_COL32_WHITE, 0, 2.f);
      } else {
        drawList->AddCircle(ImGui::GetMousePos(), editor.paintRadius, IM_COL32_WHITE, 0, 1.f);
      }
    }
  }

  ///
  /// final widget
  ///
  void SceneEditor::drawPath() {
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
    int idx = searchIndex(sceneLabels, sceneRenderData.sceneLabel);
    if (ImGui::BeginCombo("##scene_context_selection", (const char *)ICON_MD_ARROW_FORWARD,
                          flags)) {
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
      for (int i = 0; i < sceneLabels.size(); ++i) {
        const bool selected = idx == i;
        if (ImGui::Selectable(sceneLabels[i].c_str(), selected)) {
          idx = i;
          sceneRenderData.sceneLabel = sceneLabels[idx];
          sceneRenderData.path.resize(0);

          selectionChanged = true;  // *
        }
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::PopStyleVar(1);
      ImGui::EndCombo();
    }
    if (idx != -1) {
      ImGui::SameLine();
      bool selected = sceneRenderData.selectedObj == -1;
      if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 1.f, 0, 1.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2);
      }
      if (ImGui::Button(sceneLabels[idx].c_str())) {
        sceneRenderData.selectedObj = -1;
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
      SceneContext &currentScene = zs_resources().get_scene_context(sceneRenderData.sceneLabel);
      auto primLabels = currentScene.getPrimitiveLabels();
      // find current selected
      int primIdx = -1;
      if (sceneRenderData.path.size() > 0)
        primIdx = searchIndex(primLabels, sceneRenderData.path[0]);

      ImGui::SameLine();
      auto label = fmt::format("##scene_prim_selection_{}", 0);
      if (ImGui::BeginCombo(label.c_str(), (const char *)ICON_MD_ARROW_FORWARD, flags)) {
        for (int i = 0; i < primLabels.size(); ++i) {
          const bool selected = primIdx == i;
          if (ImGui::Selectable(primLabels[i].c_str(), selected)) {
            primIdx = i;
            sceneRenderData.path.resize(1);
            sceneRenderData.path[0] = primLabels[i];
            if (sceneRenderData.selectedObj > 0) sceneRenderData.selectedObj = -1;
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

        bool selected = sceneRenderData.selectedObj == l;
        if (selected) {
          ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
          ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 1.f, 0, 1.f));
          ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2);
        }
        if (ImGui::Button(buttonLabel.c_str())) {
          sceneRenderData.selectedObj = selected ? -1 : l;
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
        if (sceneRenderData.path.size() > l)
          primIdx = searchIndex(primLabels, sceneRenderData.path[l]);

        ImGui::SameLine();
        auto label = fmt::format("##scene_prim_selection_{}", l);
        if (ImGui::BeginCombo(label.c_str(), (const char *)ICON_MD_ARROW_FORWARD, flags)) {
          for (int i = 0; i < primLabels.size(); ++i) {
            const bool selected = primIdx == i;
            if (ImGui::Selectable(primLabels[i].c_str(), selected)) {
              primIdx = i;
              sceneRenderData.path.resize(l + 1);
              sceneRenderData.path[l] = primLabels[i];
              if (sceneRenderData.selectedObj > l) sceneRenderData.selectedObj = -1;
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
      while (primIdx != -1 && l < sceneRenderData.path.size())
        drawLevel(l++, parentLabel, primIdx, primLabels, prim);
    }

    if (selectionChanged) onVisiblePrimsChanged.emit(getCurrentScenePrims());
  }
  ActionWidgetComponent SceneEditor::getWidget() {
    return ActionWidgetComponent{[this]() {
      viewportFocused = ImGui::IsWindowFocused();
      viewportHovered = ImGui::IsWindowHovered();

      auto &io = ImGui::GetIO();
      auto &style = ImGui::GetStyle();

      ImDrawList *drawList = ImGui::GetWindowDrawList();
      drawList->ChannelsSplit(_num_layers);  // 0: scene, 1: interaction (selection, ...), 2:
                                             // overlay config buttons/selectables

      bool modeChanged = false;

      /// layer 0
      drawList->ChannelsSetCurrent(_scene);

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
      int idx = inputMode.currentMode();
      std::vector<const char *> items(inputMode.numModes());
      for (int i = 0; i < inputMode.numModes(); ++i) {
        items[i] = inputMode.getIconText((input_mode_e)i);
      }
      if (ImGui::BeginCombo("##mode_selection", items[idx], flags)) {
        for (int i = 0; i < inputMode.numModes(); ++i) {
          const bool selected = i == idx;
          if (ImGui::Selectable(items[i], selected)) {
            idx = i;
            inputMode.turnTo((input_mode_e)i, *this);
            modeChanged = true;
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      ImGui::SameLine();
      ImGui::Text(inputMode.getModeInfo((input_mode_e)idx));

      /// @brief path/address display
      ImGui::SameLine();
      ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
      ImGui::SameLine();

      drawPath();

      ImGui::EndChild();

      ///
      /// scene area
      ///
      vpPanelSize = ImGui::GetContentRegionAvail();
      ImGui::SetNextItemAllowOverlap();
      ImGui::Image(
          (ImU64) reinterpret_cast<VkDescriptorSet *>(&sceneAttachments.renderedSceneColorSet),
          vpPanelSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImColor(0, 0, 0, 0));

      viewportMinScreenPos = ImGui::GetItemRectMin();
      viewportMaxScreenPos = ImGui::GetItemRectMax();
      ImVec2 windowPos_ = ImGui::GetWindowPos();

      auto mousePos_ = ImGui::GetMousePos();
      viewportMousePos = zs::vec<float, 2>{mousePos_.x - viewportMinScreenPos.x,
                                           mousePos_.y - viewportMinScreenPos.y};
      viewportMousePos[0] = std::clamp(viewportMousePos[0], 0.f, (float)viewportPanelSize.width);
      viewportMousePos[1] = std::clamp(viewportMousePos[1], 0.f, (float)viewportPanelSize.height);

      bool imageHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
      sceneClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

      // ImGui::EndGroup();

      /// guizmo
      if (auto focusPrim = focusPrimPtr.lock();
          enableGuizmo && inputMode.isEditMode() && focusPrim) {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(windowPos_.x, windowPos_.y, vpPanelSize.x, vpPanelSize.y);
        auto projection = sceneRenderData.camera.get().matrices.perspective;
        auto view = sceneRenderData.camera.get().matrices.view;

#if 0
        auto &model = sceneRenderData.models[focusObjId];
        auto transform = model.useTransform
                             ? get_transform(model.transform)
                             : get_transform(model.translate, model.rotate, model.scale);
#else
        auto &prim = focusPrim;
        // auto &model = prim->vkTriMesh(this->ctx());
        auto pModel = prim->queryVkTriMesh(this->ctx(), sceneRenderData.currentTimeCode);
        if (pModel && currentVisiblePrimsDrawn.at(prim.get())) {
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
        drawList->ChannelsSetCurrent(_config);

        // coord gizmo
        const glm::vec4 xAxis{1, 0, 0, 1}, yAxis{0, 1, 0, 1}, zAxis{0, 0, 1, 1};

        // view
        auto &sceneCam = sceneRenderData.camera.get();
        glm::mat4 view = glm::lookAt(glm::vec3(0), sceneCam.getCameraFront(), sceneCam._roughUp());
        // proj
        glm::mat4 proj = glm::ortho(-2.f, 2.f, -2.f, 2.f, -2.f, 2.f);

        ImVec2 center{viewportMaxScreenPos.x - style.FramePadding.x * 2 - gizmoSize / 2 - 8,
                      viewportMinScreenPos.y + style.FramePadding.y * 2 + gizmoSize / 2 + 8};
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

        // mode selection (top right)
        int currentSelection = inputMode.currentMode();
        {
          ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.8f);
          ImGui::SetWindowFontScale(2.f);

          // primary options
          ImVec2 iconCursorPos{viewportMaxScreenPos.x - style.FramePadding.x * 2,
                               viewportMinScreenPos.y + gizmoSize + 8 + style.FramePadding.y * 2};
          iconCursorPos += primaryScenePanelOffset;

          auto sz = ImGui::CalcTextSize(inputMode.getIconText((input_mode_e)0));
          ImGui::SetCursorScreenPos(ImVec2{iconCursorPos.x - sz.x, iconCursorPos.y});
          ImGui::BeginGroup();
          ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
          for (int i = 0; i < inputMode.numModes(); ++i) {
            const char *iconText = inputMode.getIconText((input_mode_e)i);
            auto sz = ImGui::CalcTextSize(iconText);
            if (ImGui::Selectable(iconText, currentSelection == i, 0, sz)) {
              if (!modeChanged) {
                inputMode.turnTo(static_cast<input_mode_e>(i), *this);
                modeChanged = true;
              }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_DelayNone)) {
              ImGui::SetTooltip(inputMode.getModeInfo(static_cast<input_mode_e>(i)));
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
              if (delta.x + ImGui::GetItemRectMax().x > viewportMaxScreenPos.x)
                delta.x = viewportMaxScreenPos.x - ImGui::GetItemRectMax().x;
              if (delta.x + ImGui::GetItemRectMin().x < viewportMinScreenPos.x)
                delta.x = viewportMinScreenPos.x - ImGui::GetItemRectMin().x;
              if (delta.y + ImGui::GetItemRectMax().y > viewportMaxScreenPos.y)
                delta.y = viewportMaxScreenPos.y - ImGui::GetItemRectMax().y;
              if (delta.y + ImGui::GetItemRectMin().y < viewportMinScreenPos.y)
                delta.y = viewportMinScreenPos.y - ImGui::GetItemRectMin().y;
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
              for (int i = 0; i < 9 && i < inputMode.numModes(); ++i) {
                id = static_cast<ImGuiKey>((int)ImGuiKey_0 + i + 1);
                if (ImGui::IsKeyPressed(id)) {
                  pressedId = i;
                  if (numIndexPressed++ > 0) break;
                }
              }
              if (numIndexPressed == 1) {
                inputMode.turnTo(static_cast<input_mode_e>(pressedId), *this);
                modeChanged = true;
              }
            };
          }
          // secondary options
          currentSelection = inputMode.currentMode();
          ;

          ImGui::SetWindowFontScale(1.f);
          ImGui::PopStyleVar();
        }
        // visualization options (top left)
        {
          ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.8f);
          ImGui::SetWindowFontScale(2.f);

          // primary options
          ImVec2 iconCursorPos{viewportMinScreenPos.x + style.FramePadding.x * 2,
                               viewportMinScreenPos.y + style.FramePadding.y * 2};
          auto drawOption = [&iconCursorPos, &imageHovered](const char *label, bool &enable,
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
              if (delta.x + ImGui::GetItemRectMax().x > viewportMaxScreenPos.x)
                delta.x = viewportMaxScreenPos.x - ImGui::GetItemRectMax().x;
              if (delta.x + ImGui::GetItemRectMin().x < viewportMinScreenPos.x)
                delta.x = viewportMinScreenPos.x - ImGui::GetItemRectMin().x;
              if (delta.y + ImGui::GetItemRectMax().y > viewportMaxScreenPos.y)
                delta.y = viewportMaxScreenPos.y - ImGui::GetItemRectMax().y;
              if (delta.y + ImGui::GetItemRectMin().y < viewportMinScreenPos.y)
                delta.y = viewportMinScreenPos.y - ImGui::GetItemRectMin().y;
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
          ImVec2 iconCursorPos{viewportMinScreenPos.x + style.FramePadding.x * 2,
                               viewportMaxScreenPos.y - style.FramePadding.y * 2};
          auto drawInfo = [&iconCursorPos, &style](const char *label) {
            auto sz = ImGui::CalcTextSize(label);
            iconCursorPos.y -= sz.y + style.ItemSpacing.y;
            ImGui::SetCursorScreenPos(iconCursorPos);
            ImGui::Text(label);
          };
          drawInfo(fmt::format("hovered obj label: {}, id: {}", getHoveredPrimLabel(),
                               getHoveredPrimId())
                       .c_str());
          drawInfo(fmt::format("focus obj label: {}, id: {}", getFocusPrimLabel(), getFocusPrimId())
                       .c_str());

          ImGui::SetWindowFontScale(1.f);
        }

        // header (top middle)
        {
          ImGui::SetWindowFontScale(1.5f);
          auto iconCursorPos = ImVec2{(viewportMaxScreenPos.x + viewportMinScreenPos.x) / 2,
                                      viewportMinScreenPos.y + style.FramePadding.y * 2};
          auto msg
              = fmt::format(fmt::runtime((const char *)u8"模式: {}\n"),
                            inputMode.getModeInfo(static_cast<input_mode_e>(currentSelection)));
          auto sz = ImGui::CalcTextSize(msg.c_str()).x;
          drawList->AddText(iconCursorPos - ImVec2{sz / 2, 0}, IM_COL32_WHITE, msg.c_str());
          ImGui::SetWindowFontScale(1.f);
        }

        // text overlay (bottom right)
        {
          ImGui::SetWindowFontScale(1.2f);
          auto msg = fmt::format("viewport mouse pos: {}, {}\n", viewportMousePos[0],
                                 viewportMousePos[1]);
          auto iconCursorPos = viewportMaxScreenPos - style.FramePadding * 2;
          drawList->AddText(iconCursorPos - ImGui::CalcTextSize(msg.c_str()), IM_COL32_WHITE,
                            msg.c_str());

          iconCursorPos.y -= ImGui::CalcTextSize(msg.c_str()).y;
          msg = fmt::format("use (L)Ctrl + index to switch interaction mode\n");
          drawList->AddText(iconCursorPos - ImGui::CalcTextSize(msg.c_str()), IM_COL32_WHITE,
                            msg.c_str());

          iconCursorPos.y -= ImGui::CalcTextSize(msg.c_str()).y;
          msg = fmt::format("rendering objects: {}\n", numFrameRenderModels);
          drawList->AddText(iconCursorPos - ImGui::CalcTextSize(msg.c_str()), IM_COL32_WHITE,
                            msg.c_str());

          iconCursorPos.y -= ImGui::CalcTextSize(msg.c_str()).y;
          msg = fmt::format("frame per second: {}\n", framePerSecond);
          drawList->AddText(iconCursorPos - ImGui::CalcTextSize(msg.c_str()), IM_COL32_WHITE,
                            msg.c_str());
          ImGui::SetWindowFontScale(1.f);
        }
      }

      /// upper layers
      this->sceneHovered = imageHovered;
      inputMode.paint();

      drawList->ChannelsMerge();
    }};
  }

  void SceneEditor::SceneEditorInputMode::turnTo(input_mode_e newMode, SceneEditor &editor) {
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
  const char *SceneEditor::SceneEditorInputMode::getModeInfo(input_mode_e id) const {
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
  const char *SceneEditor::SceneEditorInputMode::getIconText(input_mode_e id) const {
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

}  // namespace zs