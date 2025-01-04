#pragma once
#include <stdexcept>
#include <string>
#include <variant>

#include "WidgetEvent.hpp"
#include "editor/ImguiSystem.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "zensim/ZpcFunction.hpp"
#include "zensim/ZpcImplPattern.hpp"
#include "zensim/types/ImplPattern.hpp"
#include "zensim/ui/Widget.hpp"
#include "zensim/zpc_tpls/fmt/core.h"
#include "zensim/zpc_tpls/moodycamel/concurrent_queue/concurrentqueue.h"

namespace zs {

  struct WidgetId {
    const void *id{nullptr};
    bool isStr{false};

    WidgetId() noexcept = default;
    template <typename Tn, enable_if_t<is_integral_v<Tn>> = 0> WidgetId(Tn id) noexcept
        : id{(const void *)(intptr_t)id}, isStr{false} {}
    WidgetId(const void *id) noexcept : id{id}, isStr{false} {}
    WidgetId(const char *id) noexcept : id{id}, isStr{true} {}
    ~WidgetId() noexcept = default;
    WidgetId(WidgetId &&o) noexcept = default;
    WidgetId(const WidgetId &o) noexcept = default;
    WidgetId &operator=(WidgetId &&o) noexcept = default;
    WidgetId &operator=(const WidgetId &o) noexcept = default;

    operator const void *() const noexcept { return id; }
    explicit operator ImGuiID() const noexcept { return ImGui::GetCurrentWindow()->GetID(id); }
  };
  /// @brief for receiving/issuing/processing events
  using GuiEventQueue = moodycamel::ConcurrentQueue<GuiEvent *>;
  struct GuiEventHub {
    GuiEventHub() = default;
    ~GuiEventHub();
    GuiEventHub(GuiEventHub &&o) noexcept
        : _msgQueue{zs::exchange(o._msgQueue, nullptr)},
          _ownQueue{zs::exchange(o._ownQueue, false)} {}
    GuiEventHub &operator=(GuiEventHub &&o);
    GuiEventHub(const GuiEventHub &) = delete;
    GuiEventHub &operator=(const GuiEventHub &) = delete;

    GuiEventQueue *getMessageQueue() noexcept { return _msgQueue; }
    void connectMessageQueue(GuiEventQueue *q) {
      if (_ownQueue) delete _msgQueue;
      _msgQueue = q;
      _ownQueue = false;
    }
    void setupMessageQueue() {
      if (_ownQueue) delete _msgQueue;
      _msgQueue = new GuiEventQueue;
      _ownQueue = true;
    }
    void postEvents(WidgetConcept *receiver, std::vector<GuiEvent *> *pUnhandled = nullptr) {
      assert(_msgQueue && receiver);
      auto &q = *_msgQueue;
      std::array<GuiEvent *, 512> evs;
      while (auto n = q.try_dequeue_bulk(evs.begin(), 512)) {
        for (int i = 0; i < n; ++i) {
          receiver->onEvent(evs[i]);
        }
        for (int i = 0; i < n; ++i) {
          if (evs[i]->isAccepted() || !pUnhandled)
            delete evs[i];
          else if (pUnhandled) {
            pUnhandled->push_back(evs[i]);
          }
        }
      }
    }

    template <typename E, enable_if_t<is_base_of_v<GuiEvent, remove_cv_t<E>>> = 0>
    void addEvent(E *e) {
      assert(_msgQueue);
      _msgQueue->enqueue(e);
    }

  protected:
    GuiEventQueue *_msgQueue{nullptr};
    bool _ownQueue{false};
  };
  struct WidgetBase {
    ;
    ;
    // properties
    // status
    bool _retainedAlike{false};
    bool _mouseTracking{false};  // tracking mouse move
    bool _focused{false}, _hovered{true};
  };
  struct WidgetNode : WidgetBase, WidgetConcept {
    WidgetNode() = default;
    WidgetNode(Shared<WidgetConcept> widget, WidgetConcept *p = nullptr) : _widget{widget} {
      setParent(p);
    }

