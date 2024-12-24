#pragma once
#include <variant>

#include "WidgetComponent.hpp"
#include "imgui.h"
#include "interface/world/ObjectInterface.hpp"
#include "interface/world/value_type/ValueInterface.hpp"

namespace zs {

namespace ui {

enum resource_e {
  resource_default = 0,
  resource_integers,
  resource_floats,
  resource_string,
  num_resources
};

///
/// @note mostly assign the data from ui widget to the bound zsobj
///
template <typename Derived> struct ResourceWidgetInterface {
  float preferredWidth() const { return self().do_preferredWidth(); }

  /// @note required implementation
  void draw() {
    self().do_draw();
    return;
  }

  bool link(ZsVar &obj) {
    _twin = &obj;
    return true;
  }
  // bool linkObject(ZsObject obj) {
  //   assert(obj.isObject());
  //   _aux = ZsVar(obj.newRef());
  //   _twin = &_aux;
  //   return true;
  // }
  void setAux(ZsValue aux) { _aux = aux; }

  bool linked() const { return _twin != nullptr; }
  // ensure already linked
  ZsValue value() const { return _twin->getValue(); }
  ZsVar &ref() { return *_twin; }
  ZsValue aux() const { return _aux; }

  template <typename F> int injectCustomBehavior(ZsValue loc, F &&f) {
    return self().do_injectCustomBehavior(loc, FWD(f));
  }

  // it is safer to share the ownership without much extra storage cost,
  // but the widget itself is expected to be removed not so long after the
  // target resource destruction

  std::string getLabelOpaque() const { return std::string("##") + _label; }
  std::string_view getLabel() const { return _label; }

  std::string _label;

protected:
  float do_preferredWidth() const { return 0.f; }
  int do_injectCustomBehavior(...) { return 0; }

private:
  auto &self() noexcept { return static_cast<Derived &>(*this); }
  const auto &self() const noexcept {
    return static_cast<const Derived &>(*this);
  }
  ZsVar *_twin; // using its value might be better?
  ZsVar _aux;
};

inline bool is_fundamental_resource(ZsValue obj);

inline bool is_compound_resource(ZsValue obj) {
  return obj.isTuple() || obj.isList() || obj.isDict() || obj.isSet();
}

///
/// @note display
///
struct TrivialValueDisplay : ResourceWidgetInterface<TrivialValueDisplay> {
  static constexpr bool s_editable = false;
  static bool is_compatible(ZsValue obj);

  float do_preferredWidth() const;
  void do_draw();

  TrivialValueDisplay(std::string_view label, ZsVar &obj) { link(obj); }
};

struct PyDisplay : ResourceWidgetInterface<PyDisplay> {
  static bool is_compatible(ZsValue obj) { return obj.isObject(); }

  float do_preferredWidth() const;
  void do_draw();

  PyDisplay(std::string_view label, ZsVar &obj) {
    _label = label;
    link(obj);

    updateStr();
  }

  void updateStr();

  std::string _str;
};

///
/// @note edit
///
struct IntegerList : ResourceWidgetInterface<IntegerList> {
  static bool is_compatible(ZsValue obj);

  float do_preferredWidth() const;
  void do_draw();

  IntegerList(std::string_view label, ZsVar &obj) {
    link(obj);

    _label = label;
    _flags = // ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_AutoSelectAll;
    auto h = obj.getValue();
    if (h.isList()) {
      ZsList &l = h.asList();
      _vs.resize(l.size());
      int i = 0;
      for (auto e : l)
        _vs[i++] = static_cast<long long>(e);
    } else {
      _vs.resize(1);
      switch (h._idx) {
      case zs_var_type_object:
        _vs[0] = static_cast<long long>(h);
        break;
      case zs_var_type_i64:
        _vs[0] = h._v.i64;
        break;
      case zs_var_type_i32:
        _vs[0] = h._v.i32;
        break;
      case zs_var_type_i8:
        _vs[0] = h._v.i8;
        break;
      default:;
      }
    }
  }

  std::vector<i64> _vs{0};
  ImGuiInputTextFlags _flags;
};

struct FloatingPointList : ResourceWidgetInterface<FloatingPointList> {
  static bool is_compatible(ZsValue obj);

  float do_preferredWidth() const;
  void do_draw();

  FloatingPointList(std::string_view label, ZsVar &obj) {
    link(obj);

    _label = label;
    _flags = // ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_AutoSelectAll;
    auto h = obj.getValue();
    if (h.isList()) {
      ZsList &l = h.asList();
      _vs.resize(l.size());
      int i = 0;
      for (auto e : l)
        _vs[i++] = static_cast<double>(e);
    } else {
      _vs.resize(1);
      switch (h._idx) {
      case zs_var_type_object:
        _vs[0] = static_cast<double>(h);
        break;
      case zs_var_type_f64:
        _vs[0] = h._v.f64;
        break;
      case zs_var_type_f32:
        _vs[0] = h._v.f32;
        break;
      default:;
      }
    }
  }

  std::vector<double> _vs{0.};
  ImGuiInputTextFlags _flags;
};

struct String : ResourceWidgetInterface<String> {
  static bool is_compatible(ZsValue obj);

  float do_preferredWidth() const {
    return ImGui::CalcTextSize((_buffer + " ").c_str()).x;
  }
  void do_draw();

  String(std::string_view label, ZsVar &obj);

