#pragma once
#include <string_view>
#include <variant>

#include "world/scene/Primitive.hpp"
#include "zensim/types/ImplPattern.hpp"
#include "zensim/types/Polymorphism.h"
#include "zensim/ui/Widget.hpp"

namespace zs {

  struct SceneEditorWidgetComponent;
  struct SceneEditorDetailPanel : WidgetComponentConcept {
    void paint() override {}

    SceneEditorDetailPanel(SceneEditorWidgetComponent *w) noexcept : widget{w} {}
    SceneEditorWidgetComponent *widget;
  };

  struct AssetBrowserComponent;
  struct AssetBrowserDetailPanel : WidgetComponentConcept {
    void paint() override {}

    AssetBrowserDetailPanel(AssetBrowserComponent *w) noexcept : widget{w} {}
    AssetBrowserComponent *widget;
  };

  struct SequencerWidget;
  struct SequencerDetailPanel : WidgetComponentConcept {
    void paint() override {}

    SequencerDetailPanel(SequencerWidget *w) noexcept : widget{w} {}
    SequencerWidget *widget;
  };

  struct GraphWidgetComponent;
  struct GraphEditorDetailPanel : WidgetComponentConcept {
    void paint() override {}

    GraphEditorDetailPanel(GraphWidgetComponent *w) noexcept : widget{w} {}
    GraphWidgetComponent *widget;
  };

  using DetailsPanel = std::variant<std::monostate, AssetBrowserDetailPanel, SequencerDetailPanel,
                                    GraphEditorDetailPanel, SceneEditorDetailPanel>;

  struct DetailsWidgetComponent : WidgetComponentConcept {
    enum inherent_widget_e {
      none_ = 0,
      asset_browser_,
      sequencer_,
      graph_editor_,
      scene_editor_,
      num_inherent_widgets
    };
    inline static const char *s_labels[num_inherent_widgets]
        = {(const char *)u8"<自动>", (const char *)u8"资源管理", (const char *)u8"时间轴",
           (const char *)u8"结点图", (const char *)u8"场景视口"};

    DetailsWidgetComponent() = default;
    DetailsWidgetComponent(std::string_view label) : _label{label} {}
    ~DetailsWidgetComponent() = default;
    DetailsWidgetComponent(DetailsWidgetComponent &&) = default;
    DetailsWidgetComponent &operator=(DetailsWidgetComponent &&) = default;

    void switchToWidget(inherent_widget_e target);

    void paint() override;

    std::string _label;
    inherent_widget_e _target{none_};
    DetailsPanel _panel{};
  };

}  // namespace zs