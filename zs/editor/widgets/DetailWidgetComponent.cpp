#include "DetailWidgetComponent.hpp"

#include "world/system/ResourceSystem.hpp"
#include "AssetBrowserComponent.hpp"
#include "GraphWidgetComponent.hpp"
#include "SceneWidgetComponent.hpp"
#include "SequencerComponent.hpp"
#include "imgui.h"

namespace zs {

  void DetailsWidgetComponent::switchToWidget(inherent_widget_e target) {
    _target = target;
    switch (target) {
      case none_:
        _panel = std::monostate{};
        break;
      case asset_browser_:
        _panel = AssetBrowserDetailPanel{ResourceSystem::get_widget_ptr<AssetBrowserComponent>(
            g_defaultWidgetLabelAssetBrowser)};
        break;
      case sequencer_:
        _panel = SequencerDetailPanel{
            ResourceSystem::get_widget_ptr<SequencerWidget>(g_defaultWidgetLabelSequencer)};
        break;
      case graph_editor_:
        _panel = GraphEditorDetailPanel{
            ResourceSystem::get_widget_ptr<GraphWidgetComponent>(g_defaultWidgetLabelGraphEditor)};
        break;
      case scene_editor_:
        _panel = SceneEditorDetailPanel{
            ResourceSystem::get_widget_ptr<SceneEditorWidgetComponent>(g_defaultWidgetLabelScene)};
        break;
      default:
        break;
    }
  }
  void DetailsWidgetComponent::paint() {
    ImGui::Spacing();
    int curId = _panel.index();
    if (ImGui::BeginCombo("##detail_widget", s_labels[curId], ImGuiComboFlags_NoArrowButton)) {
      for (int i = 0; i < num_inherent_widgets; ++i) {
        const bool isSelected = (curId == i);
        if (ImGui::Selectable(s_labels[i], isSelected)) {
          curId = i;
          switchToWidget(static_cast<inherent_widget_e>(i));
        }
        if (isSelected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    if (curId) {
      match([](auto &panel) { panel.paint(); }, [](std::monostate) {})(_panel);
    }
  }

}  // namespace zs