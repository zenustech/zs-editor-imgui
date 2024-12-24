#include "ResourceWidgetComponent.hpp"

#include <Python.h>

#include "imgui.h"
#include "imgui_stdlib.h"
#include "interface/details/PyHelper.hpp"
#include "world/core/Utils.hpp"
#include "zensim/types/Polymorphism.h"

namespace zs::ui {

  ///
  /// TrivialValueDisplay
  ///
  bool TrivialValueDisplay::is_compatible(ZsValue obj) {
    switch (obj._idx) {
      case zs_var_type_cstr:
      case zs_var_type_i64:
      case zs_var_type_i32:
      case zs_var_type_i8:
      case zs_var_type_f64:
      case zs_var_type_f32:
        return true;
      default:;
    }
    return false;
  }
  float TrivialValueDisplay::do_preferredWidth() const {
    ZsValue v = value();
    switch (v._idx) {
      case zs_var_type_cstr:
        return ImGui::CalcTextSize(v._v.cstr).x;
      case zs_var_type_i64: {
        auto text = std::to_string(v._v.i64);
        return ImGui::CalcTextSize(text.c_str()).x;
      }
      case zs_var_type_i32: {
        auto text = std::to_string(v._v.i32);
        return ImGui::CalcTextSize(text.c_str()).x;
      }
      case zs_var_type_i8: {
        auto text = std::to_string(v._v.i8);
        return ImGui::CalcTextSize(text.c_str()).x;
      }
      case zs_var_type_f64: {
        auto text = std::to_string(v._v.f64);
        return ImGui::CalcTextSize(text.c_str()).x;
      }
      case zs_var_type_f32: {
        auto text = std::to_string(v._v.f32);
        return ImGui::CalcTextSize(text.c_str()).x;
      }
      default:;
    }
    return 0.f;
  }
  void TrivialValueDisplay::do_draw() {
    ZsValue v = value();
    switch (v._idx) {
      case zs_var_type_cstr:
        ImGui::TextUnformatted(v._v.cstr);
        break;
      case zs_var_type_i64: {
        auto text = std::to_string(v._v.i64);
        ImGui::TextUnformatted(text.c_str());
        break;
      }
      case zs_var_type_i32: {
        auto text = std::to_string(v._v.i32);
        ImGui::TextUnformatted(text.c_str());
        break;
      }
      case zs_var_type_i8: {
        auto text = std::to_string(v._v.i8);
        ImGui::TextUnformatted(text.c_str());
        break;
      }
      case zs_var_type_f64: {
        auto text = std::to_string(v._v.f64);
        ImGui::TextUnformatted(text.c_str());
        break;
      }
      case zs_var_type_f32: {
        auto text = std::to_string(v._v.f32);
        ImGui::TextUnformatted(text.c_str());
        break;
      }
      default:;
    }
  }
  ///
  /// PyDisplay
  ///
  void PyDisplay::updateStr() {
    auto obj = value();
    GILGuard guard;
    PyVar str = zs_string_obj(obj);
    PyVar typeStr = zs_string_obj_type(obj);
    if (str && typeStr) {
      auto text = fmt::format("type: [{}], str: {}\n\n", str.asString().c_str(),
                              typeStr.asString().c_str());
      _str = text;
    }
  }
  float PyDisplay::do_preferredWidth() const { return ImGui::CalcTextSize(_str.c_str()).x; }
  void PyDisplay::do_draw() {
    // ImGui::BeginDisabled(true);
    ImGui::InputText(getLabelOpaque().c_str(), _str.data(), _str.size(),
                     ImGuiInputTextFlags_ReadOnly);
    // ImGui::EndDisabled();

    if (_PyThreadState_UncheckedGet() && PyGILState_Check()) updateStr();
  }

