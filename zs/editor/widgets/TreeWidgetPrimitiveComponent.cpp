#include "TreeWidgetComponent.hpp"
//
#include <filesystem>

#include "imgui.h"
#include "imgui_internal.h"
#include "world/system/ResourceSystem.hpp"
#include "world/system/ZsExecSystem.hpp"
#include "world/scene/Primitive.hpp"
#include "world/scene/PrimitiveConversion.hpp"

namespace zs {

  namespace ui {

    template <typename Builder>
    void __build_primitive_node(Builder& builder, Shared<ZsPrimitive> prim) {
      if (!prim) return;

      builder.setOption(TreeNodeTrailingOptions::type_e::_visible, true)
          .setupObject([prim](PrimitiveTreeNode& node) {
            node._path = prim->getPath();
            // node.setLabel(std::filesystem::path(node._path).stem().string());
            node.setLabel(prim->label());
            node._prim = prim;
            node.setType(PrimitiveTreeNode::_mesh);
          });

      size_t nChilds = prim->numChildren();
      if (nChilds) {
        for (size_t i = 0; i < nChilds; ++i) {
          auto ch_ = prim->getChild(i);
          if (auto ch = ch_.lock()) {
            auto chBuilder = builder.beginChild();
            __build_primitive_node(chBuilder, ch);
            builder.endChild();
          }
        }
      }
    }

    PrimitiveTreeNode* build_primitive_tree_node(Weak<ZsPrimitive> pr) {
      auto builder = build_tree_node<PrimitiveTreeNode>();  // pr ? pr->getName() : "<empty>"

      auto prim = pr.lock();
      assert(prim && "current ZsPrimitive pointed-to should be valid.");
      __build_primitive_node(builder, prim);
      return builder.get();
    }

    ///
    ///
    ///
    void PrimitiveTreeNode::onDelete(std::string_view path) {
      auto p = _prim.lock();
      if (!p) return;  // already removed
      auto parent = p->getParent();
      if (parent->removeChild(p.get())) {
        // p is still valid yet, because it is being share-owned by p.
        // ResourceSystem::mark_primitive_dirty(_path);
        // ResourceSystem::onPrimitivesChanged().emit({std::string{_sceneName}});
      }

      // ResourceSystem::onPrimitivesChanged().emit({std::string{_sceneName}});
    }

    PrimitiveEditor::PrimitiveEditor() {
      _sceneContextLabel = g_defaultSceneLabel;

      /// setup tab maintenance ops
      _tabs.setLeadingSymbolLiteral((const char*)ICON_MD_LIST);
      _tabs.setInsertionCallback([this](std::string_view label, Meta& meta) {
        meta._scene = ResourceSystem::get_scene_context_primitive(_sceneContextLabel, label);
      });
      _tabs.setRemovalCallback([](std::string_view label, Meta& meta) {});
      _tabs.setIsValidPredicate([this](std::string_view label) -> bool {
        auto pMeta = _tabs.getEntryContent(label);
        if (pMeta) return static_cast<bool>(pMeta->_scene.lock());
        return false;
      });
      _tabs.setIsModifiedPredicate([](std::string_view label) -> bool { return false; });
      _tabs.setSaveCallback([](std::string_view label) -> void {});
      _tabs.setItemListCallback([this]() -> std::vector<std::string> {
        return ResourceSystem::get_scene_context_primitive_labels(_sceneContextLabel);
      });
      //
      // _tabs.appendTab(g_defaultUsdLabel);

      /// as in AssetBrowser, monitor usd file changes
      ResourceSystem::onSceneContextPrimitiveCreation().connect(
          [this](const std::vector<std::string>& labels) {
            for (auto& label : labels) {
              auto prim = ResourceSystem::get_scene_context_primitive(_sceneContextLabel, label);
              ResourceSystem::set_widget(get_scene_prim_widget_label(_sceneContextLabel, label),
                                         ui::build_primitive_tree_node(prim));
            }
          });
      ResourceSystem::onSceneContextPrimitiveChanged().connect(
          [this](const std::vector<std::string>& labels) {
            for (auto& label : labels) {
              auto prim = ResourceSystem::get_scene_context_primitive(_sceneContextLabel, label);
              ResourceSystem::set_widget(get_scene_prim_widget_label(_sceneContextLabel, label),
                                         ui::build_primitive_tree_node(prim));
            }
          });
    }
    PrimitiveEditor::~PrimitiveEditor() {}
    void PrimitiveEditor::paint() {
      _tabs.paint();
      int selectedIdx = _tabs.getFocusId();

      /// @note avoid tree of different tabs (usd files) id conflicts (open status, etc.)
      if (selectedIdx != -1)
        ImGui::PushID(ImGui::GetCurrentWindow()->GetID(_tabs[selectedIdx].getLabel().data()));

      if (ImGui::BeginChild("ScenePrimitiveTree",
                            ImVec2(0.0f, -ImGui::GetTextLineHeightWithSpacing()),
                            ImGuiChildFlags_Borders, ImGuiWindowFlags_NoMove)) {
        if (selectedIdx != -1) {
          auto label = _tabs[selectedIdx].getLabel();
          ResourceSystem::get_widget_ptr(get_scene_prim_widget_label(_sceneContextLabel, label))
              ->paint();
        }
      }
      ImGui::EndChild();

      if (selectedIdx != -1) ImGui::PopID();

      ///
      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSETS_BROWSER_ITEMS")) {
          auto assets = (AssetEntry*)payload->Data;
          const auto numAssets = payload->DataSize / sizeof(AssetEntry);

          for (int i = 0; i < numAssets; ++i) {
            const auto& entry = assets[i];
#if 0
            if (entry._type != AssetEntry::usd_) continue;
            const auto& l = entry.getLabel();
            int idx = _tabs.tabOpened(l);
            // fmt::print("DROPPING asset file: {} to text editor!\n", assets[i].getLabel());

            if (idx != -1) {
              _tabs[idx]._visible = true;
            } else if (!l.empty()) {
              _tabs.appendTab(l);
            }
#endif
          }
        }
        ImGui::EndDragDropTarget();
      }
    }

  }  // namespace ui

}  // namespace zs