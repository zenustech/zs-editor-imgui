#include "GlfwSystem.hpp"
#include "GuiWindow.hpp"
#include "ImguiSystem.hpp"
#include "imgui.h"

namespace zs {

struct ImGuiPlatformViewportData;

static void update_imgui_key_modifiers(GLFWwindow *window) {
  ImGuiIO &io = ImGui::GetIO();
  io.AddKeyEvent(
      ImGuiMod_Ctrl,
      (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) ||
          (glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS));
  io.AddKeyEvent(ImGuiMod_Shift,
                 (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ||
                     (glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS));
  io.AddKeyEvent(ImGuiMod_Alt,
                 (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS) ||
                     (glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS));
  io.AddKeyEvent(ImGuiMod_Super,
                 (glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS) ||
                     (glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS));
}

static ImGuiKey glfw_key_to_imgui_key(int key) {
  switch (key) {
  case GLFW_KEY_TAB:
    return ImGuiKey_Tab;
  case GLFW_KEY_LEFT:
    return ImGuiKey_LeftArrow;
  case GLFW_KEY_RIGHT:
    return ImGuiKey_RightArrow;
  case GLFW_KEY_UP:
    return ImGuiKey_UpArrow;
  case GLFW_KEY_DOWN:
    return ImGuiKey_DownArrow;
  case GLFW_KEY_PAGE_UP:
    return ImGuiKey_PageUp;
  case GLFW_KEY_PAGE_DOWN:
    return ImGuiKey_PageDown;
  case GLFW_KEY_HOME:
    return ImGuiKey_Home;
  case GLFW_KEY_END:
    return ImGuiKey_End;
  case GLFW_KEY_INSERT:
    return ImGuiKey_Insert;
  case GLFW_KEY_DELETE:
    return ImGuiKey_Delete;
  case GLFW_KEY_BACKSPACE:
    return ImGuiKey_Backspace;
  case GLFW_KEY_SPACE:
    return ImGuiKey_Space;
  case GLFW_KEY_ENTER:
    return ImGuiKey_Enter;
  case GLFW_KEY_ESCAPE:
    return ImGuiKey_Escape;
  case GLFW_KEY_APOSTROPHE:
    return ImGuiKey_Apostrophe;
  case GLFW_KEY_COMMA:
    return ImGuiKey_Comma;
  case GLFW_KEY_MINUS:
    return ImGuiKey_Minus;
  case GLFW_KEY_PERIOD:
    return ImGuiKey_Period;
  case GLFW_KEY_SLASH:
    return ImGuiKey_Slash;
  case GLFW_KEY_SEMICOLON:
    return ImGuiKey_Semicolon;
  case GLFW_KEY_EQUAL:
    return ImGuiKey_Equal;
  case GLFW_KEY_LEFT_BRACKET:
    return ImGuiKey_LeftBracket;
  case GLFW_KEY_BACKSLASH:
    return ImGuiKey_Backslash;
  case GLFW_KEY_RIGHT_BRACKET:
    return ImGuiKey_RightBracket;
  case GLFW_KEY_GRAVE_ACCENT:
    return ImGuiKey_GraveAccent;
  case GLFW_KEY_CAPS_LOCK:
    return ImGuiKey_CapsLock;
  case GLFW_KEY_SCROLL_LOCK:
    return ImGuiKey_ScrollLock;
  case GLFW_KEY_NUM_LOCK:
    return ImGuiKey_NumLock;
  case GLFW_KEY_PRINT_SCREEN:
    return ImGuiKey_PrintScreen;
  case GLFW_KEY_PAUSE:
    return ImGuiKey_Pause;
  case GLFW_KEY_KP_0:
    return ImGuiKey_Keypad0;
  case GLFW_KEY_KP_1:
    return ImGuiKey_Keypad1;
  case GLFW_KEY_KP_2:
    return ImGuiKey_Keypad2;
  case GLFW_KEY_KP_3:
    return ImGuiKey_Keypad3;
  case GLFW_KEY_KP_4:
    return ImGuiKey_Keypad4;
  case GLFW_KEY_KP_5:
    return ImGuiKey_Keypad5;
  case GLFW_KEY_KP_6:
    return ImGuiKey_Keypad6;
  case GLFW_KEY_KP_7:
    return ImGuiKey_Keypad7;
  case GLFW_KEY_KP_8:
    return ImGuiKey_Keypad8;
  case GLFW_KEY_KP_9:
    return ImGuiKey_Keypad9;
  case GLFW_KEY_KP_DECIMAL:
    return ImGuiKey_KeypadDecimal;
  case GLFW_KEY_KP_DIVIDE:
    return ImGuiKey_KeypadDivide;
  case GLFW_KEY_KP_MULTIPLY:
    return ImGuiKey_KeypadMultiply;
  case GLFW_KEY_KP_SUBTRACT:
    return ImGuiKey_KeypadSubtract;
  case GLFW_KEY_KP_ADD:
    return ImGuiKey_KeypadAdd;
  case GLFW_KEY_KP_ENTER:
    return ImGuiKey_KeypadEnter;
  case GLFW_KEY_KP_EQUAL:
    return ImGuiKey_KeypadEqual;
  case GLFW_KEY_LEFT_SHIFT:
    return ImGuiKey_LeftShift;
  case GLFW_KEY_LEFT_CONTROL:
    return ImGuiKey_LeftCtrl;
  case GLFW_KEY_LEFT_ALT:
    return ImGuiKey_LeftAlt;
  case GLFW_KEY_LEFT_SUPER:
    return ImGuiKey_LeftSuper;
  case GLFW_KEY_RIGHT_SHIFT:
    return ImGuiKey_RightShift;
  case GLFW_KEY_RIGHT_CONTROL:
    return ImGuiKey_RightCtrl;
  case GLFW_KEY_RIGHT_ALT:
    return ImGuiKey_RightAlt;
  case GLFW_KEY_RIGHT_SUPER:
    return ImGuiKey_RightSuper;
  case GLFW_KEY_MENU:
    return ImGuiKey_Menu;
  case GLFW_KEY_0:
    return ImGuiKey_0;
  case GLFW_KEY_1:
    return ImGuiKey_1;
  case GLFW_KEY_2:
    return ImGuiKey_2;
  case GLFW_KEY_3:
    return ImGuiKey_3;
  case GLFW_KEY_4:
    return ImGuiKey_4;
  case GLFW_KEY_5:
    return ImGuiKey_5;
  case GLFW_KEY_6:
    return ImGuiKey_6;
  case GLFW_KEY_7:
    return ImGuiKey_7;
  case GLFW_KEY_8:
    return ImGuiKey_8;
  case GLFW_KEY_9:
    return ImGuiKey_9;
  case GLFW_KEY_A:
    return ImGuiKey_A;
  case GLFW_KEY_B:
    return ImGuiKey_B;
  case GLFW_KEY_C:
    return ImGuiKey_C;
  case GLFW_KEY_D:
    return ImGuiKey_D;
  case GLFW_KEY_E:
    return ImGuiKey_E;
  case GLFW_KEY_F:
    return ImGuiKey_F;
  case GLFW_KEY_G:
    return ImGuiKey_G;
  case GLFW_KEY_H:
    return ImGuiKey_H;
  case GLFW_KEY_I:
    return ImGuiKey_I;
  case GLFW_KEY_J:
    return ImGuiKey_J;
  case GLFW_KEY_K:
    return ImGuiKey_K;
  case GLFW_KEY_L:
    return ImGuiKey_L;
  case GLFW_KEY_M:
    return ImGuiKey_M;
  case GLFW_KEY_N:
    return ImGuiKey_N;
  case GLFW_KEY_O:
    return ImGuiKey_O;
  case GLFW_KEY_P:
    return ImGuiKey_P;
  case GLFW_KEY_Q:
    return ImGuiKey_Q;
  case GLFW_KEY_R:
    return ImGuiKey_R;
  case GLFW_KEY_S:
    return ImGuiKey_S;
  case GLFW_KEY_T:
    return ImGuiKey_T;
  case GLFW_KEY_U:
    return ImGuiKey_U;
  case GLFW_KEY_V:
    return ImGuiKey_V;
  case GLFW_KEY_W:
    return ImGuiKey_W;
  case GLFW_KEY_X:
    return ImGuiKey_X;
  case GLFW_KEY_Y:
    return ImGuiKey_Y;
  case GLFW_KEY_Z:
    return ImGuiKey_Z;
  case GLFW_KEY_F1:
    return ImGuiKey_F1;
  case GLFW_KEY_F2:
    return ImGuiKey_F2;
  case GLFW_KEY_F3:
    return ImGuiKey_F3;
  case GLFW_KEY_F4:
    return ImGuiKey_F4;
  case GLFW_KEY_F5:
    return ImGuiKey_F5;
  case GLFW_KEY_F6:
    return ImGuiKey_F6;
  case GLFW_KEY_F7:
    return ImGuiKey_F7;
  case GLFW_KEY_F8:
    return ImGuiKey_F8;
  case GLFW_KEY_F9:
    return ImGuiKey_F9;
  case GLFW_KEY_F10:
    return ImGuiKey_F10;
  case GLFW_KEY_F11:
    return ImGuiKey_F11;
  case GLFW_KEY_F12:
    return ImGuiKey_F12;
  case GLFW_KEY_F13:
    return ImGuiKey_F13;
  case GLFW_KEY_F14:
    return ImGuiKey_F14;
  case GLFW_KEY_F15:
    return ImGuiKey_F15;
  case GLFW_KEY_F16:
    return ImGuiKey_F16;
  case GLFW_KEY_F17:
    return ImGuiKey_F17;
  case GLFW_KEY_F18:
    return ImGuiKey_F18;
  case GLFW_KEY_F19:
    return ImGuiKey_F19;
  case GLFW_KEY_F20:
    return ImGuiKey_F20;
  case GLFW_KEY_F21:
    return ImGuiKey_F21;
  case GLFW_KEY_F22:
    return ImGuiKey_F22;
  case GLFW_KEY_F23:
    return ImGuiKey_F23;
  case GLFW_KEY_F24:
    return ImGuiKey_F24;
  default:
    return ImGuiKey_None;
  }
}

static int translate_untranslated_key(int key, int scancode) {
#if GLFW_HAS_GETKEYNAME && !defined(__EMSCRIPTEN__)
  // GLFW 3.1+ attempts to "untranslate" keys, which goes the opposite of what
  // every other framework does, making using lettered shortcuts difficult. (It
  // had reasons to do so: namely GLFW is/was more likely to be used for
  // WASD-type game controls rather than lettered shortcuts, but IHMO the 3.1
  // change could have been done differently) See
  // https://github.com/glfw/glfw/issues/1502 for details. Adding a workaround
  // to undo this (so our keys are translated->untranslated->translated, likely
  // a lossy process). This won't cover edge cases but this is at least going to
  // cover common cases.
  if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_EQUAL)
    return key;
  GLFWerrorfun prev_error_callback = glfwSetErrorCallback(nullptr);
  const char *key_name = glfwGetKeyName(key, scancode);
  glfwSetErrorCallback(prev_error_callback);
#if GLFW_HAS_GETERROR && !defined(__EMSCRIPTEN__) // Eat errors (see #5908)
  (void)glfwGetError(nullptr);
#endif
  if (key_name && key_name[0] != 0 && key_name[1] == 0) {
    const char char_names[] = "`-=[]\\,;\'./";
    const int char_keys[] = {
        GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_MINUS,         GLFW_KEY_EQUAL,
        GLFW_KEY_LEFT_BRACKET, GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_BACKSLASH,
        GLFW_KEY_COMMA,        GLFW_KEY_SEMICOLON,     GLFW_KEY_APOSTROPHE,
        GLFW_KEY_PERIOD,       GLFW_KEY_SLASH,         0};
    IM_ASSERT(IM_ARRAYSIZE(char_names) == IM_ARRAYSIZE(char_keys));
    if (key_name[0] >= '0' && key_name[0] <= '9') {
      key = GLFW_KEY_0 + (key_name[0] - '0');
    } else if (key_name[0] >= 'A' && key_name[0] <= 'Z') {
      key = GLFW_KEY_A + (key_name[0] - 'A');
    } else if (key_name[0] >= 'a' && key_name[0] <= 'z') {
      key = GLFW_KEY_A + (key_name[0] - 'a');
    } else if (const char *p = strchr(char_names, key_name[0])) {
      key = char_keys[p - char_names];
    }
  }
  // if (action == GLFW_PRESS) printf("key %d scancode %d name '%s'\n", key,
  // scancode, key_name);
#else
  IM_UNUSED(scancode);
#endif
  return key;
}

void GUIWindow::default_glfw_key_callback(GLFWwindow *window, int keycode,
                                          int scancode, int action, int mods) {
  auto &states =
      *static_cast<InteractiveStates *>(glfwGetWindowUserPointer(window));

  /// @note ignore unknown named key
  if (keycode == GLFW_KEY_UNKNOWN)
    return;

  update_imgui_key_modifiers(window);

  keycode = translate_untranslated_key(keycode, scancode);
  if (keycode >= 0 && keycode < IM_ARRAYSIZE(states.keyOwnerWindows))
    states.keyOwnerWindows[keycode] =
        (action == GLFW_PRESS || action == GLFW_REPEAT) ? window : nullptr;

  ImGuiIO &io = ImGui::GetIO();
  ImGuiKey imgui_key = glfw_key_to_imgui_key(keycode);
  io.AddKeyEvent(imgui_key, (action == GLFW_PRESS || action == GLFW_REPEAT));
  // To support legacy indexing (<1.87 user code)
  io.SetKeyEventNativeData(imgui_key, keycode, scancode);

  ///
  switch (action) {
  case GLFW_PRESS: {
    states.keyPressed.setOn(keycode);
    states.keyPressedCb(keycode, 0);
    break;
  }
  case GLFW_RELEASE: {
    states.keyPressed.setOff(keycode);
    states.keyReleasedCb(keycode);
    break;
  }
  case GLFW_REPEAT: {
    states.keyPressed.setOn(keycode);
    states.keyPressedCb(keycode, 1);
    break;
  }
  default:;
  }
}

void GUIWindow::setup_glfw_callbacks(GLFWwindow *window) {
  glfwSetWindowSizeCallback(window, [](GLFWwindow *window, int width,
                                       int height) {
    auto &states =
        *static_cast<InteractiveStates *>(glfwGetWindowUserPointer(window));

    if (ImGuiViewport *viewport = ImGui::FindViewportByPlatformHandle(window)) {
      if (ImGuiPlatformViewportData *vd =
              get_platform_viewport_data(viewport)) {
        bool ignore_event =
            (ImGui::GetFrameCount() <=
             get_platform_viewport_ignore_window_size_event_frame(vd) + 1);
        // data->IgnoreWindowSizeEventFrame = -1;
        if (ignore_event)
          return;
      }
      viewport->PlatformRequestResize = true;
    }

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);

    states.dpiScale = (float)w / (float)width;

    states.width = (zs::u32)(width * states.dpiScale);
    states.height = (zs::u32)(height * states.dpiScale);

    ///
    states.resizeCb(states.width, states.height, states.dpiScale);
  });

