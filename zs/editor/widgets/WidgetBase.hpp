#pragma once
#include <optional>

#include "WidgetComponent.hpp"
#include "WidgetDrawUtilities.hpp"
#include "imgui.h"
#include "zensim/types/Polymorphism.h"
#include "zensim/ui/Widget.hpp"
//
#include "imgui_internal.h"
namespace zs {

  // ImGuiDir_ dir; // ImGuiDir_None/Left/Right/Up/Down
  enum widget_split_direction_e {
    widget_split_none = ImGuiDir_None,
    widget_split_left = ImGuiDir_Left,
    widget_split_right = ImGuiDir_Right,
    widget_split_up = ImGuiDir_Up,
    widget_split_down = ImGuiDir_Down
  };

  struct DockingLayoutNode : HierarchyConcept {
    struct SplitOp {
      ImGuiID nodeId;  // nodeid during split
      float ratio;
      ImGuiDir dir;  // ImGuiDir_None/Left/Right/Up/Down
    };
    DockingLayoutNode(ImGuiID id = -1, DockingLayoutNode *par = nullptr) : _id0{id}, _id{id} {
      static_cast<HierarchyConcept *>(this)->parent() = par;
    }
    DockingLayoutNode(DockingLayoutNode &&) noexcept = default;
    DockingLayoutNode &operator=(DockingLayoutNode &&) noexcept = default;
    ~DockingLayoutNode() = default;

    void clear() {
      for (auto &ch : _childDockingNodes) ch.clear();
      _childDockingNodes.clear();
      _childSplits.clear();
      _dockedWidgets.clear();
    }
    ImGuiID split(ImGuiID id0, float ratio, widget_split_direction_e dir) {
      if (id0 == _id0) {
        // record
        _childSplits.push_back(SplitOp{id(), ratio, (ImGuiDir)dir});
        // operate
        ImGuiID child = ImGui::DockBuilderSplitNode(id(), (ImGuiDir)dir, ratio, NULL, &_id);
        _childDockingNodes.emplace_back(child, this);
        return child;
      } else {
        for (auto &ch : _childDockingNodes) return ch.split(id0, ratio, dir);
      }
      return -1;
    }
    bool dockWidget(std::string_view name, ImGuiID dockId0) {
      if (dockId0 == _id0) {
        _dockedWidgets.push_back(std::string(name));
        ImGui::DockBuilderDockWindow(name.data(), id());  // not initialId here
        return true;
      } else {
        for (auto &ch : _childDockingNodes)
          if (ch.dockWidget(name, dockId0)) return true;
      }
      return false;
    }

    ImGuiID initialId() const noexcept { return _id0; }
    ImGuiID id() const noexcept { return _id; }
    ImGuiID &id() noexcept { return _id; }

    std::vector<DockingLayoutNode> _childDockingNodes;
    std::vector<SplitOp> _childSplits;
    std::vector<std::string> _dockedWidgets;
    ImGuiID _id0, _id;
  };

  ///
  /// widget layout builder
  ///
  struct DockingLayoutBuilder : ObjectConcept {
    static void reset(ImGuiID dockId) {
      ImGui::DockBuilderRemoveNode(dockId);
      ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    }
    static void set_placement(ImGuiID dockId, ImVec2 pos, ImVec2 extent) {
      ImGui::DockBuilderSetNodePos(dockId, pos);
      ImGui::DockBuilderSetNodeSize(dockId, extent);
    }

    DockingLayoutBuilder() : _tag{}, _root{}, _buildRoutine{} {}
    DockingLayoutBuilder(std::string_view tag, ImVec2 pos, ImVec2 extent)
        : _tag{tag}, _root{ImGui::GetID(tag.data())} {
      ImGuiID dockId = _root.initialId();
      // if (reset)
      { DockingLayoutBuilder::reset(dockId); }
      set_placement(dockId, pos, extent);
    }
    ~DockingLayoutBuilder() = default;
    DockingLayoutBuilder(DockingLayoutBuilder &&) = default;
    DockingLayoutBuilder &operator=(DockingLayoutBuilder &&) = default;

    DockingLayoutBuilder &begin(std::string_view tag, ImVec2 pos = ImGui::GetCursorScreenPos(),
                                ImVec2 extent = ImGui::GetContentRegionAvail()) {
      _tag = std::string{tag};
      _root = DockingLayoutNode{ImGui::GetID(tag.data())};
      ImGuiID dockId = _root.initialId();
      DockingLayoutBuilder::reset(dockId);
      set_placement(dockId, pos, extent);
      return *this;
    }
    DockingLayoutBuilder &begin() {
      _root = DockingLayoutNode{ImGui::GetID(_tag.data())};
      ImGuiID dockId = _root.initialId();
      DockingLayoutBuilder::reset(dockId);
      set_placement(dockId, ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail());
      return *this;
    }
    DockingLayoutBuilder &setName(std::string_view tag) {
      _tag = std::string{tag};
      return *this;
    }
    DockingLayoutBuilder &split(ImGuiID id0, float ratio, widget_split_direction_e dir,
                                ImGuiID *dockId) {
      *dockId = _root.split(id0, ratio, dir);
      return *this;
    }
    DockingLayoutBuilder &split(float ratio, widget_split_direction_e dir, ImGuiID *dockId) {
      return split(_root.initialId(), ratio, dir, dockId);
    }

    DockingLayoutBuilder &dockWidget(std::string_view name, ImGuiID dockId0) {
      if (!_root.dockWidget(name, dockId0))
        throw std::runtime_error(fmt::format("failed docking {} to {}!\n", name, dockId0));
      return *this;
    }
    DockingLayoutBuilder &dockWidget(std::string_view name) {
      return dockWidget(name, _root.initialId());
    }
    void build(DockingLayoutNode *ret = nullptr) {
      ImGui::DockBuilderFinish(_root.initialId());
      if (ret) *ret = zs::move(_root);
      return;
    }