    void paint() override { _widget->paint(); }
    auto &refWidget() noexcept { return _widget; }
    const auto &refWidget() const noexcept { return _widget; }

    template <typename T> auto widget() { return std::dynamic_pointer_cast<T>(_widget); }
    template <typename T> auto widget() const {
      return std::dynamic_pointer_cast<const T>(_widget);
    }

    bool onEvent(GuiEvent *e) { return _widget->onEvent(e); }
    gui_widget_e getWidgetType() const { return _widget->getWidgetType(); }

  private:
    // certain compound widget can have multiple child widgets, managed internally
    Shared<WidgetConcept> _widget;
  };

  struct IDGenerator {
    ImGuiID nextId() noexcept {
      if (_recycled.size()) {
        auto ret = _recycled.back();
        _recycled.pop_back();
        return ret;
      }
      return _curId++;
    }
    void recycleId(ImGuiID id) { _recycled.push_back(id); }

    ImGuiID _curId{1};
    std::vector<ImGuiID> _recycled{};
  };
  template <typename T = void> struct TabEntry {
    static_assert(is_default_constructible_v<T>, "T should be default_constructible.");
    TabEntry(std::string_view label, ImGuiID id) : _label{label}, _id{id}, _visible{true} {}

    std::string_view getLabel() const noexcept { return _label; }
    ImGuiID getId() const noexcept { return _id; }
    operator T &() noexcept { return _content; }
    T &content() noexcept { return _content; }

    std::string _label;
    T _content;
    ImGuiID _id;
    bool _visible;
  };
  template <typename T> struct TabEntries {
    int tabOpened(std::string_view label) const noexcept {
      int idx = -1;
      for (int i = 0; i < _openedTabs.size(); ++i)
        if (_openedTabs[i]._visible && _openedTabs[i]._label == label) {
          idx = i;
          break;
        }
      return idx;
    }
    /// @note maintain [id, callbacks(insertion/removal), focusid]
    void appendTab(std::string_view label) {
      ImGuiID id = _idGenerator.nextId();
      _openedTabs.emplace_back(label, id);
      _onInsertion(label, _openedTabs.back());
      _focusIdx = _openedTabs.size() - 1;
    }
    typename std::vector<TabEntry<T>>::iterator removeTab(
        typename std::vector<TabEntry<T>>::iterator it) {
      auto &entry = *it;
      _onRemoval(entry.getLabel(), entry);
      _idGenerator.recycleId(entry.getId());
      if ((it - _openedTabs.begin()) == _focusIdx) {
        if (_openedTabs.size() > 1)
          _focusIdx = 0;
        else
          _focusIdx = -1;
      } else if ((it - _openedTabs.begin()) < _focusIdx)
        _focusIdx--;
      return _openedTabs.erase(it);
    }
    bool removeTab(std::string_view label) {
      auto idx = tabOpened(label);
      if (idx == -1) return false;
      removeTab(_openedTabs.begin() + idx);
      return true;
    }
    ~TabEntries() {
      auto it = _openedTabs.begin();
      while (it != _openedTabs.end()) {
        it = removeTab(it);
      }
    }

    auto size() const noexcept { return _openedTabs.size(); }
    TabEntry<T> &operator[](int i) {
      assert(i >= 0 && i < _openedTabs.size());
      return _openedTabs[i];
    }
    const TabEntry<T> &operator[](int i) const {
      assert(i >= 0 && i < _openedTabs.size());
      return _openedTabs[i];
    }
    inline void paint();
    int getFocusId() const noexcept { return _focusIdx; }

    template <typename F> void setInsertionCallback(F &&f) { _onInsertion = FWD(f); }
    template <typename F> void setRemovalCallback(F &&f) { _onRemoval = FWD(f); }

    template <typename F> void setIsValidPredicate(F &&f) { _isValid = FWD(f); }
    /// @note for unsaved dot display
    template <typename F> void setIsModifiedPredicate(F &&f) { _isModified = FWD(f); }
    template <typename F> void setSaveCallback(F &&f) { _save = FWD(f); }
    template <typename F> void setItemListCallback(F &&f) { _availableItems = FWD(f); }
    void setLeadingSymbolLiteral(const char *s) noexcept { _leadingSymbolLiteral = s; }
    T *getEntryContent(std::string_view label) {
      for (auto &tab : _openedTabs)
        if (tab.getLabel() == label) return &tab.content();
      return nullptr;
    }

  protected:
    zs::function<void(std::string_view, T &)> _onInsertion, _onRemoval;
    zs::function<bool(std::string_view)> _isValid;
    zs::function<bool(std::string_view)> _isModified;
    zs::function<void(std::string_view)> _save;
    zs::function<std::vector<std::string>()> _availableItems;  // "+"

    std::vector<TabEntry<T>> _openedTabs;
    int _focusIdx{-1};
    const char *_leadingSymbolLiteral{"~"};
    IDGenerator _idGenerator;
  };