  glfwSetWindowPosCallback(window, [](GLFWwindow *window, int, int) {
    auto &states =
        *static_cast<InteractiveStates *>(glfwGetWindowUserPointer(window));

    if (ImGuiViewport *viewport = ImGui::FindViewportByPlatformHandle(window)) {
      if (ImGuiPlatformViewportData *vd =
              get_platform_viewport_data(viewport)) {
        bool ignore_event =
            (ImGui::GetFrameCount() <=
             get_platform_viewport_ignore_window_pos_event_frame(vd) + 1);
        // data->IgnoreWindowPosEventFrame = -1;
        if (ignore_event)
          return;
      }
      viewport->PlatformRequestMove = true;
    }
  });

  glfwSetWindowCloseCallback(window, [](GLFWwindow *window) {
    auto &states =
        *static_cast<InteractiveStates *>(glfwGetWindowUserPointer(window));

    if (ImGuiViewport *viewport = ImGui::FindViewportByPlatformHandle(window))
      viewport->PlatformRequestClose = true;

    ///
    states.closeCb(); // e.g. destroying swapchain
  });

  glfwSetWindowFocusCallback(window, [](GLFWwindow *window, int focused) {
    auto &states =
        *static_cast<InteractiveStates *>(glfwGetWindowUserPointer(window));
    states.focused = focused;

    ImGuiIO &io = ImGui::GetIO();
    io.AddFocusEvent(focused != 0);
    fmt::print("window [{}] focus state [{}].\n", states.title, states.focused);
  });

