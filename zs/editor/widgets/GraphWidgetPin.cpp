#include "GraphWidgetComponent.hpp"
#include "WidgetDrawUtilities.hpp"

#include "imgui.h"
#include "imgui_node_editor.h"
#include "imgui_stdlib.h"
#include "editor/widgets/ResourceWidgetComponent.hpp"

#include "interface/details/PyHelper.hpp"
#include "world/core/Utils.hpp"

struct Zs_InputTextCallback_UserData {
  std::string *Str;
  ImGuiInputTextCallback ChainCallback;
  void *ChainCallbackUserData;
};
static int InputTextCallback(ImGuiInputTextCallbackData *data) {
  Zs_InputTextCallback_UserData *user_data =
      (Zs_InputTextCallback_UserData *)data->UserData;
  if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    // Resize string callback
    // If for some reason we refuse the new length (BufTextLen) and/or capacity
    // (BufSize) we need to set them back to what we want.
    std::string *str = user_data->Str;
    IM_ASSERT(data->Buf == str->c_str());
    str->resize(data->BufTextLen);
    data->Buf = (char *)str->c_str();
  } else if (user_data->ChainCallback) {
    // Forward to user callback, if any
    data->UserData = user_data->ChainCallbackUserData;
    return user_data->ChainCallback(data);
  }
  return 0;
}

