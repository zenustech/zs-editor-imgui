#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include "world/async/StateMachine.hpp"

struct CameraControl;

namespace zs {

  inline StateMachine camera_control_statemachine(CameraControl &cameraCtrl) {
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

}  // namespace zs