  glfwSetWindowIconifyCallback(window, [](GLFWwindow *window, int iconified) {
    auto &states =
        *static_cast<InteractiveStates *>(glfwGetWindowUserPointer(window));
    states.focused = !iconified;
    switch (iconified) {
    case GLFW_TRUE:
      states.focused = false;
      break;
    case GLFW_FALSE:
      states.focused = true;
      break;
    default:
      throw std::runtime_error("Unknown window iconify state.");
    }
    fmt::print("window [{}] iconification state [{}] (focused: {}).\n",
               states.title, iconified, states.focused);
  });

  ////////////////////////////////
  ////////// key board ///////////
  ////////////////////////////////
  glfwSetKeyCallback(window, default_glfw_key_callback);

  glfwSetCharCallback(window, [](GLFWwindow *window, unsigned int keycode) {
    // keycode in "Unicode code point"
    auto &states =
        *static_cast<InteractiveStates *>(glfwGetWindowUserPointer(window));

    ImGuiIO &io = ImGui::GetIO();
    io.AddInputCharacter(keycode);

    ///
    states.textCb((char)keycode);
  });

  ////////////////////////////////
  //////////// mouse /////////////
  ////////////////////////////////
  glfwSetMouseButtonCallback(
      window, [](GLFWwindow *window, int button, int action, int mods) {
        auto &states =
            *static_cast<InteractiveStates *>(glfwGetWindowUserPointer(window));

        ImGuiIO &io = ImGui::GetIO();
        update_imgui_key_modifiers(window);
        if (button >= 0 && button < ImGuiMouseButton_COUNT)
          io.AddMouseButtonEvent(button, action == GLFW_PRESS);

        ///
        switch (action) {
        case GLFW_PRESS: {
          states.mousePressedCb(button);
          break;
        }
        case GLFW_RELEASE: {
          states.mouseReleasedCb(button);
          break;
        }
        }
      });