  ///
  /// IntegerList
  ///
  bool IntegerList::is_compatible(ZsValue obj) {
    if (obj.isList()) {
      ZsList &l = obj.asList();
      for (auto e : l)
        if (!e.isIntegral()) return false;
      return true;
    }
    return obj.isIntegral();
  }
  float IntegerList::do_preferredWidth() const {
    float ma = 0.f;
    for (auto &i : _vs) {
      auto str = fmt::format("{} ", i);
      if (auto l = ImGui::CalcTextSize(str.c_str()).x; l > ma) ma = l;
    }
    return ma * _vs.size();
  }
  void IntegerList::do_draw() {
    if (ImGui::InputScalarN(getLabelOpaque().data(), ImGuiDataType_S64, _vs.data(), _vs.size(),
                            NULL, NULL, "%" PRId64, _flags)) {
      auto &v = ref().getRef();
      auto h = value();
      if (h.isList()) {
        ZsList &list = h.asList();
        for (int i = 0; i < _vs.size(); ++i) {
          if (ZsObject v = zs_long_obj_long_long(_vs[i]); list.setItemSteal(i, v) == -1) {
            Py_DECREF(v.handle());
          }
        }
      } else {
        assert(_vs.size() == 1);
        switch (h._idx) {
          case zs_var_type_i64:
            v._v.i64 = _vs[0];
          case zs_var_type_i32:
            v._v.i32 = _vs[0];
          case zs_var_type_i8:
            v._v.i8 = _vs[0];
          case zs_var_type_object:
            ref() = zs_long_obj_long_long(_vs[0]);
          default:;
        }
      }
    }
  }

  ///
  /// FloatingPointList
  ///
  bool FloatingPointList::is_compatible(ZsValue obj) {
    if (obj.isList()) {
      ZsList &l = obj.asList();
      for (auto e : l)
        if (!e.isFloatingPoint()) return false;
      return true;
    }
    return obj.isFloatingPoint();
  }

  float FloatingPointList::do_preferredWidth() const {
    float ma = 0.f;
    for (auto &f : _vs) {
      // auto str = fmt::format("{}", f);
      auto str = cformat("%.10f ", f);
      if (auto l = ImGui::CalcTextSize(str.c_str()).x; l > ma) ma = l;
    }
    return ma * _vs.size();
  }
  void FloatingPointList::do_draw() {
    if (ImGui::InputScalarN(getLabelOpaque().data(), ImGuiDataType_Double, _vs.data(), _vs.size(),
                            NULL, NULL, "%.10f", _flags)
        && linked()) {
      auto &v = ref().getRef();
      auto h = value();
      if (h.isList()) {
        ZsList &list = h.asList();
        for (int i = 0; i < _vs.size(); ++i) {
          if (ZsObject v = zs_float_obj_double(_vs[i]); list.setItemSteal(i, v) == -1)
            Py_DECREF(v.handle());
        }
      } else {
        assert(_vs.size() == 1);
        switch (h._idx) {
          case zs_var_type_f64:
            v._v.f64 = _vs[0];
          case zs_var_type_f32:
            v._v.f32 = _vs[0];
          case zs_var_type_object:
            ref() = zs_float_obj_double(_vs[0]);
          default:;
        }
      }
    }
  }

  ///
  /// String
  ///
  String::String(std::string_view label, ZsVar &obj) {
    _label = label;
    link(obj);

    _flags = 0;  // ImGuiInputTextFlags_EnterReturnsTrue;

    auto h = obj.getValue();
    if (h.isBytesOrByteArray()) {
      ZsBytes &bs = h.asBytes();
      auto sz = bs.size();
      _buffer.resize(sz);
      std::memcpy(_buffer.data(), bs.c_str(), sizeof(char) * sz);
    } else {
      ZsString &str = h.asString();
      if (PyVar bs = zs_bytes_obj(str)) {
        auto sz = bs.asBytes().size();
        _buffer.resize(sz);
        std::memcpy(_buffer.data(), bs.asBytes().c_str(), sizeof(char) * sz);
      }
    }
  }
  bool String::is_compatible(ZsValue obj) { return obj.isString() || obj.isBytesOrByteArray(); }
  void String::do_draw() {
    if (ImGui::InputText(getLabelOpaque().data(), &_buffer, _flags) && linked()) {
      auto h = value();
      auto &v = ref();
      if (h.isString()) {
        v = zs_string_obj_cstr(_buffer.c_str());
      } else if (h.isBytes()) {
        v = zs_bytes_obj_cstr(_buffer.c_str());
      } else {
        assert(h.isByteArray());
        ZsBytes byteArray = value();
        auto curLen = byteArray.size();
        auto bufferLen = _buffer.size() + 1;
        if (curLen > bufferLen || byteArray.resize(bufferLen)) {
          std::memcpy(byteArray.data(), _buffer.c_str(), sizeof(char) * bufferLen);
        }
      }
    }
  }