namespace zs {

namespace ge {

int Pin::getPinIndex() const {
  if (!_parent) {
    return _node->_curChildPinNo;
  } else {
    return _parent->_curChildPinNo;
  }
}

void Pin::paint(float width) {
  if (!visible())
    return;

  ed::PushStyleVar(ed::StyleVar_PinCorners, ImDrawFlags_RoundCornersAll);

  // ed::PushStyleVar(ed::StyleVar_PinArrowSize, 10.0f);
  // ed::PushStyleVar(ed::StyleVar_PinArrowWidth, 10.0f);
  // ed::PushStyleVar(ed::StyleVar_PinCorners, ImDrawFlags_RoundCornersBottom);

  const ed::PinKind kd = getEditorPinKind();

  int alpha = 255;
  ImColor color = ImColor(255, 255, 255); // determined by type
  color.Value.w = alpha / 255.0f;

  const float textOffsetY = evaluateIconTextVerticalOffset();
  // bool connected = _node->_graph->isPinLinked(_id);
  // bool connected = _links.size() > 0;
  bool connected = isPinLinked();
  bool canEdit = editable(connected);
  bool isExpanded = expanded();

  auto leftBound = ImGui::GetWindowPos().x + ed::GetNodePosition(_node->_id).x;
  auto rightBound = leftBound + width;

  auto &editorStyle = ed::GetStyle();

  ImGui::PushID(_id.AsPointer());
  ImGui::BeginGroup();

  const auto padding = ImGui::GetStyle().FramePadding;
  const auto spacing = ImGui::GetStyle().ItemInnerSpacing;
  ImVec2 pinRectMin, pinRectMax;

  float appendixZoneStartX, appendixZoneEndX, appendixZoneStartY;
  float appendixTriggerZoneStartX, appendixTriggerZoneEndX;
  if (_kind == pin_kind_e::Input) {

    /// 0-icon
    float offsetX = get_pin_icon_size() + ImGui::GetStyle().ItemSpacing.x;

    ed::BeginPin(_id, kd, _parent ? ed::PinFlags_Removable : ed::PinFlags_None);
    ax::Widgets::Icon(ImVec2(static_cast<float>(get_pin_icon_size()),
                             static_cast<float>(get_pin_icon_size())),
                      getIconType(), connected, getIconColor(),
                      ImColor(32, 32, 32, alpha));
    ed::EndPin();
    pinRectMin = ImGui::GetItemRectMin();
    pinRectMax = ImGui::GetItemRectMax();

    ImGui::SameLine();

    appendixZoneStartX =
        ImGui::GetItemRectMin().x; // retrieved after first item

    /// 1-label
    offsetX +=
        ImGui::CalcTextSize(_name.c_str()).x + ImGui::GetStyle().ItemSpacing.x;

    ImGui::SetCursorPosY(pinRectMin.y + textOffsetY);

    // ImGui::Text(_name.c_str());
    drawLabelText();

    appendixZoneStartY = ImGui::GetItemRectMax().y; // retrieved after label
    appendixTriggerZoneStartX = ImGui::GetItemRectMin().x;

    /// 2-switch(optional)
    if (expandable()) {
      ImVec2 pos = ImGui::GetItemRectMin();

      ImGui::SameLine();
      // ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textOffsetY);

      if (isExpanded) {
        appendixZoneStartX = ImGui::GetItemRectMin().x;

        offsetX += ImGui::CalcTextSize("+").x + ImGui::GetStyle().ItemSpacing.x;

        /// layering: https://github.com/ocornut/imgui/issues/7291
        auto drawList = ImGui::GetWindowDrawList();
        auto color = editorStyle.Colors[ed::StyleColor_SelNodeBorder];

        std::string_view offTag = "-";
        ImGui::TextColored(editorStyle.Colors[ed::StyleColor_PinRectBorder],
                           offTag.data());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
          _expanded = false;

        ImVec2 labelSize = ImGui::CalcTextSize(_name.c_str());
        drawList->AddRect(
            pos -
                ImVec2(editorStyle.PinRounding, editorStyle.PinRounding) * 0.5,
            pos + labelSize +
                ImVec2(editorStyle.PinRounding, editorStyle.PinRounding) * 0.5,
            ImColor(color.x, color.y, color.z, 1.f), editorStyle.PinRounding,
            ImDrawFlags_RoundCornersAll, editorStyle.NodeBorderWidth);
      } else {
        offsetX += ImGui::CalcTextSize("-").x + ImGui::GetStyle().ItemSpacing.x;

        std::string_view onTag = "+";
        ImGui::TextColored(editorStyle.Colors[ed::StyleColor_PinRectBorder],
                           onTag.data());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
          _expanded = true;
      }
    }

    /// 3-content(customizable)
    if (canEdit) {
      ImGui::SameLine();
      offsetX += spacing.x + ImGui::GetStyle().ItemSpacing.x;
      // ImGui::SetCursorPosY(pinRectMin.y - spacing.y / 2);

      auto textLabel = std::string("##") + _name;

      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding.x / 2, 0));
      ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(spacing.x, 0));
      auto sz = spacing.x +
                // ImGui::CalcTextSize(content, content + strlen(content) + 1);
                _contentWidget.preferredWidth();
      sz += ImGui::CalcTextSize(" ").x;
      sz = std::max(get_min_content_size(), sz);
      sz = std::min(_node->_nodeWidth - offsetX - width - spacing.x, sz);

#if 1
      ImGui::SetNextItemWidth(sz);
      drawContentItem();
#elif 1
      auto content = contentData();
      ImGui::SetNextItemWidth(sz.x);
      std::string &contentStr = (*_content);
      ImGui::InputText(textLabel.c_str(), &contentStr,
                       ImGuiInputTextFlags_EnterReturnsTrue |
                           ImGuiInputTextFlags_AutoSelectAll);
#elif 0
      std::string &contentStr = (*_content);
      ImGui::InputTextEx(textLabel.c_str(), NULL, content,
                         (*_content).capacity(), sz,
                         ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_AutoSelectAll);
#else
      std::string &contentStr = (*_content);
      Zs_InputTextCallback_UserData cb_user_data;
      cb_user_data.Str = &contentStr;
      cb_user_data.ChainCallback = nullptr;
      cb_user_data.ChainCallbackUserData = nullptr;
      ImGui::InputTextEx(textLabel.c_str(), NULL, content,
                         (*_content).capacity() + 1, sz,
                         ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_AutoSelectAll |
                             ImGuiInputTextFlags_CallbackResize,
                         InputTextCallback, &cb_user_data);
#endif
      ImGui::PopStyleVar(2);
    }

