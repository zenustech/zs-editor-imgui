#pragma once
#include "imgui.h"
#include "zensim/ui/Widget.hpp"

namespace zs {

  ///
  /// mouse
  ///
  struct KeyModifiers {
    // ImGuiKeyChord _modifiers;  // (12) ctrl, (13) shift, (14) alt, (15) super
    bool ctrl{false}, shift{false}, alt{false}, super{false};
  };
  struct MouseEvent : virtual GuiEvent {
    ImVec2 _pos{-1.f, -1.f};
    ImGuiMouseButton _button{ImGuiMouseButton_Left};
    double _time{0.f};
    KeyModifiers _modifiers{};
    ImGuiMouseSource _mouseSource{ImGuiMouseSource_Mouse};

    MouseEvent(ImVec2 pos, ImGuiMouseButton button, double time, KeyModifiers modifiers = {},
               ImGuiMouseSource source = ImGuiMouseSource_Mouse) noexcept
        : _pos{pos}, _button{button}, _time{time}, _modifiers{modifiers}, _mouseSource{source} {}

    /// @note position with respect to the current window
    ImVec2 windowPos() const noexcept { return _pos; }
    ImGuiMouseButton button() const noexcept { return _button; }
    double time() const noexcept { return _time; }
    KeyModifiers modifiers() const noexcept { return _modifiers; }
    ImGuiMouseSource source() const noexcept { return _mouseSource; }
  };

  struct MousePressEvent : virtual MouseEvent {
    MousePressEvent(MouseEvent &&ev) noexcept : MouseEvent{zs::move(ev)} {}
    gui_event_e getGuiEventType() const { return gui_event_mousePressed; }
  };

  struct MouseReleaseEvent : virtual MouseEvent {
    MouseReleaseEvent(MouseEvent &&ev) noexcept : MouseEvent{zs::move(ev)} {}
    gui_event_e getGuiEventType() const { return gui_event_mouseReleased; }
  };

  struct MouseMoveEvent : virtual MouseEvent {
    ImVec2 _delta{0.f, 0.f};

    MouseMoveEvent(MouseEvent &&ev, const ImVec2 &delta) noexcept
        : MouseEvent{zs::move(ev)}, _delta{delta} {}
    gui_event_e getGuiEventType() const { return gui_event_mouseMoved; }
  };

  struct MouseDoubleClickEvent : virtual MouseEvent {
    MouseDoubleClickEvent(MouseEvent &&ev) noexcept : MouseEvent{zs::move(ev)} {}
    gui_event_e getGuiEventType() const { return gui_event_mouseDoubleClicked; }
  };

  struct MouseScrollEvent : virtual MouseEvent {
    float _wheelV{0.f};  // vertical
    float _wheelH{0.f};  // horizontal

    MouseScrollEvent(MouseEvent &&ev, float wheelV, float wheelH) noexcept
        : MouseEvent{zs::move(ev)}, _wheelV{wheelV}, _wheelH{wheelH} {}
    gui_event_e getGuiEventType() const { return gui_event_mouseScroll; }
  };

  ///
  /// keyboard
  ///
  struct KeyEvent : virtual GuiEvent {
    ImGuiKey _key{ImGuiKey_None};
    double _time{0.f};
    KeyModifiers _modifiers{};
    int _inputSource{0};  // 0: None, 1: Mouse, 2: _Keyboard, 3: GamePad
    bool _fromRepeat{false};

    KeyEvent(ImGuiKey key, double time, KeyModifiers modifiers = {}, bool repeat = false,
             int source = 2) noexcept
        : _key{key},
          _time{time},
          _modifiers{modifiers},
          _fromRepeat{repeat},
          _inputSource{source} {}

    /// @note position with respect to the current window
    ImGuiKey key() const noexcept { return _key; }
    const char *keyName() const noexcept { return ImGui::GetKeyName(_key); }
    bool isModKey() const noexcept;

    double time() const noexcept { return _time; }

    KeyModifiers modifiers() const noexcept { return _modifiers; }
    int source() const noexcept { return _inputSource; }

    bool isAutoRepeat() const noexcept { return _fromRepeat; }
  };
  struct KeyPressEvent : virtual KeyEvent {
    KeyPressEvent(KeyEvent &&ev) noexcept : KeyEvent{zs::move(ev)} {}
    gui_event_e getGuiEventType() const { return gui_event_keyPressed; }
  };

  struct KeyReleaseEvent : virtual KeyEvent {
    KeyReleaseEvent(KeyEvent &&ev) noexcept : KeyEvent{zs::move(ev)} {}
    gui_event_e getGuiEventType() const { return gui_event_keyReleased; }
  };

  struct KeyCharacterEvent : virtual KeyEvent {
    ImWchar _c{0};
    KeyCharacterEvent(KeyEvent &&ev, ImWchar c) noexcept : KeyEvent{zs::move(ev)}, _c{c} {}
    gui_event_e getGuiEventType() const { return gui_event_keyCharacter; }
  };

}  // namespace zs