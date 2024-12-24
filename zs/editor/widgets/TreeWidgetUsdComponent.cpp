#include "TreeWidgetComponent.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "world/system/ResourceSystem.hpp"
#include "world/system/ZsExecSystem.hpp"

namespace zs {

  namespace ui {

    template <typename Builder> void __build_usd_node(Builder& builder, ScenePrimConcept* prim) {
      if (!prim) return;

      builder  //.enableSelection()
          .setOption(TreeNodeTrailingOptions::type_e::_visible, true)
          .setupObject([prim](UsdTreeNode& node) {
            node.setLabel(prim->getName());
            node._path = prim->getPath();
            // node._prim = prim;
            node._scene = prim->getScene();
            node._sceneName = prim->getScene()->getName();
            if (prim->isValid()) node.setType(UsdTreeNode::_mesh);
          });

      size_t nChilds = 0;  // the following might not assign any value to it, must init 0
      prim->getAllChilds(&nChilds, nullptr);
      std::vector<ScenePrimHolder> childs(nChilds);
      if (nChilds) {
        prim->getAllChilds(&nChilds, childs.data());

        for (size_t i = 0; i < nChilds; ++i) {
          auto chBuilder = builder.beginChild();
          __build_usd_node(chBuilder, childs[i].get());
          builder.endChild();
        }
      }
    }

    UsdTreeNode* build_usd_tree_node(ScenePrimConcept* pr) {
      auto builder = build_tree_node<UsdTreeNode>();  // pr ? pr->getName() : "<empty>"

      __build_usd_node(builder, pr);
      return builder.get();
    }

    ///
    ///
    ///
    void UsdTreeNode::onDelete(std::string_view path) {
#if ZPC_USD_ENABLED
      fmt::print("deleted [{}] of scene [{}] ({})\n", _path, _scene->getName(), _sceneName);
      auto ret = ResourceSystem::get_usd(_sceneName)->removePrim(path.data());
      fmt::print("retrieved scene name {}, deletion success: {}\n",
                 ResourceSystem::get_usd(_sceneName)->getName(), ret);

      // dynamic_cast<USDSceneDesc*>(ResourceSystem::get_usd(_sceneName))->viewTree();

      ResourceSystem::mark_usd_dirty(_sceneName);
#endif
      ResourceSystem::onUsdFilesChanged().emit({std::string{_sceneName}});
    }

    void TreeNodeBase::paintNode(ImGuiSelectionBasicStorage* selection, const std::string& path) {
      ImGui::TableNextRow();
      /// column 1
      ImGui::TableNextColumn();

      auto flags
          = _flags | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NavLeftJumpsBackHere;
      if (isLeaf()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet;
      if (selection->Contains(_id)) {  // isSelected
        flags |= ImGuiTreeNodeFlags_Selected;
      }

      ImGui::AlignTextToFramePadding();  // to align with potential trailing buttons
      ImGui::SetNextItemSelectionUserData((ImGuiSelectionUserData)(intptr_t)this);
      ImGui::SetNextItemStorageID(_id);
      bool nodeOpen = ImGui::TreeNodeEx(_label.c_str(), flags);
      bool toggled = !nodeOpen && ImGui::IsItemToggledOpen();

      // right-click menu
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopupEx((ImGuiID)_id,
                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
                               | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
      }
      if (ImGui::BeginPopupEx((ImGuiID)_id,
                              ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
                                  | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
        ImGui::Text(path.substr(0, path.size() - 1).c_str());
        if (ImGui::Selectable((const char*)u8"删除")) {
          ZS_EVENT_SCHEDULER().emplace(
              // must hold a std::string rather than a string_view
              [this, path = std::string(path.substr(0, path.size() - 1))]() {
                this->onDelete(path);
              });

          /// @note when tree rebuilt (e.g. signaled here), imgui ids are likely reused, thus reset
          /// here ahead
          /// @note reset open/close state here to operate within imgui window scope
          tree_node_set_open(this, false);
        }
        ImGui::EndPopup();
      }
      // trailing buttons
      auto findOption = [this](TreeNodeTrailingOptions::type_e type) -> TreeNodeTrailingOptions* {
        if (_trailingOptions.contains(type)) return &_trailingOptions.at(type);
        return nullptr;
      };
      /// column 2 (visibility)
      ImGui::PushID(_id);
      ImGui::TableNextColumn();
      {
        auto option = findOption(TreeNodeTrailingOptions::_visible);
        if (option) {
          option->paint();
        } else
          ImGui::TextDisabled("--");
      }
      /// column 3 (editable)
      ImGui::TableNextColumn();
      {
        auto option = findOption(TreeNodeTrailingOptions::_editable);
        if (option) {
          option->paint();
        } else
          ImGui::TextDisabled("--");
      }
      ImGui::PopID();
      /*
                if (_trailingOptions.size()) {
                  const auto &style = ImGui::GetStyle();
                  auto width = ImGui::GetContentRegionAvail().x;
                  auto accumWidth = 0.f;
                  for (int i = 0; i < _trailingOptions.size(); ++i) {
                    auto &option = _trailingOptions[i];
                    accumWidth += option.evalWidth() + style.FramePadding.x * 2.0f +
         style.ItemSpacing.x; ImGui::SameLine(width - accumWidth); option.paint();
                  }
                }
                */

      // children
      // if (_toggled) _toggled = ImGui::IsItemToggledOpen();
      if (nodeOpen) {  // togglable() &&
        ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing() - ImGui::GetStyle().FramePadding.x * 2);
        for (auto& child : _viewedTreeNodes) {
          auto ch = static_cast<TreeNodeBase*>(child);
          ch->paintNode(selection, path + ch->getLabel() + "/");
        }
        ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing() - ImGui::GetStyle().FramePadding.x * 2);
      } else if (toggled) {
        tree_close_and_unselect_child_nodes(this, selection);
      }

      if (nodeOpen) {
        ImGui::TreePop();
      }
    }