    /// FIN
    appendixZoneEndX = ImGui::GetItemRectMax().x; // retrieved at the end
    appendixTriggerZoneEndX = ImGui::GetItemRectMax().x;
    if (appendixZoneEndX > _node->_nodeBound)
      _node->_nodeBound = appendixZoneEndX;

    ImGui::SetCursorPosY(pinRectMax.y + ImGui::GetStyle().ItemSpacing.y);

  } else {
    float offset = 0.f;
    auto boundX = width - ImGui::CalcTextSize(_name.c_str()).x -
                  get_pin_icon_size() - ImGui::CalcTextSize("+").x -
                  spacing.x / 2 - ImGui::GetStyle().ItemSpacing.x * 4;

    enum stage_e : u32 {
      stage_content = 0,
      stage_switch,
      stage_label,
      stage_icon,
      num_stages
    };
    u32 headStage = stage_label;
    ImVec2 dims[num_stages] = {};
    std::memset(dims, 0, sizeof(ImVec2) * num_stages);

    // 0-content
    if (canEdit) {
      dims[stage_content] =
          ImVec2(spacing.x, 0) +
          // ImGui::CalcTextSize(contentData(),
          //                     contentData() + strlen(contentData()) + 1
          ImVec2(_contentWidget.preferredWidth(), 0);
      dims[stage_content].x += ImGui::CalcTextSize(" ").x;
      dims[stage_content].x =
          std::max(get_min_content_size(), dims[stage_content].x);
      dims[stage_content].x = std::min(boundX, dims[stage_content].x);

      headStage = stage_content;
      offset += dims[stage_content].x + ImGui::GetStyle().ItemSpacing.x;
    }
    // 1-switch
    if (expandable()) {
      if (isExpanded)
        dims[stage_switch] = ImGui::CalcTextSize("-");
      else
        dims[stage_switch] = ImGui::CalcTextSize("+");

      if (headStage > stage_switch)
        headStage = stage_switch;
      offset += dims[stage_switch].x + ImGui::GetStyle().ItemSpacing.x;
    }
    // 2-label
    dims[stage_label] = ImGui::CalcTextSize(_name.c_str());
    offset += dims[stage_label].x + ImGui::GetStyle().ItemSpacing.x;
    // 3-icon
    dims[stage_icon] = ImVec2(get_pin_icon_size(), get_pin_icon_size());
    offset += dims[stage_icon].x;

    appendixZoneStartX = rightBound - offset;
    appendixTriggerZoneStartX = rightBound - offset;

    /// 0-content(customizable)
    if (dims[stage_content].x !=
        0.f) { // output pin content is editable at all time
      // ImGui::SetCursorPosY(ImGui::GetCursorPosY() - spacing.y / 2);
      auto textLabel = std::string("##") + _name;

      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding.x / 2, 0));
      ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(spacing.x, 0));

      ImGui::SetCursorPosX(rightBound - offset);

      ImGui::SetNextItemWidth(dims[stage_content].x);
      drawContentItem();

      offset -= (dims[stage_content].x + ImGui::GetStyle().ItemSpacing.x);

      ImGui::PopStyleVar(2);
    }

    /// 1-switch(optional)
    if (dims[stage_switch].x != 0.f) {
      if (headStage < stage_switch)
        ImGui::SameLine();
      else
        ImGui::SetCursorPosX(rightBound - offset);

      if (isExpanded) {
        std::string_view offTag = "-";
        ImGui::TextColored(editorStyle.Colors[ed::StyleColor_PinRectBorder],
                           offTag.data());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
          _expanded = false;
      } else {
        std::string_view onTag = "+";
        ImGui::TextColored(editorStyle.Colors[ed::StyleColor_PinRectBorder],
                           onTag.data());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
          _expanded = true;
      }

      offset -= (dims[stage_switch].x + ImGui::GetStyle().ItemSpacing.x);
    }

    /// 2-label
    if (headStage < stage_label)
      ImGui::SameLine();
    else
      ImGui::SetCursorPosX(rightBound - offset);

    // ImGui::Text(_name.c_str());
    drawLabelText();

    if (isExpanded) {
      ImVec2 pos = ImGui::GetItemRectMin();
      pos.y -= spacing.y / 2;
      auto drawList = ImGui::GetWindowDrawList();
      auto color = editorStyle.Colors[ed::StyleColor_SelNodeBorder];

      ImVec2 labelSize = ImGui::CalcTextSize(_name.c_str());
      drawList->AddRect(
          pos - ImVec2(editorStyle.PinRounding, editorStyle.PinRounding) * 0.5,
          pos + labelSize +
              ImVec2(editorStyle.PinRounding, editorStyle.PinRounding) * 0.5,
          ImColor(color.x, color.y, color.z, 1.f), editorStyle.PinRounding,
          ImDrawFlags_RoundCornersAll, editorStyle.NodeBorderWidth);
    }
    offset -= (dims[stage_label].x + ImGui::GetStyle().ItemSpacing.x);

    appendixZoneStartY = ImGui::GetItemRectMax().y; // retrieved after label
    appendixZoneEndX = ImGui::GetItemRectMax().x;
    appendixTriggerZoneEndX = ImGui::GetItemRectMax().x;

    /// 3-icon
    ImGui::SameLine();
    ImGui::SetCursorPosX(rightBound - offset);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - textOffsetY);

    ed::BeginPin(_id, kd, _parent ? ed::PinFlags_Removable : ed::PinFlags_None);
    ax::Widgets::Icon(ImVec2(static_cast<float>(get_pin_icon_size()),
                             static_cast<float>(get_pin_icon_size())),
                      getIconType(), connected, getIconColor(),
                      ImColor(32, 32, 32, alpha));
    ed::EndPin();

    pinRectMax = ImGui::GetItemRectMax();

    if (pinRectMax.x > _node->_nodeBound)
      _node->_nodeBound = pinRectMax.x;

    if (!isExpanded)
      appendixZoneEndX = ImGui::GetItemRectMax().x; // retrieved at the end

    ImGui::SetCursorPosY(pinRectMax.y + spacing.y);
  }

  /// secondary pins
  if (isExpanded) {
    const auto &color = editorStyle.Colors[ed::StyleColor_SelNodeBorder];
    if (_kind == pin_kind_e::Input) {
      ImGui::Spacing();

      ImGui::Indent(get_pin_icon_size());
      ImVec2 p0 = ImGui::GetCursorScreenPos();
      p0.y = pinRectMax.y + ImGui::GetStyle().ItemSpacing.y;
      ImGui::Indent(ImGui::GetStyle().ItemSpacing.x);

      // ImGui::Text(_name.c_str());
      ImGui::Spacing();
      ImGui::PushButtonRepeat(true);
      ImGui::SetCursorPosY(p0.y + ImGui::GetStyle().ItemSpacing.y);
      if (ImGui::SmallButton("+")) {
        auto id = _node->_graph->nextPinId();
        append(id, std::string("item_") + std::to_string(id.Get()));
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("-")) {
        remove_back();
      }
      ImGui::PopButtonRepeat();

      _curChildPinNo = 0;
      for (auto &ch : _chs) {
        ch.paint(width + get_pin_icon_size() + ImGui::GetStyle().ItemSpacing.x);
        _curChildPinNo++;
      }

      ImGui::Unindent(ImGui::GetStyle().ItemSpacing.x);
      ImVec2 p1 = ImGui::GetCursorScreenPos();
      ImGui::Unindent(get_pin_icon_size());

      ImGui::Spacing();

      /// vertical line
      auto drawList = ImGui::GetWindowDrawList();
      drawList->AddLine(p0, p1, ImColor(color.x, color.y, color.z, 1.f),
                        editorStyle.NodeBorderWidth);

    } else if (_kind == pin_kind_e::Output) {
      const auto &spacing = ImGui::GetStyle().ItemInnerSpacing;
      ImGui::Spacing();

      ImVec2 p0(0, pinRectMax.y + ImGui::GetStyle().ItemSpacing.y);
      // label
#if 0
      ImGui::SetCursorPosX(rightBound - get_pin_icon_size() -
                           ImGui::GetStyle().ItemSpacing.x -
                           ImGui::CalcTextSize(_name.c_str()).x);
      ImGui::Text(_name.c_str());
#else
      ImGui::Spacing();
      ImGui::PushButtonRepeat(true);
      ImGui::SetCursorPosY(p0.y + ImGui::GetStyle().ItemSpacing.y);
      ImGui::SetCursorPosX(rightBound - get_pin_icon_size() -
                           ImGui::GetStyle().ItemSpacing.x * 3 - spacing.x * 4 -
                           ImGui::CalcTextSize("+").x -
                           ImGui::CalcTextSize("-").x);
      if (ImGui::SmallButton("+")) {
        auto id = _node->_graph->nextPinId();
        append(id, std::string("item_") + std::to_string(id.Get()));
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("-")) {
        remove_back();
      }
      ImGui::PopButtonRepeat();
#endif

      _curChildPinNo = 0;
      for (auto &ch : _chs) {
        ch.paint(width - get_pin_icon_size() - ImGui::GetStyle().ItemSpacing.x);
        _curChildPinNo++;
      }
      ImVec2 p1 = ImGui::GetCursorScreenPos();

      ImGui::Spacing();

      /// vertical line
      auto drawList = ImGui::GetWindowDrawList();
      p0.x = p1.x = rightBound - get_pin_icon_size();
      drawList->AddLine(p0, p1, ImColor(color.x, color.y, color.z, 1.f),
                        editorStyle.NodeBorderWidth);

      ImGui::SetCursorPosX(rightBound);
      ImGui::Dummy(ImVec2(0, 0));
    }
  }

  /// appendix zone
  ImRect appendixTriggerZone{
      ImVec2{appendixTriggerZoneStartX, appendixZoneStartY +
                                            ImGui::GetStyle().ItemSpacing.y -
                                            get_half_insertion_zone_span()},
      ImVec2{appendixTriggerZoneEndX, appendixZoneStartY +
                                          ImGui::GetStyle().ItemSpacing.y +
                                          get_half_insertion_zone_span()}};
  // if (ImGui::IsMouseDragging(ed::GetConfig().DragButtonIndex, 1))
  if (appendixTriggerZone.Contains(ImGui::GetMousePos())) {
    auto ctx = _node->_graph->getEditorContext();
    auto &itemCreator = ctx->GetItemCreator();
    if (itemCreator.IsDragging() && itemCreator.m_DraggedPin &&
        itemCreator.m_DraggedPin->m_Kind != getEditorPinKind()) {
      if (ImGui::IsMouseReleased(ed::GetConfig().DragButtonIndex)) {
        // fmt::print("Creating a new pin and link!\n");

        /// @note item creations in effect in the next frame
        _node->_graph->enqueueMaintenanceAction([this,
                                                 graph = _node->_graph]() {
          auto &itemCreator = graph->getEditorContext()->GetItemCreator();
          /// @note hide the to-be-deleted pin during the next draw
          if (itemCreator.m_OriginalActivePin &&
              itemCreator.m_OriginalActivePin != itemCreator.m_DraggedPin) {
            auto pin = graph->_pins.at(itemCreator.m_OriginalActivePin->m_ID);
            pin->setInvisible();
          }
          if (expanded()) { // child-level appendix
            auto graph = _node->_graph;
            auto id = graph->nextPinId();
            // auto chPin = append(zs::addressof(*std::begin(_chs)), id,
            // std::to_string(id.Get()));
            auto chPin =
                prepend(id, std::string("item_") + std::to_string(id.Get()));
            auto srcPin = graph->_pins.at(itemCreator.m_DraggedPin->m_ID);
            auto dstPin = chPin;
            if (srcPin->_kind == pin_kind_e::Input &&
                dstPin->_kind == pin_kind_e::Output)
              zs_swap(srcPin, dstPin);
            graph->spawnLink(srcPin, dstPin);
          } else { // same-level appendix
            if (_parent) {
              auto graph = _node->_graph;
              auto id = graph->nextPinId();
              auto chPin = _parent->append(
                  this, id, std::string("item_") + std::to_string(id.Get()));
              auto srcPin = graph->_pins.at(itemCreator.m_DraggedPin->m_ID);
              auto dstPin = chPin;
              if (srcPin->_kind == pin_kind_e::Input &&
                  dstPin->_kind == pin_kind_e::Output)
                zs_swap(srcPin, dstPin);
              graph->spawnLink(srcPin, dstPin);
            } else {
              auto graph = _node->_graph;
              auto id = graph->nextPinId();
              Pin *chPin = nullptr;
              if (_kind == pin_kind_e::Input)
                chPin = _node->appendInput(
                    this, id, std::string("item_") + std::to_string(id.Get()));
              else if (_kind == pin_kind_e::Output)
                chPin = _node->appendOutput(
                    this, id, std::string("item_") + std::to_string(id.Get()));
              auto srcPin = graph->_pins.at(itemCreator.m_DraggedPin->m_ID);
              auto dstPin = chPin;
              if (srcPin->_kind == pin_kind_e::Input &&
                  dstPin->_kind == pin_kind_e::Output)
                zs_swap(srcPin, dstPin);
              graph->spawnLink(srcPin, dstPin);
            }
          }
        });

      } else if (itemCreator.m_DraggedPin != nullptr) {

        auto drawList = ImGui::GetWindowDrawList();
        auto color = editorStyle.Colors[ed::StyleColor_SelNodeBorder];

        drawList->AddRectFilled(
            ImVec2(appendixZoneStartX, appendixZoneStartY),
            ImVec2(appendixZoneEndX,
                   appendixZoneStartY + ImGui::GetStyle().ItemSpacing.y * 2),
            ImGui::ColorConvertFloat4ToU32(
                editorStyle.Colors[ed::StyleColor_Flow]),
            editorStyle.PinRounding,
            ImDrawFlags_RoundCornersAll); // , editorStyle.NodeBorderWidth
      }
    }
  }

  ImGui::EndGroup();

