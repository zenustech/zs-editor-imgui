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
          sceneEditor->canvasLocalMousePos[0], sceneEditor->canvasLocalMousePos[1],
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

  ///
  /// final widget
  ///
  Shared<SceneEditorWidgetComponent> &SceneEditor::getWidget() {
    if (!_widget) _widget = std::make_shared<SceneEditorWidgetComponent>(this);
    // fmt::print("\n\n\ncurrent scene editor widget addr: {}\n\n\n", (void *)_widget.get());
    return _widget;
  }

}  // namespace zs