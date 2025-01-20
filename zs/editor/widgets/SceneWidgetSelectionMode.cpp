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
    for (;;) {
      auto e__ = co_await zs::Event<GuiEvent *>{};
      auto e_ = std::get<GuiEvent *>(e__);

      switch (e_->getGuiEventType()) {
        case gui_event_mousePressed:
          // if (((WidgetNode *)cameraCtrl._editor->getWidget()->getZsUserPointer())
          //        ->windowHovered())
          if (selectionMode.editor.sceneHovered) {
            auto e = dynamic_cast<MousePressEvent *>(e_);
            if (e->button() == selectionMode._mouseBinding) {
              selectionMode.selectionEnd = selectionMode.selectionStart = e->windowPos();
              selectionMode._state = 1;
              e->accept();
            }
          }
          break;
        case gui_event_mouseReleased: {
          auto e = dynamic_cast<MouseReleaseEvent *>(e_);
          if (e->button() == selectionMode._mouseBinding && selectionMode.selectionStart.has_value()
              && selectionMode.selectionEnd.has_value()) {
            ///
            auto &editor = selectionMode.editor;
            auto &selectionStart = *selectionMode.selectionStart;
            auto &selectionEnd = *selectionMode.selectionEnd;
            glm::uvec2 mi{std::min(selectionStart.x, selectionEnd.x),
                          std::min(selectionStart.y, selectionEnd.y)},
                ma{1 + std::max(selectionStart.x, selectionEnd.x),
                   1 + std::max(selectionStart.y, selectionEnd.y)};

            editor.selectionBox = SelectionRegion{};
            (*editor.selectionBox).extent = ma - mi;
            (*editor.selectionBox).offset
                = mi - glm::uvec2{editor.canvasMinCursorPos.x, editor.canvasMinCursorPos.y};
            /// reset states
            selectionMode.selectionStart = selectionMode.selectionEnd = {};
            selectionMode._state = 0;

            e->accept();
          }
          break;
        }
        case gui_event_mouseMoved: {
          if (selectionMode._state == 1) {
            auto e = dynamic_cast<MouseMoveEvent *>(e_);
            // auto delta = e->getDelta();
            selectionMode.selectionEnd = e->windowPos();

            (*selectionMode.selectionEnd).x = std::clamp((*selectionMode.selectionEnd).x,
                                                         selectionMode.editor.canvasMinCursorPos.x,
                                                         selectionMode.editor.canvasMaxCursorPos.x);
            (*selectionMode.selectionEnd).y = std::clamp((*selectionMode.selectionEnd).y,
                                                         selectionMode.editor.canvasMinCursorPos.y,
                                                         selectionMode.editor.canvasMaxCursorPos.y);
            e->accept();
          }
          break;
        }
        default:
          break;
      }  // end guievent switch
    }
  }

  SceneEditorSelectionMode::SceneEditorSelectionMode(SceneEditor &editor) : editor{editor} {
    editor._camCtrl._cameraState = camera_control_statemachine(editor._camCtrl);  // reset state
  }

  void SceneEditorSelectionMode::init() {
    _selectOperation = selection_operation_statemachine(*this);
  }

  bool SceneEditorSelectionMode::onEvent(GuiEvent *e) {
    if (_selectOperation.hasValue()) _selectOperation.onEvent(e);
    return e->isAccepted();
  }

  void SceneEditorSelectionMode::paint() {
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSetCurrent(_interaction);

    /// selection
    if (_state) {
      drawList->AddRect(*selectionStart, *selectionEnd,
                        ImGui::GetColorU32(IM_COL32(0, 130, 216, 255)));  // Border
      drawList->AddRectFilled(*selectionStart, *selectionEnd,
                              ImGui::GetColorU32(IM_COL32(0, 130, 216, 50)));  // Background
    }

    // ImGui::SetCursorScreenPos(editor.canvasMinCursorPos);
  }

}  // namespace zs