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

  SceneEditorPaintMode::SceneEditorPaintMode(SceneEditor &editor) : editor{editor} {
    editor._camCtrl._cameraState = camera_control_statemachine(editor._camCtrl);  // reset state
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

}  // namespace zs