    std::string_view dockspaceName() const noexcept { return _tag; }
    ImGuiID getDockNodeId() const { return ImGui::GetID(_tag.data()); }

    bool hasBuildRoutine() const noexcept { return static_cast<bool>(_buildRoutine); }
    void embedBuildRoutine(function<void(DockingLayoutBuilder &)> routine) {
      _buildRoutine = zs::move(routine);
    }
    void buildByRoutine() { return _buildRoutine(*this); }

    std::string _tag;
    DockingLayoutNode _root;
    function<void(DockingLayoutBuilder &)> _buildRoutine;
  };

  ///
  /// widget configs
  ///
  struct WidgetConfigs : WidgetConcept {
    WidgetConfigs &setStyle(ImGuiStyleVar_ style, float param) {
      styleConfigs.set(style, param);
      return *this;
    }
    WidgetConfigs &setStyle(ImGuiStyleVar_ style, ImVec2 param) {
      styleConfigs.set(style, param);
      return *this;
    }
    WidgetConfigs &setColor(ImGuiCol_ color, ImVec4 param) {
      colorConfigs.set(color, param);
      return *this;
    }
    WidgetConfigs &clearStyleConfigs() {
      styleConfigs.clear();
      return *this;
    }
    WidgetConfigs &clearColorConfigs() {
      colorConfigs.clear();
      return *this;
    }
    WidgetConfigs &clearConfigs() {
      clearStyleConfigs();
      clearColorConfigs();
      return *this;
    }

    void push() const {
      styleConfigs.push();
      colorConfigs.push();
    }
    void pop() const {
      colorConfigs.pop();
      styleConfigs.pop();
    };
    WidgetStyle styleConfigs;
    WidgetColorStyle colorConfigs;
  };
  struct WindowWidgetNode : WidgetConfigs {
    struct WidgetDescriptor {
      GenericWidgetElement widget;
    };

    WindowWidgetNode(std::string_view tag = "DefaultWidget", HierarchyConcept *parent = nullptr,
                     ImGuiWindowFlags flags
                     = ImGuiWindowFlags_None | ImGuiWindowFlags_HorizontalScrollbar,
                     bool allowClose = false)
        : name{tag},
          flags{flags},
          topLevel{false},
          openStatus{},
          dockSpaceNeedRebuild{false},
          layoutBuilder{} {
      layoutBuilder.setName(name + "_dockspace");
      static_cast<HierarchyConcept *>(this)->parent() = parent;
      if (allowClose == true) openStatus = true;
    }
    ~WindowWidgetNode() = default;
    WindowWidgetNode(WindowWidgetNode &&o) = default;
    WindowWidgetNode &operator=(WindowWidgetNode &&o) = default;

    void paint() override {
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

        /// draw components and subwindows
        for (auto &comp : components) {
          match([](Shared<WidgetConcept> &widget) { widget->paint(); },
                [](Shared<WidgetComponentConcept> &widgetComponent) { widgetComponent->paint(); })(
              comp.widget);
        }

        if (!topLevel) ImGui::EndChild();
      } else
        isFirstFrame = false;

      ImGui::End();
    }
    /// manage components
    template <typename ChildT> void appendComponent(ChildT &&widget) {
      components.push_back(WidgetDescriptor{
          Shared<WidgetComponentConcept>{zs::make_shared<remove_cvref_t<ChildT>>(FWD(widget))}});
    }
    template <typename ChildT, enable_if_t<is_base_of_v<WidgetComponentConcept, ChildT>> = 0>
    void appendComponent(Shared<ChildT> &&widget) {
      components.push_back(WidgetDescriptor{Shared<WidgetComponentConcept>{FWD(widget)}});
    }
    template <typename ChildT, enable_if_t<is_base_of_v<WidgetComponentConcept, ChildT>> = 0>
    void appendComponent(ChildT *widget) {
      components.push_back(WidgetDescriptor{Shared<ChildT>{widget}});
    }
    void setMenuComponent(MenuBarWidgetComponent &&w) { menu = zs::move(w); }

    /// layout
    void requireLayoutRebuild() noexcept { dockSpaceNeedRebuild = true; }
    tuple<ImVec2, ImVec2> queryRegion() const {
      ImGui::Begin(name.data(), nullptr, flags);

      auto pos = ImGui::GetCursorScreenPos();
      auto size = ImGui::GetContentRegionAvail();
      ImGui::End();
      return zs::make_tuple(pos, size);
    }

    template <typename F>
    enable_if_type<is_invocable_r_v<F, void, DockingLayoutBuilder &>, void> setDockSpaceBuilder(
        F &&f) {
      layoutBuilder.embedBuildRoutine(FWD(f));
    }

    /// manage states
    void setTopLevel() { topLevel = true; }
    void setUnsaved() { flags |= ImGuiWindowFlags_UnsavedDocument; }
    void setSaved() { flags &= ~ImGuiWindowFlags_UnsavedDocument; }
    // void placeAt(u32 layoutNodeId) override {}

    /// data members
    // id
    std::string name;
    // WidgetConfigs (styles/ configs)/ flags
    ImGuiWindowFlags flags;
    // layout
    bool dockSpaceNeedRebuild;
    DockingLayoutBuilder layoutBuilder;
    // contents
    std::optional<MenuBarWidgetComponent> menu;
    std::vector<WidgetDescriptor> components;
    // states
    bool topLevel;
    bool isFirstFrame{true};
    std::optional<bool> openStatus;
  };

}  // namespace zs