  struct WidgetStyle : ObjectConcept {
    void set(ImGuiStyleVar_ style, float param) { configs[style] = param; }
    void set(ImGuiStyleVar_ style, ImVec2 param) { configs[style] = param; }
    void clear() { configs.clear(); }

    void push() const {
      for (const auto &[style, param] : configs) {
        switch (style) {
            // ImVec2
          case ImGuiStyleVar_WindowPadding:
          case ImGuiStyleVar_WindowMinSize:
          case ImGuiStyleVar_WindowTitleAlign:
          case ImGuiStyleVar_FramePadding:
          case ImGuiStyleVar_ItemSpacing:
          case ImGuiStyleVar_ItemInnerSpacing:
          case ImGuiStyleVar_CellPadding:
          case ImGuiStyleVar_ButtonTextAlign:
          case ImGuiStyleVar_SelectableTextAlign:
          case ImGuiStyleVar_SeparatorTextAlign:
          case ImGuiStyleVar_SeparatorTextPadding:
            ImGui::PushStyleVar(style, std::get<ImVec2>(param));
            break;
            // float
          default:
            ImGui::PushStyleVar(style, std::get<float>(param));
        }
      }
    }
    void pop() const {
      if (auto n = configs.size(); n > 0) ImGui::PopStyleVar(n);
    }

    std::map<ImGuiStyleVar_, std::variant<float, ImVec2>> configs;
  };
  struct WidgetColorStyle : ObjectConcept {
    void set(ImGuiCol_ color, ImVec4 param) { configs[color] = param; }
    void clear() { configs.clear(); }

    void push() const {
      for (const auto &[color, param] : configs) {
        ImGui::PushStyleColor(color, param);
      }
    }
    void pop() const {
      if (auto n = configs.size(); n > 0) ImGui::PopStyleColor(n);
    }

    std::map<ImGuiCol_, ImVec4> configs;
  };

  struct AssetEntry {
    enum type_e : int { unknown_ = 0, text_, usd_, texture_ };

    std::string _tag{};
    type_e _type{unknown_};

    AssetEntry(std::string tag, ImGuiID id, type_e type = unknown_) noexcept
        : _tag{tag}, _id{id}, _type{type} {}

    const char *getTag() const noexcept { return _tag.c_str(); }
    std::string_view getLabel() const noexcept { return _tag; }
    ImGuiID getId() { return _id; }
    type_e getType() const noexcept { return _type; }
    /// @note used for Selectable display
    const char *getDisplayLabel() const;

  protected:
    ImGuiID _id{(ImGuiID)-1};
  };

  struct ActionWidgetComponent : WidgetConcept {
    ActionWidgetComponent() = default;
    ~ActionWidgetComponent() = default;
    template <typename F> ActionWidgetComponent(F &&f) : _aggregate{FWD(f)} {}

