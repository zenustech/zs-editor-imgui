#pragma once
#include "zensim/types/ImplPattern.hpp"
#include "zensim/ui/Widget.hpp"

namespace zs {

  struct SceneEditor;

  struct SceneEditorWidgetComponent : WidgetConcept {
    SceneEditorWidgetComponent(SceneEditor* s) noexcept : sceneEditor{s} {}
    ~SceneEditorWidgetComponent() override = default;

    void paint() override;
    void drawPath();

    bool onEvent(GuiEvent* e) override;

    SceneEditor* sceneEditor;
  };

}  // namespace zs