  std::string _buffer;
  ImGuiInputTextFlags _flags;
};

struct Bool : ResourceWidgetInterface<Bool> {
  static bool is_compatible(ZsValue obj);

  float do_preferredWidth() const { return 0.f; }
  void do_draw();

  Bool(std::string_view label, ZsVar &obj);

  bool _checked;
};

struct Combo : ResourceWidgetInterface<Combo> {
  static bool is_compatible(ZsValue obj) {
    return obj.isTuple() || obj.isList();
  }

  float do_preferredWidth() const;
  void do_draw();

  Combo(std::string_view label, ZsVar &obj, ZsValue candidates);

  int _idx;
  std::vector<std::string> _candidates;
  std::vector<const char *> _displayItems;
  std::vector<ZsVar> _initVals;
};

struct ComboSlider : ResourceWidgetInterface<ComboSlider> {
  static bool is_compatible(ZsValue aux) {
    return aux.isTuple() || aux.isList();
  }

  float do_preferredWidth() const;
  void do_draw();

  ComboSlider(std::string_view label, ZsVar &obj, ZsValue candidates);

  int _idx;
  std::vector<std::string> _candidates;
  std::vector<const char *> _displayItems;
  std::vector<ZsVar> _initVals;
};

struct Button : ResourceWidgetInterface<Button> {
  static bool is_compatible(ZsValue obj) { return obj.isString(); }

  float do_preferredWidth() const {
    return ImGui::CalcTextSize(_tag.c_str()).x;
  }
  void do_draw();

  Button(std::string_view label, ZsVar &obj);

  std::string _tag;
  zs::function<void()> _action;
};

#if 0
struct PopupSelection : ResourceWidgetInterface<PopupSelection> {
  static bool is_compatible(ZsValue obj) {
    return obj.isTuple() || obj.isList();
  }

  float do_preferredWidth() const;
  void do_draw();

  PopupSelection(std::string_view label, ZsVar &obj, ZsValue candidates);

  template <typename F> int do_injectCustomBehavior(ZsValue loc, F &&f) {
    if (auto p = static_cast<long long>(loc); p == 0)
      ;
    return 0;
  }

  int _idx;
  std::vector<std::string> _candidates;
  std::vector<const char *> _displayItems;
  std::vector<ZsVar> _initVals;
};
#endif

/// ZsVar type must not be changed (handle may change for PyObj)
struct GenericResourceWidget {
  GenericResourceWidget() = default;
  GenericResourceWidget(std::string_view label, ZsVar &obj);
  GenericResourceWidget(std::string_view label, ZsVar &obj, ZsValue aux);
  /// @note for button
  template <typename WidgetT>
  GenericResourceWidget(WidgetT &&w) : _widget{FWD(w)} {}

  void drawWithLabel();
  void draw();

  float preferredWidth() const;

  bool link(ZsVar &obj);

  template <typename F> int injectCustomBehavior(ZsValue loc, F &&f) {
    return std::visit(
        [loc, f = FWD(f)](auto &widget) mutable {
          if constexpr (!zs::is_same_v<RM_CVREF_T(widget), std::monostate>)
            return widget.injectCustomBehavior(loc, FWD(f));
          return 0;
        },
        _widget);
  }

  operator bool() const { return _widget.index() != 0; }

protected:
  void init(std::string_view label, ZsVar &obj);

  std::variant<std::monostate, PyDisplay, TrivialValueDisplay, Bool,
               IntegerList, FloatingPointList, String, Combo, ComboSlider,
               Button>
      _widget;
};

template <typename WidgetType, typename... Ts>
GenericResourceWidget make_generic_resource_widget(Ts &&...ts) {
  return GenericResourceWidget(WidgetType{FWD(ts)...});
}

/// attrib
struct GenericAttribWidget {
  enum separator_type_e : int {
    sep = 0,
    header,
    tab,
  };
  using widget_t =
      std::variant<std::monostate, PyDisplay, TrivialValueDisplay, Bool,
                   IntegerList, FloatingPointList, String, Combo, Button>;
  struct Section {
    Section(separator_type_e type, std::string_view tag)
        : _type{type}, _tag{tag} {}
    separator_type_e _type;
    std::string _tag;
    std::vector<widget_t> _widgets;
  };

  GenericAttribWidget() = default;

  void appendWidget(std::string_view label, ZsVar &obj);
  void appendWidget(std::string_view label, ZsVar &obj, ZsValue aux);

  template <typename WidgetT> void appendWidget(WidgetT &&w) {
    _sections.back()._widgets.emplace_back(FWD(w));
  }
  void newSection(std::string_view tag, separator_type_e type) {
    _sections.emplace_back(type, tag);
    if (type == separator_type_e::tab)
      _hasTabSection = true;
  }

  void draw();

  operator bool() const { return _sections.size(); }

protected:
  void init(widget_t &widget, std::string_view label, ZsVar &obj);

  std::vector<Section> _sections;
  bool _hasTabSection{false};
};

template <typename WidgetType, typename... Ts>
GenericAttribWidget make_generic_attrib_widget(Ts &&...ts) {
  return GenericAttribWidget(WidgetType{FWD(ts)...});
}

bool is_fundamental_resource(ZsValue obj) {
  return TrivialValueDisplay::is_compatible(obj) ||
         IntegerList::is_compatible(obj) ||
         FloatingPointList::is_compatible(obj) || String::is_compatible(obj) ||
         Bool::is_compatible(obj);
}

} // namespace ui

} // namespace zs