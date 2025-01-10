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
      auto e__ = co_await zs::Event<GuiEvent *>{};
      auto e_ = std::get<GuiEvent *>(e__);

      switch (e_->getGuiEventType()) {
        case gui_event_mousePressed: {
          auto e = dynamic_cast<MousePressEvent *>(e_);
          auto idx = cameraCtrl.findMouseAction(e->button());
          if (idx != -1) {
            activeMouseButton = e->button();
            mouseAction = idx;
          }
          break;
        }
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
          break;
        }
        case gui_event_keyPressed: {
          auto e = dynamic_cast<KeyPressEvent *>(e_);
          auto idx = cameraCtrl.findDirectionIndex(e->key());
          cameraCtrl.setKeyState(idx, 1);
          break;
        }
        case gui_event_keyReleased: {
          auto e = dynamic_cast<KeyReleaseEvent *>(e_);
          auto idx = cameraCtrl.findDirectionIndex(e->key());
          cameraCtrl.setKeyState(idx, 0);
          break;
        }
        default:
          break;
      }  // end guievent switch
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
  Shared<SceneEditorWidgetComponent> SceneEditor::getWidget() {
    if (!_widget) _widget = std::make_shared<SceneEditorWidgetComponent>(this);
    return _widget;
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