#if 0
  if (hasContents()) {
    ed::Suspend();
    // ImGui::SetItemTooltip((*_content).data());
    ed::Resume();
  }
#endif

  ImGui::PopID();

  ed::PopStyleVar(1);
}
Pin::~Pin() {
  // _node->_graph->_pins[id];//erase
  _node->_graph->removePinLinks(_id);
  _node->_graph->_pins.erase(_id);
}

Pin *Pin::prepend(std::string_view label) {
  auto id = _node->_graph->nextPinId();
  return prepend(id, label);
}
Pin *Pin::prepend(const ed::PinId &id, std::string_view label) {
  auto ret =
      _chs.emplace(std::begin(_chs), id, _node, label, _type, _kind, this);
  return _node->_graph->_pins[id] = zs::addressof(*ret);
}

Pin *Pin::append(std::string_view label) {
  auto id = _node->_graph->nextPinId();
  return append(id, label);
}
Pin *Pin::append(const ed::PinId &id, std::string_view label) {
  _chs.emplace_back(id, _node, label, _type, _kind, this);
  return _node->_graph->_pins[id] = &_chs.back();
}

Pin *Pin::append(const Pin *target, std::string_view label) {
  auto id = _node->_graph->nextPinId();
  return append(target, id, label);
}
Pin *Pin::append(const Pin *target, const ed::PinId &id,
                 std::string_view label) {
  auto it = std::begin(_chs);
  for (; it != std::end(_chs); ++it)
    if (zs::addressof(*it) == target)
      break;
  if (it != std::end(_chs))
    it++;
  auto ret = _chs.emplace(it, id, _node, label, _type, _kind, this);
  return _node->_graph->_pins[id] = zs::addressof(*ret);
}

