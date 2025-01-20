#pragma once
#include <optional>

#include "glm/glm.hpp"
#include "imgui.h"
#include "world/async/StateMachine.hpp"
#include "zensim/types/ImplPattern.hpp"
#include "zensim/types/Polymorphism.h"
#include "zensim/ui/Widget.hpp"

namespace zs {

  struct SceneEditor;

  enum input_mode_e { _still = 0, _roaming, _select, _paint, _num_modes };
  enum layer_e { _scene = 0, _interaction, _config, _num_layers };
  struct SelectionRegion {
    glm::uvec2 offset, extent;
  };

  struct SceneEditorDefaultMode {
    SceneEditorDefaultMode(SceneEditor &editor);

    void paint();
    bool onEvent(GuiEvent *e) { return false; }

    SceneEditor &editor;
  };
  struct SceneEditorRoamingMode {
    SceneEditorRoamingMode(SceneEditor &editor);

    void paint();
    bool onEvent(GuiEvent *e) { return false; }

    SceneEditor &editor;
  };
  struct SceneEditorSelectionMode {
    SceneEditorSelectionMode(SceneEditor &editor);

    void init();  // initialize selection operation
    void paint();
    bool onEvent(GuiEvent *e);

    ImGuiMouseButton _mouseBinding{ImGuiMouseButton_Left};
    std::optional<ImVec2> selectionStart{}, selectionEnd{};
    int _state{0};
    StateMachine _selectOperation;
    SceneEditor &editor;
  };
  struct SceneEditorPaintMode {
    SceneEditorPaintMode(SceneEditor &editor);

    void init();  // initialize paint operation
    void paint();
    bool onEvent(GuiEvent *e);

    ImGuiMouseButton _mouseBinding{ImGuiMouseButton_Left};
    std::optional<ImVec2> _painterCenter{};
    float _painterRadius{10.f};
    bool _painting{false};
    StateMachine _paintOperation;
    SceneEditor &editor;
  };

  struct SceneEditorInteractionMode {
    void paint();
    void turnTo(input_mode_e newMode, SceneEditor &editor);
    int numModes() const { return (int)_num_modes; }
    int currentMode() const { return (int)_index; }
    const char *getModeInfo(input_mode_e id) const;
    const char *getIconText(input_mode_e id) const;
    bool isPaintMode() const noexcept { return _index == input_mode_e::_paint; }
    bool isEditMode() const noexcept {
      return _index == input_mode_e::_select || _index == input_mode_e::_paint;
    }

    bool onEvent(GuiEvent *e);

    input_mode_e _index{_still};
    variant<Owner<SceneEditorDefaultMode>, Owner<SceneEditorRoamingMode>,
            Owner<SceneEditorSelectionMode>, Owner<SceneEditorPaintMode>>
        _modes;
  };

  struct SceneEditorWidgetComponent : WidgetConcept {
    SceneEditorWidgetComponent(SceneEditor *s) noexcept : sceneEditor{s} {}
    ~SceneEditorWidgetComponent() override = default;

    void paint() override;

    void drawPath();

    bool onEvent(GuiEvent *e) override;

    SceneEditor *sceneEditor;
  };

}  // namespace zs