    void TreeNodeBase::paint() {
      bool tableOpen = false;
      if (tableOpen = ImGui::BeginTable("tree_table", 3, ImGuiTreeNodeFlags_SpanFullWidth);
          tableOpen) {
        ImGui::TableSetupColumn((const char*)u8"结点名", ImGuiTableColumnFlags_NoHide);
        ImGui::TableSetupColumn((const char*)u8"可见", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::CalcTextSize((const char*)u8"可见").x);
        ImGui::TableSetupColumn((const char*)u8"编辑", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::CalcTextSize((const char*)u8"编辑").x);
        ImGui::TableHeadersRow();
      }

      /// multi select
      ImGuiMultiSelectFlags flags
          = ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_BoxSelect1d;
      ImGuiMultiSelectIO* msIo = ImGui::BeginMultiSelect(flags, _imguiSelection.Size, -1);

      auto applySelectionRequests = [](ImGuiMultiSelectIO* ms_io, TreeNodeBase* tree,
                                       ImGuiSelectionBasicStorage* selection) {
        for (ImGuiSelectionRequest& req : ms_io->Requests) {
          if (req.Type == ImGuiSelectionRequestType_SetAll) {
            if (req.Selected)
              tree_set_all_in_open_nodes(tree, selection, req.Selected);
            else
              selection->Clear();
          } else if (req.Type == ImGuiSelectionRequestType_SetRange) {
            TreeNodeBase* first_node = (TreeNodeBase*)(intptr_t)req.RangeFirstItem;
            TreeNodeBase* last_node = (TreeNodeBase*)(intptr_t)req.RangeLastItem;
            int cnt = 0;
            for (TreeNodeBase* node = first_node; node != nullptr;
                 node = tree_get_next_node_in_visible_order(node, last_node)) {
              selection->SetItemSelected(node->_id, req.Selected);
#if 0
              if (cnt++ > 100) {
                fmt::print(
                    "iterating {} and still counting. first node: ({}) {} (id in par: {}, open: "
                    "{}), last node: ({}) {} (id "
                    "in par: {}, open: {}). cur "
                    "node: ({}) {} (id in par: {}, open: {}, par child size: {})\n",
                    cnt, first_node->_label, first_node ? first_node->_id : 0,
                    first_node->getViewIndexInParent(), tree_node_get_open(first_node),
                    last_node->_label, last_node ? last_node->_id : 0,
                    last_node->getViewIndexInParent(), tree_node_get_open(last_node), node->_label,
                    node ? node->_id : 0, node->getViewIndexInParent(), tree_node_get_open(node),
                    node->getParent()->numViewedChildren());
                if (node->getViewIndexInParent() == -1) {
                  fmt::print("\t[");
                  for (int i = 0; i < node->getParent()->numViewedChildren(); ++i)
                    fmt::print(
                        "{}, ",
                        static_cast<TreeNodeBase*>(node->getParent()->getChildByIndex(i))->_id);
                  fmt::print("]\n");
                }
                fmt::print("iterating {} and still counting. selection size: {}\n", cnt,
                           selection->Size);
                getchar();
              }
#endif
            }
          }
        }
      };
      applySelectionRequests(msIo, this, &_imguiSelection);

      paintNode(&_imguiSelection, "/");

      msIo = ImGui::EndMultiSelect();
      applySelectionRequests(msIo, this, &_imguiSelection);

      if (tableOpen) {
        ImGui::EndTable();
      }

      // ImGui::PopStyleVar();
    }

