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

  static StateMachine paint_operation_statemachine(SceneEditorPaintMode &paintMode) {
    for (;;) {
      auto e__ = co_await zs::Event<GuiEvent *>{};
      auto e_ = std::get<GuiEvent *>(e__);

      switch (e_->getGuiEventType()) {
        case gui_event_mousePressed: {
          auto e = dynamic_cast<MousePressEvent *>(e_);
          if (paintMode.editor.sceneHovered && !paintMode._painterCenter.has_value())
            paintMode._painterCenter = e->windowPos();

          if (paintMode._painterCenter.has_value()) {
            if (e->button() == paintMode._mouseBinding) {
              paintMode._painting = true;
              e->accept();
            }
          }
          break;
        }
        case gui_event_mouseReleased: {
          auto e = dynamic_cast<MouseReleaseEvent *>(e_);
          if (e->button() == paintMode._mouseBinding && paintMode._painting) {
            paintMode._painting = false;
            e->accept();
          }
          break;
        }
        case gui_event_mouseScroll: {
          auto e = dynamic_cast<MouseScrollEvent *>(e_);
          if (paintMode._painterCenter.has_value()) {
            paintMode._painterRadius += e->getScrollV() * 2.5f;
            if (paintMode._painterRadius <= 1.f + detail::deduce_numeric_epsilon<float>() * 10)
              paintMode._painterRadius = 1.f;
            e->accept();
          }
          break;
        }
        case gui_event_mouseMoved: {
          if (paintMode.editor.sceneHovered) {
            auto e = dynamic_cast<MouseMoveEvent *>(e_);
            paintMode._painterCenter = e->windowPos();

            e->accept();
          } else {
            paintMode._painterCenter = {};
          }
          break;
        }
        default:
          break;
      }  // end guievent switch
    }
  }

  SceneEditorPaintMode::SceneEditorPaintMode(SceneEditor &editor) : editor{editor} {
    editor._camCtrl._cameraState = camera_control_statemachine(editor._camCtrl);  // reset state
  }

  void SceneEditorPaintMode::init() { _paintOperation = paint_operation_statemachine(*this); }

  bool SceneEditorPaintMode::onEvent(GuiEvent *e) {
    assert(_paintOperation.hasValue());
    _paintOperation.onEvent(e);
    return e->isAccepted();
  }

  void SceneEditorPaintMode::paint() {
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSetCurrent(_interaction);

    /// selection
    if (_painterCenter.has_value()) {
      auto c = *_painterCenter;

      editor.paintRadius = _painterRadius;
      // if (_painterRadius <= 1.f + detail::deduce_numeric_epsilon<float>() * 10)
      ImGui::SetMouseCursor(ImGuiMouseCursor_None);

      if (_painting) {
        //
        editor.paintCenter
            = glm::uvec2{c.x - editor.canvasMinCursorPos.x, c.y - editor.canvasMinCursorPos.y};

        drawList->AddCircle(c, _painterRadius, IM_COL32_WHITE, 0, 2.f);
      } else {
        drawList->AddCircle(c, _painterRadius, IM_COL32_WHITE, 0, 1.f);
      }
    }
  }

}  // namespace zs