  ///
  /// Bool
  ///
  Bool::Bool(std::string_view label, ZsVar &obj) {
    _label = label;
    link(obj);

    _checked = static_cast<long long>(obj);
  }
  bool Bool::is_compatible(ZsValue obj) { return obj.isBoolObject(); }
  void Bool::do_draw() {
    if (ImGui::Checkbox(getLabelOpaque().c_str(), &_checked)) {
      auto &v = ref().getRef();

      if (_checked)
        v._v.obj = Py_True;
      else
        v._v.obj = Py_False;
    }
  }

  ///
  /// Combo
  ///
  Combo::Combo(std::string_view label, ZsVar &obj, ZsValue candidates) {
    _label = label;
    link(obj);

    auto processCandidates = [this, &obj](auto options) {
      _idx = 0;
      _candidates.reserve(options.size());
      _displayItems.reserve(options.size());
      _initVals.reserve(options.size());
      int i = 0;
      for (auto e : options) {
        if (obj == e) _idx = i;
        if (e.isString()) {
          if (PyVar bs = zs_bytes_obj(e)) {
            auto &str = _candidates.emplace_back(fmt::format("\"{}\"", bs.asBytes().c_str()));
            _displayItems.emplace_back(str.c_str());
            _initVals.emplace_back(e.newRef());
          }
        } else if (e.isBytesOrByteArray()) {
          if (ZsString s = zs_string_obj(e)) {
            auto &str = _candidates.emplace_back(fmt::format("b\'{}\'", e.asBytes().c_str()));
            _displayItems.emplace_back(str.c_str());
            _initVals.emplace_back(s);
          }
        } else {
          if (ZsString s = zs_string_obj(e)) {
            if (PyVar bs = zs_bytes_obj(s)) {
              auto &str = _candidates.emplace_back(bs.asBytes().c_str());
              _displayItems.emplace_back(str.c_str());
              _initVals.emplace_back(e.newRef());
            }
          }
        }
        i++;
      }
      obj = _initVals[_idx];
    };
    // tuple or list
    if (candidates.isTuple()) {
      processCandidates(candidates.asTuple());
    } else if (candidates.isList()) {
      processCandidates(candidates.asList());
    }
  }
  float Combo::do_preferredWidth() const {
    float ma = 0.f;
    for (auto &s : _candidates) {
      if (auto l = ImGui::CalcTextSize((s + " ").c_str()).x; l > ma) ma = l;
    }
    return ma;
  }
  void Combo::do_draw() {
    if (ImGui::BeginCombo(getLabelOpaque().data(), _displayItems[_idx],
                          ImGuiComboFlags_NoArrowButton)) {
      for (int i = 0; i < _displayItems.size(); ++i) {
        const bool isSelected = (_idx == i);
        if (ImGui::Selectable(_displayItems[i], isSelected)) {
          _idx = i;
          ref() = _initVals[_idx];
        }
        if (isSelected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  }

  ///
  /// Combo Slider
  ///
  ComboSlider::ComboSlider(std::string_view label, ZsVar &obj, ZsValue candidates) {
    _label = label;
    link(obj);

    auto processCandidates = [this, &obj](auto options) {
      _idx = 0;
      _candidates.reserve(options.size());
      _displayItems.reserve(options.size());
      _initVals.reserve(options.size());
      int i = 0;
      for (auto e : options) {
        if (obj == e) _idx = i;
        if (e.isString()) {
          if (PyVar bs = zs_bytes_obj(e)) {
            auto &str = _candidates.emplace_back(fmt::format("\"{}\"", bs.asBytes().c_str()));
            _displayItems.emplace_back(str.c_str());
            _initVals.emplace_back(e.newRef());
          }
        } else if (e.isBytesOrByteArray()) {
          if (ZsString s = zs_string_obj(e)) {
            auto &str = _candidates.emplace_back(fmt::format("b\'{}\'", e.asBytes().c_str()));
            _displayItems.emplace_back(str.c_str());
            _initVals.emplace_back(s);
          }
        } else {
          if (ZsString s = zs_string_obj(e)) {
            if (PyVar bs = zs_bytes_obj(s)) {
              auto &str = _candidates.emplace_back(bs.asBytes().c_str());
              _displayItems.emplace_back(str.c_str());
              _initVals.emplace_back(e.newRef());
            }
          }
        }
        i++;
      }
      obj = _initVals[_idx];
    };
    // tuple or list
    if (candidates.isTuple()) {
      processCandidates(candidates.asTuple());
    } else if (candidates.isList()) {
      processCandidates(candidates.asList());
    }
  }
  float ComboSlider::do_preferredWidth() const {
    float ma = 0.f;
    for (auto &s : _candidates) {
      if (auto l = ImGui::CalcTextSize((s + " ").c_str()).x; l > ma) ma = l;
    }
    return ma * 2;
  }
  void ComboSlider::do_draw() {
    const char *elemName
        = (_idx >= 0 && _idx < _displayItems.size()) ? _displayItems[_idx] : "Unknown";
    if (ImGui::SliderInt(getLabelOpaque().data(), &_idx, 0, _displayItems.size() - 1, elemName)) {
      ref() = _initVals[_idx];
    }
  }

  ///
  /// Button
  ///
  Button::Button(std::string_view label, ZsVar &obj) {
    _label = label;
    link(obj);

    if (obj.getValue().isString()) {
      if (PyVar bs = zs_bytes_obj(obj.getValue())) {
        _tag = std::string(bs.asBytes().c_str());
      }
    }
  }
  void Button::do_draw() {
#if 0
  ImGui::InputText(getLabelOpaque().c_str(), _tag.data(), _tag.size(),
                   ImGuiInputTextFlags_ReadOnly);
#else
    if (ImGui::Button(_tag.c_str())) {
      if (_action) {
        _action();
      }
    }
#endif
  }

  ///
  GenericResourceWidget::GenericResourceWidget(std::string_view label, ZsVar &obj) {
    init(label, obj);
  }
  GenericResourceWidget::GenericResourceWidget(std::string_view label, ZsVar &obj, ZsValue aux) {
    if (Combo::is_compatible(aux)) {
      _widget = Combo(label, obj, aux);
    } else
      init(label, obj);
  }
  void GenericResourceWidget::init(std::string_view label, ZsVar &obj) {
    bool done = true;
    if (is_fundamental_resource(obj)) {
      if (Bool::is_compatible(obj)) {
        _widget = Bool(label, obj);
      } else if (IntegerList::is_compatible(obj)) {
        _widget = IntegerList(label, obj);
      } else if (FloatingPointList::is_compatible(obj)) {
        _widget = FloatingPointList(label, obj);
      } else if (String::is_compatible(obj)) {
        _widget = String(label, obj);
      } else if (TrivialValueDisplay::is_compatible(obj)) {
        _widget = TrivialValueDisplay(label, obj);
      } else if (PyDisplay::is_compatible(obj)) {
        _widget = PyDisplay(label, obj);
      } else
        done = false;
    } else {
      if (PyDisplay::is_compatible(obj)) {
        _widget = PyDisplay(label, obj);
      } else
        done = false;
    }
    if (!done)
      if (PyVar typeStr = zs_string_obj_type(obj)) {
        throw std::runtime_error(
            fmt::format("no appropritate resource widget for resources of type [{}]",
                        typeStr.asString().c_str()));
      }
  }
  void GenericResourceWidget::drawWithLabel() {
    return match([](std::monostate) {},
                 [](auto &widget) {
                   ImGui::AlignTextToFramePadding();
                   ImGui::TextUnformatted(widget.getLabel().data());
                   ImGui::SameLine();
                   widget.draw();
                 })(_widget);
  }
  void GenericResourceWidget::draw() {
    return match([](std::monostate) {}, [](auto &widget) { widget.draw(); })(_widget);
  }
  bool GenericResourceWidget::link(ZsVar &obj) {
    return match([](std::monostate) { return false; },
                 [&obj](auto &widget) { return widget.link(obj); })(_widget);
  }
  float GenericResourceWidget::preferredWidth() const {
    return match([](std::monostate) { return 0.f; },
                 [](auto &widget) { return widget.preferredWidth(); })(_widget);
  }

  ///
  void GenericAttribWidget::appendWidget(std::string_view label, ZsVar &obj) {
    widget_t widget;
    init(widget, label, obj);
    _sections.back()._widgets.push_back(widget);
  }
  void GenericAttribWidget::appendWidget(std::string_view label, ZsVar &obj, ZsValue aux) {
    widget_t widget;
    if (Combo::is_compatible(aux)) {
      widget = Combo(label, obj, aux);
    } else
      init(widget, label, obj);
    _sections.back()._widgets.push_back(widget);
  }
  void GenericAttribWidget::init(widget_t &widget, std::string_view label, ZsVar &obj) {
    bool done = true;
    if (is_fundamental_resource(obj)) {
      if (Bool::is_compatible(obj)) {
        widget = Bool(label, obj);
      } else if (IntegerList::is_compatible(obj)) {
        widget = IntegerList(label, obj);
      } else if (FloatingPointList::is_compatible(obj)) {
        widget = FloatingPointList(label, obj);
      } else if (String::is_compatible(obj)) {
        widget = String(label, obj);
      } else if (TrivialValueDisplay::is_compatible(obj)) {
        widget = TrivialValueDisplay(label, obj);
      } else if (PyDisplay::is_compatible(obj)) {
        widget = PyDisplay(label, obj);
      } else
        done = false;
    } else {
      if (PyDisplay::is_compatible(obj)) {
        widget = PyDisplay(label, obj);
      } else
        done = false;
    }
    if (!done)
      if (PyVar typeStr = zs_string_obj_type(obj)) {
        throw std::runtime_error(
            fmt::format("no appropritate resource widget for resources of type [{}]",
                        typeStr.asString().c_str()));
      }
  }
  void GenericAttribWidget::draw() {
    bool openTabBar = _hasTabSection && ImGui::BeginTabBar("TabBar");
    for (auto &section : _sections) {
      bool sectionOpened = true;
      switch (section._type) {
        case separator_type_e::sep:
          if (section._tag.empty())
            ImGui::Separator();
          else
            ImGui::SeparatorText(section._tag.c_str());
          break;
        case separator_type_e::header:
          sectionOpened = ImGui::CollapsingHeader(section._tag.c_str());
          break;
        case separator_type_e::tab:
          sectionOpened = ImGui::BeginTabItem(section._tag.c_str(), NULL, ImGuiTabItemFlags_None);
          break;
        default:
          ImGui::Separator();
          break;
      }
      if (sectionOpened) {
        for (auto &widget : section._widgets) {
          match([](std::monostate) {},
                [](auto &widget) {
                  ImGui::AlignTextToFramePadding();
                  ImGui::TextUnformatted(widget.getLabel().data());
                  ImGui::SameLine();
                  widget.draw();
                })(widget);
        }

        switch (section._type) {
          case separator_type_e::tab:
            ImGui::EndTabItem();
            break;
          default:
            break;
        }
      }
    }
    if (openTabBar) ImGui::EndTabBar();
  }

}  // namespace zs::ui