  glfwSetScrollCallback(
      window, [](GLFWwindow *window, double xOffset, double yOffset) {
        auto &states =
            *static_cast<InteractiveStates *>(glfwGetWindowUserPointer(window));

        ImGuiIO &io = ImGui::GetIO();
        io.AddMouseWheelEvent((float)xOffset, (float)yOffset);

        ///
        states.mouseScrollCb(xOffset, yOffset);
      });

  glfwSetCursorPosCallback(
      window, [](GLFWwindow *window, double xPos, double yPos) {
        auto &states =
            *static_cast<InteractiveStates *>(glfwGetWindowUserPointer(window));

        ImGuiIO &io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
          int window_x, window_y;
          glfwGetWindowPos(window, &window_x, &window_y);
          xPos += window_x;
          yPos += window_y;
        }
        io.AddMousePosEvent((float)xPos, (float)yPos);
        states.lastValidMousePos = zs::vec<float, 2>{(float)xPos, (float)yPos};
        ///
        states.mouseMoveCb(xPos /* * states.dpiScale*/,
                           yPos /* * states.dpiScale*/);
      });

  glfwSetCursorEnterCallback(window, [](GLFWwindow *window, int entered) {
    auto &states =
        *static_cast<InteractiveStates *>(glfwGetWindowUserPointer(window));

    ImGuiIO &io = ImGui::GetIO();
    if (entered) {
      states.mouseWindow = window;
      io.AddMousePosEvent(states.lastValidMousePos[0],
                          states.lastValidMousePos[1]);
    } else if (!entered && states.mouseWindow == window) {
      states.mouseWindow = nullptr;
      states.lastValidMousePos[0] = io.MousePos.x;
      states.lastValidMousePos[1] = io.MousePos.y;
      io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
    ///
    states.mouseEnterCb(entered);
  });

  glfwSetDropCallback(
      window, [](GLFWwindow *window, int numDropped, const char **filenames) {
        auto &states =
            *static_cast<InteractiveStates *>(glfwGetWindowUserPointer(window));
        ///
        states.mouseDropCb(numDropped, filenames);
      });
}

} // namespace zs