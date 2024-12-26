#include "GraphWidgetComponent.hpp"
#include "WidgetDrawUtilities.hpp"
#include "editor/widgets/ResourceWidgetComponent.hpp"
#include "interface/details/PyHelper.hpp"
#include "world/core/Utils.hpp"

namespace zs {

  namespace ge {

    void Node::paint() {
#if 0
  ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(229, 229, 229, 200));
  ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(125, 125, 125, 200));
  ed::PushStyleColor(ed::StyleColor_PinRect, ImColor(229, 229, 229, 60));
  ed::PushStyleColor(ed::StyleColor_PinRectBorder, ImColor(125, 125, 125, 60));
#endif

      const auto pinBackground = ed::GetStyle().Colors[ed::StyleColor_NodeBg];

#if 0
  const float rounding = 10.0f;
  const float padding = 12.0f;
  ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 0));
  ed::PushStyleVar(ed::StyleVar_NodeRounding, rounding);
  ed::PushStyleVar(ed::StyleVar_SourceDirection, ImVec2(0.0f, 1.0f));
  ed::PushStyleVar(ed::StyleVar_TargetDirection, ImVec2(0.0f, -1.0f));
  ed::PushStyleVar(ed::StyleVar_LinkStrength, 0.0f);
  ed::PushStyleVar(ed::StyleVar_PinBorderWidth, 1.0f);
  ed::PushStyleVar(ed::StyleVar_PinRadius, 6.0f);
