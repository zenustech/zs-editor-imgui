#include "GraphWidgetComponent.hpp"
#include "WidgetDrawUtilities.hpp"

namespace zs {

  namespace ge {

    void Link::paint() {
      auto findTargetPin = [](Pin *pin) -> Pin * {
        Pin *ret = pin, *par = pin->_parent;
        while (par) {
          // find the most remote ancestor that is being collapsed
          if (!par->expanded()) ret = par;
          par = par->_parent;
        }
        return ret;
      };
      auto startPin = findTargetPin(_srcPin);
      auto endPin = findTargetPin(_dstPin);
      auto graph = _srcPin->_node->_graph;
      auto pair = std::make_pair(startPin->_id.Get(), endPin->_id.Get());
      if (graph->_drawnLinks.find(pair) == graph->_drawnLinks.end()) {
        // avoid duplicate links being displayed between a pair of pins upon collapse
        ed::Link(_id, startPin->_id, endPin->_id);
        ed::Flow(_id);
        graph->_drawnLinks.emplace(zs::move(pair));
      }
    }

    Link::~Link() {
      ed::DeleteFlow(_id);
      ed::DeleteLink(_id);
      if (_srcPin) {
        _srcPin->removeLink(this);
        _srcPin = nullptr;
      }
      if (_dstPin) {
        _dstPin->removeLink(this);
        _dstPin = nullptr;
      }
      _id = ed::LinkId(0);
    }

  }  // namespace ge

}  // namespace zs