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
        case gui_event_mousePressed:
          if (((WidgetNode *)cameraCtrl._editor->getWidget()->getZsUserPointer())
                  ->windowHovered()) {
#if 0
            fmt::print(
                "scene editor widget addr: {} (widget node pointer: {}, recorded: {}), hovered: "
                "{}\n",
                (void *)cameraCtrl._editor->getWidget().get(),
                (void *)cameraCtrl._editor->getWidget()->getZsUserPointer(),
                (void *)cameraCtrl._widget,
                ((WidgetNode *)cameraCtrl._editor->getWidget()->getZsUserPointer())
                    ->windowHovered());
            // (void *)cameraCtrl._widget->refWidget().get());
#endif
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
  }

  void CameraControl::update(float dt) {
    Camera &camera = *_cam;
    // check key states
    if (_keyStates[key_direction_e::front]) {
      camera.translateForward(dt * _advanceTranslationSpeed);
      _dirty = true;
    }
    if (_keyStates[key_direction_e::back]) {
      camera.translateForward(-dt * _advanceTranslationSpeed);
      _dirty = true;
    }
    if (_keyStates[key_direction_e::left]) {
      camera.translateHorizontal(-dt * _advanceTranslationSpeed);
      _dirty = true;
    }
    if (_keyStates[key_direction_e::right]) {
      camera.translateHorizontal(dt * _advanceTranslationSpeed);
      _dirty = true;
    }
    if (_keyStates[key_direction_e::up]) {
      camera.translateVertical(dt * _advanceTranslationSpeed);
      _dirty = true;
    }
    if (_keyStates[key_direction_e::down]) {
      camera.translateVertical(-dt * _advanceTranslationSpeed);
      _dirty = true;
    }

    if (_keyStates[key_direction_e::turn_left]) {
      camera.yaw(-dt * _rotationSpeed);
      _dirty = true;
    }
    if (_keyStates[key_direction_e::turn_right]) {
      camera.yaw(dt * _rotationSpeed);
      _dirty = true;
    }
    if (_keyStates[key_direction_e::look_up]) {
      camera.pitch(dt * _rotationSpeed);
      _dirty = true;
    }
    if (_keyStates[key_direction_e::look_down]) {
      camera.pitch(-dt * _rotationSpeed);
      _dirty = true;
    }

    if (_dirty) {
      camera.updateViewMatrix();
      _editor->sceneAugmentRenderer.overlayTextNeedUpdate = true;
      _dirty = false;
    }
  }
  void CameraControl::trackCamera(Camera &camera, SceneEditor &sceneEditor) {
    _cam = zs::addressof(camera);
    _editor = zs::addressof(sceneEditor);
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
      auto dir = camera.getCameraRayDirection(
          sceneEditor->viewportMousePos[0], sceneEditor->viewportMousePos[1],
          (float)sceneEditor->vkCanvasExtent.width, (float)sceneEditor->vkCanvasExtent.height);
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
                                        (float)sceneEditor->vkCanvasExtent.width,
                                        (float)sceneEditor->vkCanvasExtent.height);
        ImGui::Text("(validation) hit screen point: %.3f, %.3f", xy.x, xy.y);
      }
      ImGui::PopTextWrapPos();
    };
  }

  void SceneEditor::SceneEditorRoamingMode::paint() {}

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
  Shared<SceneEditorWidgetComponent> &SceneEditor::getWidget() {
    if (!_widget) _widget = std::make_shared<SceneEditorWidgetComponent>(this);
    // fmt::print("\n\n\ncurrent scene editor widget addr: {}\n\n\n", (void *)_widget.get());
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