    ActionWidgetComponent(ActionWidgetComponent &&) = default;
    ActionWidgetComponent(const ActionWidgetComponent &) = default;
    ActionWidgetComponent &operator=(ActionWidgetComponent &&) = default;
    ActionWidgetComponent &operator=(const ActionWidgetComponent &) = default;

    void paint() override { _aggregate(); }

    zs::function<void()> _aggregate;
  };

  template <typename Arg> static auto transform_argument(Arg arg) {
    if constexpr (is_lvalue_reference_v<Arg>)
      return &arg;
    else if constexpr (is_pointer_v<remove_reference_t<Arg>>)
      return (remove_reference_t<Arg>)arg;
    else
      return zs::forward<Arg>(arg);
  }
  template <typename Arg> static decltype(auto) dereference_argument(Arg &&arg) {
    if constexpr (is_pointer_v<remove_reference_t<Arg>>) {
      if constexpr (is_same_v<Arg, void *> || is_same_v<Arg, void *&>)
        return (std::uintptr_t)arg;
      else if constexpr (is_pointer_v<RM_REF_T(*arg)>)
        return dereference_argument(*arg);
      else
        return *arg;
    } else
      return FWD(arg);
  }
  template <size_t... Is, typename... Args>
  static auto text_paint_callable(std::string_view templ, index_sequence<Is...>, Args &&...args) {
    return [templ = std::string(templ), as = make_tuple(transform_argument<Args>(args)...)]() {
      ImGui::Text(templ.data(), dereference_argument(get<Is>(as))...);
    };
  }

  struct TreeWidgetComponent;
  template <size_t... Is, typename... Args>
  static function<bool(const TreeWidgetComponent &)> treenode_paint_callable(WidgetId id,
                                                                             std::string_view templ,
                                                                             index_sequence<Is...>,
                                                                             Args &&...args);

  struct TextWidgetComponent : ActionWidgetComponent {
    /// @note for pointer variable, use move to enforce rvalue ref if examining
    /// its address is expected
    template <typename... Args> TextWidgetComponent(std::string_view templ = "", Args &&...args)
        : ActionWidgetComponent(
              text_paint_callable(templ, index_sequence_for<Args...>{}, FWD(args)...)) {}
    ~TextWidgetComponent() = default;
  };

  struct ButtonWidgetComponent : WidgetConcept {
    enum button_variant_e { normal = 0, little, invisible, arrow, close, collapse };

    template <typename Func>
    ButtonWidgetComponent(std::string_view label, Func &&func,
                          button_variant_e category = button_variant_e::normal)
        : _label{label}, _callback{FWD(func)}, _variant{category} {}

    void paint() override {
      switch (_variant) {
        case normal:
          if (ImGui::Button(_label.data())) _callback();
          break;
        case little:
          if (ImGui::SmallButton(_label.data())) _callback();
          break;
        default:
          break;
      };
    }

    std::string _label;
    zs::function<void()> _callback;
    button_variant_e _variant;
  };

#if 0
struct ImageWidgetComponent : WidgetConcept {
  ImageWidgetComponent(const vk::DescriptorSet &imgSet, const ImVec2 &size)
      : _imageSet{imgSet}, _size{size} {}

  void paint() override {
    ImGui::Image((ImU64) reinterpret_cast<const VkDescriptorSet *>(&_imageSet),
                 _size);
  }

  const vk::DescriptorSet &_imageSet;
  const ImVec2 &_size;
};
#endif

  struct CheckboxWidgetComponent : WidgetConcept {
    CheckboxWidgetComponent(std::string_view label, bool &flag) : _label{label}, _switch{flag} {}
    void paint() override { ImGui::Checkbox(_label.data(), &_switch); }

    std::string _label;
    bool &_switch;
  };