void Pin::drawLabelText() {
  ImGui::Text(_name.c_str());

  /// @note should only editable if parent pin is dict type!
  if (ImGui::IsItemClicked()) {
    if (ImGui::GetMouseClickedCount(ImGuiMouseButton_Left) == 2) {
      auto popupTag = fmt::format("gepin_{}", _id.Get());
      /// @note one-time action
      _node->_graph->enqueueMaintenanceAction([this, popupTag] {
        ed::Suspend();
        ImGui::OpenPopup(popupTag.c_str());
        ed::Resume();
      });
      /// @note persistant auxiliary widget action
      ed::Suspend();
      auto popupId = ImGui::GetID(popupTag.c_str());
      _node->_graph->setupAuxiliaryWidgetAction(
          popupId,
          [this, popupTag, popupId, newName = std::string(_name)]() mutable {
            ed::Suspend();
            if (ImGui::BeginPopup(popupTag.c_str())) {
              if (ImGui::InputText("##edit", &newName,
                                   ImGuiInputTextFlags_EnterReturnsTrue |
                                       ImGuiInputTextFlags_AutoSelectAll)) {
                _name = newName;
                ImGui::CloseCurrentPopup();
                _node->_graph->enqueueMaintenanceAction(
                    [graph = _node->_graph, popupId] {
                      graph->clearAuxiliaryWidgetAction(popupId);
                    });
              }
              ImGui::SetKeyboardFocusHere(-1);
              ImGui::EndPopup();
            }
            ed::Resume();
          });
      ed::Resume();
      // fmt::print("tag clicked: {}\n", popupTag);
    }
  }
}