    SceneFileEditor::SceneFileEditor() {
      /// setup tab maintenance ops
      _tabs.setLeadingSymbolLiteral((const char*)ICON_MD_LIST);
      _tabs.setInsertionCallback([](std::string_view label, Meta& meta) {
#if ZPC_USD_ENABLED
        meta._scene = ResourceSystem::get_usd(label);
#else
        meta._scene = nullptr;
#endif
      });
      _tabs.setRemovalCallback([](std::string_view label, Meta& meta) {
#if ZPC_USD_ENABLED
        if (label == g_defaultUsdLabel) {  // default usd file is always saved upon removal
          ResourceSystem::save_usd(g_defaultUsdLabel);
        }
#else
#endif
      });
      _tabs.setIsValidPredicate([](std::string_view label) -> bool {
#if ZPC_USD_ENABLED
        return ResourceSystem::has_usd(std::string{label});
#else
        return true;
#endif
      });
      _tabs.setIsModifiedPredicate([](std::string_view label) -> bool {
#if ZPC_USD_ENABLED
        return ResourceSystem::usd_modified(std::string{label});
#else
        return false;
#endif
      });
      _tabs.setSaveCallback([](std::string_view label) -> void {
#if ZPC_USD_ENABLED
        ResourceSystem::save_usd(label);
#endif
      });
      _tabs.setItemListCallback(
          []() -> std::vector<std::string> { return ResourceSystem::get_usd_labels(); });
      //
      _tabs.appendTab(g_defaultUsdLabel);

      /// as in AssetBrowser, monitor usd file changes
      ResourceSystem::onUsdFilesChanged().connect([](const std::vector<std::string>& labels) {
        for (auto& label : labels) {
#if ZPC_USD_ENABLED
          fmt::print("rebuilding usd scene [{}] widget!\n", label);
          auto root = ResourceSystem::get_usd(label)->getRootPrim();
          // auto root = ResourceSystem::get_usd(label)->getPrim("/");
          ResourceSystem::set_widget(std::string(label), ui::build_usd_tree_node(root.get()));
#endif
        }
      });
    }
    SceneFileEditor::~SceneFileEditor() {}
    void SceneFileEditor::paint() {
      _tabs.paint();
      int selectedIdx = _tabs.getFocusId();

      /// @note avoid tree of different tabs (usd files) id conflicts (open status, etc.)
      if (selectedIdx != -1)
        ImGui::PushID(ImGui::GetCurrentWindow()->GetID(_tabs[selectedIdx].getLabel().data()));

      if (ImGui::BeginChild("UsdTree", ImVec2(0.0f, -ImGui::GetTextLineHeightWithSpacing()),
                            ImGuiChildFlags_Borders, ImGuiWindowFlags_NoMove)) {
        if (selectedIdx != -1) {
          auto label = _tabs[selectedIdx].getLabel();
          ResourceSystem::get_widget_ptr(label)->paint();
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
            if (entry._type != AssetEntry::usd_) continue;
            const auto& l = entry.getLabel();
            int idx = _tabs.tabOpened(l);
            // fmt::print("DROPPING asset file: {} to text editor!\n", assets[i].getLabel());

            if (idx != -1) {
              _tabs[idx]._visible = true;
            } else if (!l.empty()) {
              _tabs.appendTab(l);
            }
          }
        }
        ImGui::EndDragDropTarget();
      }
    }

  }  // namespace ui

}  // namespace zs