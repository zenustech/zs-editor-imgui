#include "TreeWidgetComponent.hpp"

#include "imgui.h"
#include "imgui_internal.h"

namespace zs {

  namespace ui {

    bool tree_node_get_open(TreeNodeBase* node) {
      return ImGui::GetStateStorage()->GetBool(node->_id);
    }
    void tree_node_set_open(TreeNodeBase* node, bool open) {
      ImGui::GetStateStorage()->SetBool(node->_id, open);
    }
    // When closing a node: 1) close and unselect all child nodes, 2) select parent if any child was
    // selected.
    // FIXME: This is currently handled by user logic but I'm hoping to eventually provide tree node
    // features to do this automatically, e.g. a ImGuiTreeNodeFlags_AutoCloseChildNodes etc.
    int tree_close_and_unselect_child_nodes(TreeNodeBase* node,
                                            ImGuiSelectionBasicStorage* selection, int depth) {
      // Recursive close (the test for depth == 0 is because we call this on a node that was just
      // closed!)
      int unselected_count = selection->Contains((ImGuiID)node->_id) ? 1 : 0;
      if (depth == 0 || tree_node_get_open(node)) {
        auto numChs = node->numViewedChildren();
        for (u32 i = 0; i < numChs; ++i)
          // for (TreeNodeBase* child : node->Childs)
          unselected_count += tree_close_and_unselect_child_nodes(
              static_cast<TreeNodeBase*>(node->getChildByIndex(i)), selection, depth + 1);
        // tree_node_set_open(node, false);
      }

      // Select root node if any of its child was selected, otherwise unselect
      selection->SetItemSelected((ImGuiID)node->_id, (depth == 0 && unselected_count > 0));
      return unselected_count;
    }

    void tree_set_all_in_open_nodes(TreeNodeBase* node, ImGuiSelectionBasicStorage* selection,
                                    bool selected);
    TreeNodeBase* tree_get_next_node_in_visible_order(TreeNodeBase* curr_node,
                                                      TreeNodeBase* last_node);

    void tree_set_all_in_open_nodes(TreeNodeBase* node, ImGuiSelectionBasicStorage* selection,
                                    bool selected) {
      if (node->getParent() != NULL)  // Root node isn't visible nor selectable in our scheme
        selection->SetItemSelected(node->_id, selected);
      if (node->getParent() == NULL || tree_node_get_open(node)) {
        auto numChs = node->numViewedChildren();
        for (u32 i = 0; i < numChs; ++i)
          tree_set_all_in_open_nodes(static_cast<TreeNodeBase*>(node->getChildByIndex(i)),
                                     selection, selected);
      }
    }

    // Interpolate in *user-visible order* AND only *over opened nodes*.
    // If you have a sequential mapping tables (e.g. generated after a filter/search pass) this
    // would be simpler. Here the tricks are that:
    // - we store/maintain ExampleTreeNode::IndexInParent which allows implementing a linear
    // iterator easily, without searches, without recursion.
    //   this could be replaced by a search in parent, aka 'int index_in_parent =
    //   curr_node->Parent->Childs.find_index(curr_node)' which would only be called when crossing
    //   from child to a parent, aka not too much.
    // - we call SetNextItemStorageID() before our TreeNode() calls with an ID which doesn't relate
    // to UI stack,
    //   making it easier to call TreeNodeGetOpen()/TreeNodeSetOpen() from any location.
    TreeNodeBase* tree_get_next_node_in_visible_order(TreeNodeBase* curr_node,
                                                      TreeNodeBase* last_node) {
      // Reached last node
      if (curr_node == last_node) return nullptr;

      // Recurse into childs. Query storage to tell if the node is open.
      if (curr_node->numViewedChildren() > 0 && tree_node_get_open(curr_node))
        return static_cast<TreeNodeBase*>(curr_node->getChildByIndex(0));

      // Next sibling, then into our own parent
      TreeNodeBase* par;
      while ((par = static_cast<TreeNodeBase*>(curr_node->getParent())) != nullptr) {
        if (auto idx = curr_node->getViewIndexInParent(); idx + 1 < par->numViewedChildren())
          return static_cast<TreeNodeBase*>(par->getChildByIndex(idx + 1));
        curr_node = static_cast<TreeNodeBase*>(par);
      }
      return nullptr;
    }

  }  // namespace ui

}  // namespace zs