bool Pin::removeChildPin(const Pin *pin) {
  for (auto it = std::begin(_chs); it != std::end(_chs); ++it) {
    if (zs::addressof(*it) == pin) {
      _chs.erase(it);
      return true;
    } else if ((*it).removeChildPin(pin)) {
      return true;
    }
  }
  return false;
}

bool Pin::isPinLinked() const {
  if (_links.size() > 0)
    return true;
  if (expanded())
    return false;
  for (auto &ch : _chs)
    if (ch.isPinLinked())
      return true;
  return false;
}

float Pin::evaluatePinWidth(float offset) const {
  auto inherentSz =
      offset + get_pin_icon_size() +
      std::max(get_pin_icon_size() * 2, ImGui::CalcTextSize(_name.c_str()).x) +
      ImGui::GetStyle().ItemSpacing.x * 2;
  auto sz = inherentSz;
  if (editable()) {
    auto contentWidth = get_min_content_size();

    inherentSz += contentWidth + ImGui::GetStyle().ItemSpacing.x +
                  ImGui::GetStyle().ItemInnerSpacing.x;

    auto contentSz =
        ImGui::GetStyle().ItemInnerSpacing.x + _contentWidget.preferredWidth();
    contentSz += ImGui::CalcTextSize(" ").x;
    if (contentSz > contentWidth)
      contentWidth = contentSz;

    sz += contentWidth + ImGui::GetStyle().ItemSpacing.x +
          ImGui::GetStyle().ItemInnerSpacing.x;
  }
  if (expandable()) {
    inherentSz += ImGui::CalcTextSize("+").x + ImGui::GetStyle().ItemSpacing.x;
    sz += ImGui::CalcTextSize("+").x + ImGui::GetStyle().ItemSpacing.x;
    if (expanded()) {
      offset = get_pin_icon_size() + ImGui::GetStyle().ItemSpacing.x;
      for (auto &ch : _chs) {
        auto pinSz = ch.evaluatePinWidth(offset);
        if (pinSz > sz)
          sz = pinSz;
      }
    }
  }
  /// @note at most (get_min_content_size() * 3) width for content display
  if (inherentSz + get_min_content_size() * 5 < sz)
    sz = inherentSz + get_min_content_size() * 5;
  return sz;
}