  template <typename T = float, size_t N = 1> struct SliderWidgetComponent : WidgetConcept {
    static_assert(is_arithmetic_v<T>, "T should be an arithmetic type.");
    SliderWidgetComponent(std::string_view label, T *vals = nullptr, float speed = 1.f, T mi = 0,
                          T ma = 0,
                          const char *format = is_floating_point_v<T> ? "%.3f"
                                               : is_integral_v<T>     ? "%d"
                                                                      : "%llx",
                          ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp)
        : _label{label},
          _vals{vals},
          _speed{speed},
          _min{mi},
          _max{ma},
          _format{format},
          _flags{flags} {}
    void paint() override {
      if constexpr (is_floating_point_v<T>) {
        if constexpr (N == 1)
          ImGui::DragFloat(_label.data(), _vals, _speed, _min, _max, _format, _flags);
        else if constexpr (N == 2)
          ImGui::DragFloat2(_label.data(), _vals, _speed, _min, _max, _format, _flags);
        else if constexpr (N == 3)
          ImGui::DragFloat3(_label.data(), _vals, _speed, _min, _max, _format, _flags);
        else if constexpr (N == 4)
          ImGui::DragFloat4(_label.data(), _vals, _speed, _min, _max, _format, _flags);
        else
          static_assert(always_false<T>, "not implemented");
      } else if constexpr (is_integral_v<T>) {
        if constexpr (N == 1)
          ImGui::DragInt(_label.data(), _vals, _speed, _min, _max, _format, _flags);
        else if constexpr (N == 2)
          ImGui::DragInt2(_label.data(), _vals, _speed, _min, _max, _format, _flags);
        else if constexpr (N == 3)
          ImGui::DragInt3(_label.data(), _vals, _speed, _min, _max, _format, _flags);
        else if constexpr (N == 4)
          ImGui::DragInt4(_label.data(), _vals, _speed, _min, _max, _format, _flags);
        else
          static_assert(always_false<T>, "not implemented");
      } else {
        static_assert(always_false<T>, "not implemented");
      }
    }

    std::string _label;
    T *_vals;
    const char *_format;
    float _speed;
    T _min, _max;
    ImGuiSliderFlags _flags;
  };

  /// hierarchical widgets
  // menubar
  struct MenuItemWidget : ActionWidgetComponent {
    MenuItemWidget(std::string_view label, std::string_view shortcut)
        : _label{label}, ActionWidgetComponent{[]() {}}, _shortcut{shortcut} {}
    template <typename F> MenuItemWidget(std::string_view label, F &&f, std::string_view shortcut)
        : _label{label}, ActionWidgetComponent{FWD(f)}, _shortcut{shortcut} {}
    ~MenuItemWidget() = default;
    MenuItemWidget(MenuItemWidget &&) = default;
    MenuItemWidget &operator=(MenuItemWidget &&) = default;

    void paint() override {
      if (ImGui::MenuItem(_label.data(), _shortcut.data())) ActionWidgetComponent::paint();
    }
    std::string _label, _shortcut;
  };

  struct MenuBarWidgetComponent : WidgetConcept {
    struct MenuType : WidgetConcept {
      MenuType(std::string_view name) : _name{name} {}
      ~MenuType() = default;
      MenuType(MenuType &&) = default;
      MenuType &operator=(MenuType &&) = default;

      void paint() override {
        if (ImGui::BeginMenu(_name.data())) {
          for (auto &item : _items) item.paint();
          ImGui::EndMenu();
        }
      }

      MenuType &appendWidget(WidgetNode ce) {
        _items.push_back(zs::move(ce));
        return *this;
      }
      template <typename W> MenuType &appendWidget(W &&w) {
        _items.emplace_back(std::make_shared<W>(FWD(w)));
        return *this;
      }
      template <typename F> MenuType &appendItemWithAction(std::string_view label, F &&f,
                                                           std::string_view shortcut = "") {
        _items.emplace_back(std::make_shared<MenuItemWidget>(label, FWD(f), shortcut));
        return *this;
      }
      MenuType &appendItem(std::string_view label, std::string_view shortcut = "") {
        _items.emplace_back(std::make_shared<MenuItemWidget>(label, shortcut));
        return *this;
      }
      MenuType &appendMenu(const std::string &name) {
        _subMenuIds[name] = _items.size();
        _items.emplace_back(std::make_shared<MenuType>(name));
        return *this;
      }
      MenuType &withMenu(const std::string &name) {
        return *std::dynamic_pointer_cast<MenuType>(_items[_subMenuIds.at(name)].refWidget());
      }

