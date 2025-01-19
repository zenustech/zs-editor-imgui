#include "../SceneEditor.hpp"
#include "SceneWidgetComponent.hpp"
//
#include "editor/widgets/WidgetComponent.hpp"
//
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/transform.hpp>

#include "SceneWidgetCameraUtils.hpp"

namespace zs {

  SceneEditorDefaultMode::SceneEditorDefaultMode(SceneEditor &editor) : editor{editor} {
    editor._camCtrl._cameraState = camera_control_statemachine(editor._camCtrl);  // reset state
  }

  void SceneEditorDefaultMode::paint() {}

}  // namespace zs