void Pin::setupContentAndWidget(ZsDict desc) {
  if (contents()) { // check validity of the inserted ZsVar
    fmt::print("pin contents already initialized.\n");
  } else {
    auto &obj = contents();
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
          l.asList().appendSteal(
              zs_string_obj_cstr(options[i].c_str()));

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

        if (obj)
          setupContentWidget(l.asList());
      } else {
        assert(0 && "enum without an initial pick is not allowed!");
      }
    }
    /// int
    else if (ts == "int") {
      obj = zs::zs_int_descriptor(desc["defl"]);
      if (obj)
        setupContentWidget();
    }
    /// float
    else if (ts == "float") {
      obj = zs::zs_float_descriptor(desc["defl"]);
      if (obj)
        setupContentWidget();
    }
    /// string
    else if (ts == "string" || ts == "str") {
      obj = zs::zs_string_descriptor(desc["defl"]);
      if (obj)
        setupContentWidget();
    }
    /// list
    else if (ts == "list") {
      obj = zs::zs_list_descriptor(desc["defl"]);
      if (obj)
        setupContentWidget();
    }
    /// dict
    else if (ts == "dict") {
      obj = zs::zs_dict_descriptor(desc["defl"]);
      if (obj)
        setupContentWidget();
    }
    /// wildcard
    else {
      ;
    }
  }
}

