#include "GraphWidgetComponent.hpp"
#include "WidgetDrawUtilities.hpp"

namespace zs {

  namespace ge {

    void Graph::paint() {
      hoveredNode = ed::GetHoveredNode();
      hoveredPin = ed::GetHoveredPin();
      hoveredLink = ed::GetHoveredLink();

      auto &editorStyle = ed::GetStyle();

      _drawnLinks.clear();

      const ImGuiID id = ImGui::GetCurrentWindow()->GetID(_name.c_str());

      auto prevOwner = ImGui::GetKeyOwner(ImGuiMod_Alt);
      if (prevOwner != id) _prevAltOwner = prevOwner;

      ImGui::SetKeyOwner(ImGuiMod_Alt, id);

      ed::Begin(_name.c_str());

      ///
      // util::BlueprintNodeBuilder builder(0, 500, 500);
      for (auto &[id, node] : _nodes) {
        node.paint();
      }
      ///
      for (auto &[id, link] : _links) {
        link.paint();
      }

      if (ed::BeginCreate()) {
        /// new link creation
        ed::PinId srcPinId, dstPinId;
        if (ed::QueryNewLink(&srcPinId, &dstPinId)) {
          if (srcPinId && dstPinId) {
#if 0
        if (_pins.find(srcPinId) != _pins.end() &&
            _pins.find(dstPinId) != _pins.end())
#endif
            {
              auto srcPin = _pins.at(srcPinId);
              auto dstPin = _pins.at(dstPinId);
              if (/*self link*/ srcPin->_node == dstPin->_node ||
                  /*output to input only*/ srcPin->_kind == dstPin->_kind ||
                  /*already linked*/ isLinked(srcPinId, dstPinId)) {
                ed::RejectNewItem();
              } else {
                if (ed::AcceptNewItem()) {
                  if (srcPin->_kind == pin_kind_e::Input) {
                    zs_swap(srcPin, dstPin);
                  }
                  /// remove existing links related to the dstPin (if any)
                  if (dstPin->_links.size()) _links.erase((*dstPin->_links.begin())->_id);
                  ///
                  auto lid = nextLinkId();
                  auto [iter, success]
                      = _links.emplace(std::piecewise_construct, std::forward_as_tuple(lid),
                                       std::forward_as_tuple(lid, srcPin, dstPin));
                  iter->second.paint();
                }
              }
            }
          }
        }
        /// new node creation
        ed::PinId pinId = 0;
        if (ed::QueryNewNode(&pinId)) {
          if (ed::AcceptNewItem()) {
            ed::Suspend();
            ImGui::OpenPopup((const char *)u8"创建节点");
            ed::Resume();
          }
        }
      }
      ed::EndCreate();
      ///
      if (ed::BeginDelete()) {
        /// link deletion
        ed::LinkId deletedLinkId;
        while (ed::QueryDeletedLink(&deletedLinkId)) {
          if (ed::AcceptDeletedItem()) {
            _links.erase(deletedLinkId);
          }
        }
        /// pin deletion
        ed::PinId deletedPinId;
        while (ed::QueryDeletedPin(&deletedPinId)) {
          if (ed::AcceptDeletedItem()) {
            auto pin = _pins.at(deletedPinId);
            pin->_node->removePin(pin);
          }
        }
        /// node deletion
        ed::NodeId nodeId = 0;
        while (ed::QueryDeletedNode(&nodeId)) {
          if (ed::AcceptDeletedItem()) {
            _nodes.erase(nodeId);
          }
        }
      }
      ed::EndDelete();

      ed::Suspend();
      if (ed::ShowBackgroundContextMenu()) {
        ImGui::OpenPopup((const char *)u8"创建节点");
      }
      ed::Resume();

      auto popupPosition = ImGui::GetMousePos();
      ed::Suspend();
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));

      if (ImGui::BeginPopup((const char *)u8"创建节点")) {
        Node *node = nullptr;
        if (ImGui::MenuItem((const char *)u8"节点")) node = spawnDummyNode();

        if (node) {
          ed::SetNodePosition(node->_id, popupPosition);
          // node->inited = true;
        }
        ImGui::EndPopup();
      }
      ImGui::PopStyleVar(1);
      ed::Resume();