      std::string _name;
      std::vector<WidgetNode> _items;
      std::map<std::string, u32> _subMenuIds;
    };

    MenuBarWidgetComponent() = default;
    ~MenuBarWidgetComponent() = default;
    MenuBarWidgetComponent(MenuBarWidgetComponent &&o) = default;
    MenuBarWidgetComponent &operator=(MenuBarWidgetComponent &&o) = default;

    void paint() override {
      if (ImGui::BeginMenuBar()) {
        for (auto &tab : menuTabs) tab.paint();

        ImGui::EndMenuBar();
      }
    }
    /// manage components
    void appendMenu(std::string_view menuName) { menuTabs.push_back(MenuType{menuName}); }
    MenuType &withMenu(u32 i) { return menuTabs[i]; }
    MenuType &withMenu(std::string_view menuName) {
      for (auto &tab : menuTabs)
        if (tab._name == menuName) return tab;
      throw std::runtime_error(fmt::format("cannot find menu tab with name [{}]", menuName));
    }

    // contents
    std::vector<MenuType> menuTabs;
  };

  // switcher
  template <typename T> void TabEntries<T>::paint() {
    ImGuiTabBarFlags tabBarFlags = ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs
                                   | ImGuiTabBarFlags_FittingPolicyScroll;
    /// update opened tab list
    for (auto it = _openedTabs.begin(); it != _openedTabs.end();) {
      std::string label = (*it)._label;
      if ((*it)._visible && _isValid((*it).getLabel()))
        it++;
      else {
        it = removeTab(it);
      }
    }

    ///
    // i32 selectIdx = -1;
    tabBarFlags |= ImGuiTabBarFlags_TabListPopupButton;
    if (ImGui::BeginTabBar("##tabs", tabBarFlags)) {
#if 0
      /// leading
      if (ImGui::TabItemButton(_leadingSymbolLiteral, ImGuiTabItemFlags_Leading))
        ImGui::OpenPopup("opened_list");
      if (ImGui::BeginPopup("opened_list")) {
        for (int j = 0; j < _openedTabs.size(); ++j) {
          const bool selected = j == _focusIdx;
          if (ImGui::Selectable(_openedTabs[j]._label.c_str(), selected)) {
            assert(_openedTabs[j]._visible);  // = true;
            selectIdx = j;
          }
        }
        ImGui::EndPopup();
      }
#endif
      /// trailing
      if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
        ImGui::OpenPopup("append_list");
      }
      if (ImGui::BeginPopup("append_list")) {
        const auto &items = _availableItems();
        std::string_view l;
        int idx = -1;
        for (const auto &label : items) {
          if (ImGui::Selectable(label.c_str())) {
            l = label;
            idx = tabOpened(l);
            if (idx != -1) {
              _focusIdx = idx;
              assert(_openedTabs[idx]._visible);  // = true;
            }
            break;
          }
        }
        if (!l.empty() && idx == -1) appendTab(l);
        ImGui::EndPopup();
      }
      /// body
      int i = 0;
      for (auto &tab : _openedTabs) {
        bool modified = _isModified(tab._label);
        ImGuiTabItemFlags tabFlags
            = modified ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
        // if (selectIdx == i) tabFlags |= ImGuiTabItemFlags_SetSelected;
        auto label = fmt::format("{}###{}", tab.getLabel(), tab.getId());
        bool visible = ImGui::BeginTabItem(label.c_str(), &tab._visible, tabFlags);

        if (visible) {
          ImGui::EndTabItem();
          _focusIdx = i;
          if (modified) {
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) _save(tab.getLabel());
          }
        }
        // deletion may happen at the next iteration
        i++;
      }
      // trailing
      //
      ImGui::EndTabBar();
    }
  }

}  // namespace zs