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

  SceneEditorRoamingMode::SceneEditorRoamingMode(SceneEditor &editor) : editor{editor} {
    editor._camCtrl._cameraState = camera_control_statemachine(editor._camCtrl);  // reset state
  }

  void SceneEditorRoamingMode::paint() {}

}  // namespace zs