      /// @note one-time
      while (!_itemMaintenanceActions.empty()) {
        auto &task = _itemMaintenanceActions.front();
        task();
        _itemMaintenanceActions.pop();
      }
      /// @note persistant
      for (auto &[id, auxWidgetAction] : _auxiliaryWidgetActions) {
        auxWidgetAction();
      }

      ed::End();

      if (_initRequired) {
#if 0
    ed::NavigateToContent();
    auto &canvasView =
        const_cast<ImGuiEx::CanvasView &>(getEditorContext()->GetView());
    getEditorContext()->NavigateTo(_viewRect, false, 0.0f);
#endif
        _initRequired = false;
      }

      selectionChanged = hasSelectionChanged();
      if (selectionChanged) {
        querySelectionNodes();
        numSelectedNodes = selectedNodes.size();
      }

      if (ed::GetSelectedObjectCount() == 0) ImGui::SetKeyOwner(ImGuiMod_Alt, _prevAltOwner);
    }

    ///
    bool Graph::isLinked(ed::PinId id0, ed::PinId id1) const {
      if (!id0 || !id1) return false;
      if (auto p0 = findPin(id0), p1 = findPin(id1); p0 && p1) {
        bool smaller = p0->_links.size() < p1->_links.size();
        const auto &s = smaller ? p0->_links : p1->_links;
        for (const auto &link : s)
          if ((link->_srcPin == p0 && link->_dstPin == p1)
              || (link->_srcPin == p1 && link->_dstPin == p0))
            return true;
      }
      return false;
    }

    ///
    zs::tuple<Graph::NodeMap::iterator, bool> Graph::spawnNode(std::string_view name,
                                                               ed::NodeId nid) {
      updateObjectId(nid.Get());
      auto [iter, success] = _nodes.emplace(std::piecewise_construct, std::forward_as_tuple(nid),
                                            std::forward_as_tuple(nid, this, name));
      if (!success)
        throw std::runtime_error("unable to create a new node (maybe due to duplication).");
      return zs::make_tuple(iter, success);
    }
    zs::tuple<Graph::NodeMap::iterator, bool> Graph::spawnNode(std::string_view name) {
      return spawnNode(name, nextNodeId());
    }
    zs::tuple<Graph::LinkMap::iterator, bool> Graph::spawnLink(Pin *srcPin, Pin *dstPin) {
      auto lid = nextLinkId();
      auto [iter, success] = _links.emplace(std::piecewise_construct, std::forward_as_tuple(lid),
                                            std::forward_as_tuple(lid, srcPin, dstPin));
      if (!success)
        throw std::runtime_error("unable to create a new link (maybe due to duplication).");
      return zs::make_tuple(iter, success);
    }
    void Graph::removePinLinks(Pin &pin) {
      /// @note link deletion will modify pin's _links on-the-fly
      auto links = pin._links;
      for (auto link : links) {
        _links.erase(link->_id);  // wipe link ptrs in _pins upon dtor
      }
    }
    void Graph::removePinLinks(ed::PinId id) {
      if (!id) return;

      if (auto pin = findPin(id); pin) removePinLinks(*pin);
    }

    ///
    void swap(Graph &a, Graph &b) noexcept {
      zs_swap(a._nextObjectId, b._nextObjectId);
      zs_swap(a._name, b._name);
      zs_swap(a._fileName, b._fileName);
      zs_swap(a._edCtx, b._edCtx);

      zs_swap(a._links, b._links);
      zs_swap(a._pins, b._pins);
      zs_swap(a._nodes, b._nodes);

      zs_swap(a._viewJson, b._viewJson);
      zs_swap(a._viewRect, b._viewRect);
      zs_swap(a._initRequired, b._initRequired);

      /// @note _graph ptr in _nodes are still referring to graph [o] ftm,
      /// update links to graph object
      a.updateGraphLinks();
      b.updateGraphLinks();
    }

  }  // namespace ge

}  // namespace zs