#include "WidgetComponent.hpp"

#include "WidgetBase.hpp"
//
#include "IconsMaterialDesign.h"
#include "IconsMaterialDesignIcons.h"
#include "IconsMaterialSymbols.h"

namespace zs {

  /// GuiEventHub
  GuiEventHub::~GuiEventHub() {
    if (_ownQueue) {
      delete _msgQueue;
      _ownQueue = false;
    }
    _msgQueue = nullptr;
  }

  GuiEventHub &GuiEventHub::operator=(GuiEventHub &&o) {
    if (_ownQueue) delete _msgQueue;
    _msgQueue = zs::exchange(o._msgQueue, nullptr);
    _ownQueue = zs::exchange(o._ownQueue, false);
    return *this;
  }

  /// AssetEntry
  const char *AssetEntry::getDisplayLabel() const {
    const char *label = (const char *)ICON_MDI_FILE;
    switch (_type) {
      case type_e::text_:
        label = (const char *)ICON_MD_SOURCE;
        break;
      case type_e::usd_:
        label = (const char *)ICON_MS_SCENE;
        break;
      default:
        break;
    }
    return label;
  }

  /// WidgetNode
  void WidgetNode::updateWindowUIStates(WindowWidgetNode *p) {
    assert(p);
    _focused = p->windowFocused();
    _hovered = p->windowHovered();
    _appearing = p->windowAppearing();
    _collapsed = p->windowCollapsed();
    _docked = p->windowDocked();
  }

  void WidgetNode::updateItemUIStates() {
    _itemHovered = ImGui::IsItemHovered();
    _itemFocused = ImGui::IsItemFocused();
    _itemActive = ImGui::IsItemActive();
    _itemActivated = ImGui::IsItemActivated();
    _itemDeactivated = ImGui::IsItemDeactivated();
    for (int i = 0; i < ImGuiMouseButton_COUNT; ++i) _itemClicked[i] = ImGui::IsItemClicked(i);
    _itemVisible = ImGui::IsItemVisible();
    _itemEdited = ImGui::IsItemEdited();

    _itemID = ImGui::GetItemID();
    _itemRectMin = ImGui::GetItemRectMin();
    _itemRectMax = ImGui::GetItemRectMax();
    _itemRectSize = ImGui::GetItemRectSize();
  }
  /// WindowWidgetNode
  void WindowWidgetNode::updateWindowUIStates() {
    _focused = ImGui::IsWindowFocused();
    _hovered = ImGui::IsWindowHovered();
    _appearing = ImGui::IsWindowAppearing();
    _collapsed = ImGui::IsWindowCollapsed();
    _docked = ImGui::IsWindowDocked();

#if 0
    // iterate
    for (auto &comp : components) {
      match([this](WidgetNode &widgetComponent) { widgetComponent.updateWindowUIStates(this); },
            [](Shared<WindowWidgetNode> &windowNode) { windowNode->updateWindowUIStates(); })(
          comp._widget);
    }
#endif
  }
  void WidgetNode::paint() {
    _widget->paint();

    updateItemUIStates();

    if (itemActivated())
      _widget->onActivated();
    else if (itemDeactivated())
      _widget->onDeactivated();
    if (itemActive()) _widget->onActive();
    if (itemVisible()) _widget->onVisible();
    if (itemFocused()) _widget->onFocused();
    if (itemHovered()) _widget->onHovered();
    for (int i = 0; i < ImGuiMouseButton_COUNT; ++i)
      if (itemClicked(i)) _widget->onMouseButtonClicked(i);
    if (itemEdited()) _widget->onEdited();
  }
  void WindowWidgetNode::paint() {
    if (topLevel) {
      ImGuiViewport *viewport = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(viewport->Pos);
      ImGui::SetNextWindowSize(viewport->Size);
      ImGui::SetNextWindowViewport(viewport->ID);
    }
    push();
    bool windowOpened = false;
    if (openStatus) {
      bool &allowClose = *openStatus;
      if (allowClose == false) {
        pop();
        return;
      }
      windowOpened = ImGui::Begin(name.data(), &allowClose, flags);
    } else
      windowOpened = ImGui::Begin(name.data(), nullptr, flags);
    pop();

    /// dockspace
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) {
      auto dockId = ImGui::GetID(layoutBuilder.dockspaceName().data());
      if ((!ImGui::DockBuilderGetNode(dockId) || dockSpaceNeedRebuild)
          && layoutBuilder.hasBuildRoutine()) {
        dockSpaceNeedRebuild = false;
        layoutBuilder.buildByRoutine();
      }
      if (ImGui::DockBuilderGetNode(dockId)) {
        ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
      }
    }

    if (!windowOpened) {
      ImGui::End();
      return;
    }

    if (!isFirstFrame) {
      if (menu) {
        (*menu).paint();
      }

      // ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
      // ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(105, 105, 105, 255));
      // ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(43, 43, 43, 255));
      ImGui::PushStyleColor(ImGuiCol_ChildBg, g_almost_dark_color);
      // ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(43, 43, 43, 255));
      if (!topLevel)
        ImGui::BeginChild("#root_child", ImGui::GetContentRegionAvail(), ImGuiChildFlags_None);
      ImGui::PopStyleColor();

      updateWindowUIStates();

      /// draw components and subwindows
      for (auto &comp : components) {
        match(
            [this](WidgetNode &widgetComponent) {
              widgetComponent.updateWindowUIStates(this);
              widgetComponent.paint();
            },
            [](Shared<WindowWidgetNode> &windowNode) { windowNode->paint(); })(comp._widget);
      }

      if (!topLevel) ImGui::EndChild();
    } else
      isFirstFrame = false;

    ImGui::End();
  }

}  // namespace zs