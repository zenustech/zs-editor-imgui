#pragma once
#include "zensim/types/ImplPattern.hpp"
#include "zensim/ui/Widget.hpp"

namespace zs {

  struct SceneEditor;

  struct SceneEditorWidgetComponent : WidgetConcept {
    void paint() override;

    SceneEditor* sceneEditor;
  };

}  // namespace zs