zs::ui::GenericResourceWidget &Pin::setupContentWidget(ZsValue aux) {
  if (!aux)
    _contentWidget = zs::ui::GenericResourceWidget(_name, contents(), aux);
#if 0
  if (aux) {
    _contentWidget.injectCustomBehavior(
        zs_i64(0),
        [ed = _node->_graph->getEditorContext()]() mutable { ed->Suspend(); });
    _contentWidget.injectCustomBehavior(
        zs_i64(1),
        [ed = _node->_graph->getEditorContext()]() mutable { ed->Resume(); });
  }
#elif 1
  if (aux) {
    _contentWidget = zs::ui::make_generic_resource_widget<zs::ui::ComboSlider>(
        _name, contents(), aux);
  }
#endif
  return _contentWidget;
}

IconType Pin::getIconType() const {
  switch (_type) {
  case pin_type_e::Flow:
    return IconType::Flow;

  case pin_type_e::Bool:
  case pin_type_e::Int:
  case pin_type_e::Float:
  case pin_type_e::String:
    return IconType::Circle;

  case pin_type_e::List:
  case pin_type_e::Dict:
    return IconType::RoundSquare;
  case pin_type_e::Object:
    return IconType::Diamond; // wild-card

  case pin_type_e::Function:
    return IconType::Grid;

  default:
    return IconType::Diamond;
  }
}
ImColor Pin::getIconColor() const {
  switch (_type) {
  case pin_type_e::Bool:
  case pin_type_e::Int:
    return ImColor(173, 216, 230, 255); // light blue
  case pin_type_e::Float:
    return ImColor(255, 255, 237, 255); // light yellow
  case pin_type_e::String:
    return ImColor(144, 238, 144, 255); // light green

  case pin_type_e::List:
  case pin_type_e::Dict:
    return ImColor(255, 219, 187, 255); // light orange

  case pin_type_e::Function:
    return ImColor(255, 127, 127, 255); // light orange

  default:
    return ImColor(255, 255, 255, 255);
  }
}

void swap(Pin &a, Pin &b) noexcept {
  zs_swap(a._id, b._id);
  zs_swap(a._node, b._node);
  zs_swap(a._name, b._name);
  zs_swap(a._expanded, b._expanded);
  zs_swap(a._type, b._type);
  zs_swap(a._kind, b._kind);
  zs_swap(a._links, b._links);
  zs_swap(a._chs, b._chs);
  zs_swap(a._parent, b._parent);
  zs_swap(a._visible, b._visible);
  zs_swap(a._contents, b._contents);
  zs_swap(a._contentWidget, b._contentWidget);
}

} // namespace ge

} // namespace zs

/*
    if (ImGui::BeginTable(
            _name.c_str(), 1,
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX |
                ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX |
                ImGuiTableFlags_NoBordersInBody)) {
      ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                          ImVec2(ImGui::GetStyle().CellPadding.x, 0));
      ImGui::TableNextRow(ImGuiTableRowFlags_None);
      ImGui::TableNextColumn();

        ImGui::PopStyleVar();
        ImGui::EndTable();
      }

      */