#endif

      /// evaluate width
      const auto lw = evaluateInputPinWidth();
      const auto rw = evaluateOutputPinWidth();

      if (_graph->_initRequired) {
        ed::SetNodePosition(_id, _pos);
      }

      ImGui::BeginGroup();
      ImGui::PushID(_id.AsPointer());

      /// header content
      ed::BeginNode(_id);

      ImGui::Dummy(ImVec2(0.0f, 0));
      auto HeaderMin = ImGui::GetItemRectMin() + ImVec2(0, -ImGui::GetStyle().ItemSpacing.y * 2);

      ImGui::Dummy(ImVec2(Pin::get_pin_icon_size(), 0));
      ImGui::SameLine();
      ImGui::Text(_name.c_str());
      // for display symmetry
      ImGui::SameLine();
      ImGui::Dummy(ImVec2(Pin::get_pin_icon_size(), 0));

      auto headerWidth = ImGui::GetItemRectMax().x;

      _nodeWidth
          = std::max(lw + rw, headerWidth - HeaderMin.x + 16 - ed::GetStyle().NodeBorderWidth);

      _nodeBound = headerWidth;  // updated upon pin drawings

      auto &editorStyle = ed::GetStyle();
      // auto verticalOffset = Pin::get_pin_icon_size() -
      // ImGui::GetStyle().FramePadding.y / 2;
      ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().ItemSpacing.y
                           + ImGui::GetStyle().ItemInnerSpacing.y
                           + editorStyle.NodeBorderWidth * 2);

      auto headerHeight = ImGui::GetItemRectMax().y;

      /// pins
      // inputs
      _curChildPinNo = 0;
      for (auto &pin : _inputs) {
        pin.paint(0.f);
        _curChildPinNo++;
      }
      // outputs
      _curChildPinNo = 0;
      for (auto &pin : _outputs) {
        pin.paint(_nodeWidth);
        _curChildPinNo++;
      }

      ed::EndNode();

      /// header background
      if (ImGui::IsItemVisible()) {
        auto HeaderMax = ImVec2(_nodeBound, headerHeight);
        // background
        const auto halfBorderWidth = ed::GetStyle().NodeBorderWidth * 0.5f;
        const auto uv
            = ImVec2((HeaderMax.x - HeaderMin.x) / (float)(4.0f * /*HeaderTextureWidth*/ 32),
                     (HeaderMax.y - HeaderMin.y) / (float)(4.0f * /*HeaderTextureHeight*/ 32));
        auto headerColor
            = IM_COL32(0, 0, 0, 255) | (ImColor(255, 255, 255) & IM_COL32(255, 255, 255, 0));

        auto drawList = ed::GetNodeBackgroundDrawList(_id);
#if 0
    drawList->AddImageRounded(
        /*HeaderTextureId*/ 1,
        HeaderMin - ImVec2(8 - halfBorderWidth, 4 - halfBorderWidth),
        HeaderMax + ImVec2(8 - halfBorderWidth, 0), ImVec2(0.0f, 0.0f), uv,
        headerColor, ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersTop);
#else
        drawList->AddRectFilled(HeaderMin - ImVec2(8 - halfBorderWidth, 4 - halfBorderWidth),
                                HeaderMax + ImVec2(8 - halfBorderWidth, -halfBorderWidth),
                                ImColor(editorStyle.Colors[ed::StyleColor_NodeSelRectBorder]),
                                ed::GetStyle().NodeRounding,
                                ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersTopRight);
#endif
        if constexpr (false) {
          ImColor color = ImColor(editorStyle.Colors[ed::StyleColor_NodeBorder]);
          float borderWidth = editorStyle.NodeBorderWidth;
          ImVec2 p0{HeaderMin.x - (8 + halfBorderWidth), HeaderMax.y - 0.5f};
          ImVec2 p1{HeaderMax.x + (8), HeaderMax.y - 0.5f};
          if (ed::IsNodeSelected(_id)) {
            color = ImColor(editorStyle.Colors[ed::StyleColor_SelNodeBorder]);
            // borderWidth = editorStyle.SelectedNodeBorderWidth;
          } else {
            p0.x += 2 * halfBorderWidth;
            p1.x -= 2 * halfBorderWidth;
          }
          drawList->AddLine(p0, p1, color, borderWidth);
        }
      }
      // ed::SetNodePosition(_id, ImVec2(610, 20));

      ImGui::PopID();
      ImGui::EndGroup();

      // ed::PopStyleVar(7);
      // ed::PopStyleColor(4);
    }

    Node::~Node() {
      clearInputs();
      clearOutputs();
    }

    void Node::clearInputs() {
      auto guard = _graph->contextGuard();
      _inputs.clear();
    }
    void Node::clearOutputs() {
      auto guard = _graph->contextGuard();
      _outputs.clear();
    }

    Pin *Node::appendInput(std::string_view label) {
      auto id = _graph->nextPinId();
      return appendInput(id, label);
    }
    Pin *Node::appendOutput(std::string_view label) {
      auto id = _graph->nextPinId();
      return appendOutput(id, label);
    }
    Pin *Node::appendInput(const ed::PinId &id, std::string_view label) {
      _inputs.emplace_back(id, this, label, pin_type_e::Flow, pin_kind_e::Input);
      return _graph->_pins[id] = &_inputs.back();
    }
    Pin *Node::appendOutput(const ed::PinId &id, std::string_view label) {
      _outputs.emplace_back(id, this, label, pin_type_e::Flow, pin_kind_e::Output);
      return _graph->_pins[id] = &_outputs.back();
    }

    Pin *Node::appendInput(const Pin *target, std::string_view label) {
      auto id = _graph->nextPinId();
      return appendInput(target, id, label);
    }
    Pin *Node::appendOutput(const Pin *target, std::string_view label) {
      auto id = _graph->nextPinId();
      return appendOutput(target, id, label);
    }
    Pin *Node::appendInput(const Pin *target, const ed::PinId &id, std::string_view label) {
      auto it = std::begin(_inputs);
      for (; it != std::end(_inputs); ++it)
        if (zs::addressof(*it) == target) break;
      if (it != std::end(_inputs)) it++;
      auto ret = _inputs.emplace(it, id, this, label, pin_type_e::Flow, pin_kind_e::Input);
      return _graph->_pins[id] = zs::addressof(*ret);
    }
    Pin *Node::appendOutput(const Pin *target, const ed::PinId &id, std::string_view label) {
      auto it = std::begin(_outputs);
      for (; it != std::end(_outputs); ++it)
        if (zs::addressof(*it) == target) break;
      if (it != std::end(_outputs)) it++;
      auto ret = _outputs.emplace(it, id, this, label, pin_type_e::Flow, pin_kind_e::Output);
      return _graph->_pins[id] = zs::addressof(*ret);
    }

    float Node::evaluateInputPinWidth() const {
      float width = 0;
      for (auto &pin : _inputs) {
        if (auto w = pin.evaluatePinWidth(); w > width) width = w;
      }
      return width;
    }
    float Node::evaluateOutputPinWidth() const {
      float width = 0;
      for (auto &pin : _outputs) {
        if (auto w = pin.evaluatePinWidth(); w > width) width = w;
      }
      return width;
    }

    ZsVar &Node::setupAttribAndWidget(std::string_view label, ZsDict desc) {
      auto res = _attribs.emplace(label, ZsVar{});
      if (res.second && res.first->second) {  // check validity of the inserted ZsVar
        fmt::print("node attrib with name [{}] already inserted.\n", label);
      } else {
        auto &obj = res.first->second;
        ZsObject typeStr = desc["type"];
        ZsObject contents = desc["defl"];

        PyVar bs = zs_bytes_obj(typeStr);
        auto ts = std::string(bs.asBytes().c_str());

        if (ts.size() > 5 && ts.substr(0, 4) == "enum") {
          if (contents) {
            // aux
            std::vector<std::string> options = parse_string_segments(ts, " ");
            PyVar l = zs_list_obj_default();
            assert(options.size() > 1 && "should specify at least one enum option");
            for (int i = 1; i < options.size(); ++i)
              l.asList().appendSteal(zs_string_obj_cstr(options[i].c_str()));

            //
            if (contents.isString()) {
              obj.share(contents);
            } else if (contents.isBytesOrByteArray()) {
              obj = zs_string_obj_cstr(contents.asBytes().c_str());
            } else {
              obj.share(l.asList()[0]);
              assert(0 && "enum without a VALID (str/ bytes) initial "
                      "pick is not allowed!");
            }

            if (obj) _attribWidget.appendWidget(label, obj, l.asList());
          } else {
            assert(0 && "enum without an initial pick is not allowed!");
          }
        }
        /// int
        else if (ts == "int") {
          obj = zs::zs_int_descriptor(desc["defl"]);
          if (obj) _attribWidget.appendWidget(label, obj);
        }
        /// float
        else if (ts == "float") {
          obj = zs::zs_float_descriptor(desc["defl"]);
          if (obj) _attribWidget.appendWidget(label, obj);
        }
        /// string
        else if (ts == "string" || ts == "str") {
          obj = zs::zs_string_descriptor(desc["defl"]);
          if (obj) _attribWidget.appendWidget(label, obj);
        }
        /// list
        else if (ts == "list") {
          obj = zs::zs_list_descriptor(desc["defl"]);
          if (obj) _attribWidget.appendWidget(label, obj);
        }
        /// dict
        else if (ts == "dict") {
          obj = zs::zs_dict_descriptor(desc["defl"]);
          if (obj) _attribWidget.appendWidget(label, obj);
        }
        /// wildcard
        else {
          ;
        }
      }
      return res.first->second;
    }
    zs::ui::GenericAttribWidget &Node::setupAttribWidgets(ZsDict desc) {
      _attribWidget = zs::ui::GenericAttribWidget();

      if (desc.isDict()) {
        auto it = desc.begin();
        while (it) {  // iterate sections (categories)
          ZsString key = it.key();
          ZsObject val = it.value();

          PyVar keyBs = zs_bytes_obj(key);
          _attribWidget.newSection(keyBs.asBytes().c_str(),
                                   ui::GenericAttribWidget::separator_type_e::header);

          assert(val.isList());
          if (val.isList()) {
            for (auto e : val.asList()) {  // iterate section attributes
              assert(e.isDict());
              if (e.isDict()) {
                ZsDict &d = e.asDict();
                ZsObject nameStr = d["name"];
                PyVar bs = zs_bytes_obj(nameStr);
                if (bs) {
                  auto &obj = setupAttribAndWidget(bs.asBytes().c_str(), d);
                  if (!obj) {  // attrib not successfully initialized
                    fmt::print("node [{} ({})] attrib [{}] setup failed!\n", _name, _id.Get(),
                               bs.asBytes().c_str());
                  }
                }
              }
            }
          }
          ++it;
        }
      }
      return _attribWidget;
    }

    void swap(Node &a, Node &b) noexcept {
      zs_swap(a._id, b._id);
      zs_swap(a._name, b._name);
      zs_swap(a._graph, b._graph);
      zs_swap(a._subgraph, b._subgraph);
      zs_swap(a._inputs, b._inputs);
      zs_swap(a._outputs, b._outputs);
      zs_swap(a._pos, b._pos);
      zs_swap(a._type, b._type);
      zs_swap(a._nodeWidth, b._nodeWidth);
      zs_swap(a._nodeBound, b._nodeBound);
      zs_swap(a.cmdText, b.cmdText);
      zs_swap(a.helperText, b.helperText);
      zs_swap(a._attribs, b._attribs);
      zs_swap(a._attribWidget, b._attribWidget);
    }

  }  // namespace ge

}  // namespace zs