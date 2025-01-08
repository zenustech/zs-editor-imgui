#include "SceneWidgetComponent.hpp"

#include "../SceneEditor.hpp"

namespace zs {

  void SceneEditorWidgetComponent::paint() {
    ;
    ;
  }
  bool SceneEditorWidgetComponent::onEvent(GuiEvent* e) {
    if (sceneEditor->_camCtrl.onEvent(e)) return e->isAccepted();
    return false;
  }

}  // namespace zs