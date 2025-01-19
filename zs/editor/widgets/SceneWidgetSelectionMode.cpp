#include "../SceneEditor.hpp"
#include "SceneWidgetComponent.hpp"
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

#include "SceneWidgetCameraUtils.hpp"

namespace zs {

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

  SceneEditorSelectionMode::SceneEditorSelectionMode(SceneEditor &editor) : editor{editor} {
    editor._camCtrl._cameraState = camera_control_statemachine(editor._camCtrl);  // reset state
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

}  // namespace zs