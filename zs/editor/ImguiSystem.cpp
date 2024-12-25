// #include "fonts/MaterialDesign.inl"
// #include "fonts/RobotoBold.inl"
// #include "fonts/RobotoMedium.inl"
// #include "fonts/RobotoRegular.inl"
#include "imgui.h"
// #include "misc/freetype/imgui_freetype.h"

#include "GuiWindow.hpp"
// #include "IconsFontAwesome6.h"
#include "IconsMaterialDesign.h"
#include "IconsMaterialSymbols.h"
#include "ImguiSystem.hpp"
#include "imgui_node_editor.h"
#include "zensim/zpc_tpls/fmt/color.h"

#define ICON_MIN_MDI 0x30
#define ICON_MAX_MDI 0xFE7D

namespace {
  static const char *ImGui_ImplGlfw_GetClipboardText(void *) {
    return glfwGetClipboardString((GLFWwindow *)ImGui::GetIO().ClipboardUserData);
  }
  static void ImGui_ImplGlfw_SetClipboardText(void *, const char *text) {
    glfwSetClipboardString((GLFWwindow *)ImGui::GetIO().ClipboardUserData, text);
  }
  // static ax::NodeEditor::EditorContext *g_imguiEditor = nullptr;
}  // namespace

namespace zs {

  GraphEditor::GraphEditor(void *config) {
    if (config)
      editor = static_cast<void *>(ax::NodeEditor::CreateEditor(static_cast<ed::Config *>(config)));
    else
      editor = static_cast<void *>(ax::NodeEditor::CreateEditor());
  }
  GraphEditor::~GraphEditor() {
    ax::NodeEditor::DestroyEditor(static_cast<ax::NodeEditor::EditorContext *>(editor));
    editor = nullptr;
  }

  void *ImguiSystem::getNodeEditor(std::string_view tag, void *config) {
    auto t = std::string(tag);
    if (auto it = _editors.find(t); it == _editors.end()) {
      auto [iter, success] = _editors.try_emplace(t, GraphEditor{config});
      if (success)
        // fmt::print(fg(fmt::color::yellow), "created graph [{}]\n", tag);
        fmt::print("created graph [{}]\n", tag);
      return iter->second.editor;
    } else
      return it->second.editor;
  }
  bool ImguiSystem::hasNodeEditor(std::string_view tag) const {
    auto t = std::string(tag);
    return _editors.find(t) != _editors.end();
  }
  void ImguiSystem::eraseNodeEditor(std::string_view tag) {
    auto t = std::string(tag);
    if (_editors.erase(t))
      // fmt::print(fg(fmt::color::red), "removed graph [{}]\n", tag);
      fmt::print("removed graph [{}]\n", tag);
  }
  void ImguiSystem::renameNodeEditor(std::string_view oldTag, std::string_view newTag) {
    auto nh = _editors.extract(std::string(oldTag));
    if (!nh.empty()) {
      nh.key() = newTag;
      _editors.insert(zs::move(nh));
      // fmt::print(fg(fmt::color::green), "renamed graph [{}] to [{}]\n", oldTag,
      // newTag);
      fmt::print("renamed graph [{}] to [{}]\n", oldTag, newTag);
    } else
      // fmt::print(fg(fmt::color::red), "graph [{}] does not exist.\n", oldTag);
      fmt::print("graph [{}] does not exist.\n", oldTag);
  }
  void *ImguiSystem::get_node_editor(std::string_view tag, void *config) {
    return instance().getNodeEditor(tag, config);
  }
  bool ImguiSystem::has_node_editor(std::string_view tag) { return instance().hasNodeEditor(tag); }
  void ImguiSystem::erase_node_editor(std::string_view tag) {
    return instance().eraseNodeEditor(tag);
  }

  void *ImguiSystem::create_node_editor(std::string_view tag) {
    auto &editors = instance()._editors;
    auto t = std::string(tag);
    if (auto it = editors.find(t); it == editors.end()) {
      return editors.try_emplace(t, GraphEditor{}).first->second.editor;
    } else
      return it->second.editor;
  }
  void ImguiSystem::rename_node_editor(std::string_view oldTag, std::string_view newTag) {
    return instance().renameNodeEditor(oldTag, newTag);
  }

  ImguiSystem::ImguiSystem()
      : _fontSize{20}, _initialized{true}, _configFile{zs::abs_exe_directory() + "/zs-gui.ini"} {
    static_assert(sizeof(ImWchar) == sizeof(u32), "ImWchar is supposed to be 32-bit!");

    ImGui::CreateContext();
    // getNodeEditor("Editor");
    // g_imguiEditor = ax::NodeEditor::CreateEditor();
  }

  void ImguiSystem::reset() {
    if (_initialized) {
      _editors.clear();

      // ax::NodeEditor::DestroyEditor(g_imguiEditor);

      ImGui::DestroyContext();
      _initialized = false;
    }
  }

  ImguiSystem::~ImguiSystem() { reset(); }

  static int glfw_mouse_button_to_imgui(int glfwButton);
  static ImGuiKey glfw_key_to_imgui(int key);

  void ImguiSystem::setup(GUIWindow *window) {
    ImGuiIO &io = ImGui::GetIO();

    io.BackendPlatformUserData = &window->states;
    io.BackendPlatformName = window->states.title.data();
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;

#ifndef __EMSCRIPTEN__
    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;  // We can create multi-viewports
                                                                // on the Platform side (optional)
#endif
#if GLFW_HAS_MOUSE_PASSTHROUGH || (GLFW_HAS_WINDOW_HOVERED && defined(_WIN32))
    io.BackendFlags |= ImGuiBackendFlags_HasMouseHoveredViewport;  // We can call
                                                                   // io.AddMouseViewportEvent()
                                                                   // with correct data (optional)
#endif

    io.SetClipboardTextFn = ImGui_ImplGlfw_SetClipboardText;
    io.GetClipboardTextFn = ImGui_ImplGlfw_GetClipboardText;
    io.ClipboardUserData = window->getGlfwWindow();

    const float dpiScale = window->getDPIScale();
    // _fontSize *= dpiScale;

    // io.DisplayFramebufferScale = ImVec2(dpiScale, dpiScale);
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    io.IniFilename = _configFile.c_str();

    /// @note config imgui key/mouse
    io.KeyRepeatDelay = 0.400f;
    io.KeyRepeatRate = 0.05f;

    /// @note set imgui styles
    ImGui::StyleColorsDark();
    io.FontGlobalScale = 1.0f;

    if constexpr (false) {
      ImFontConfig config;
      config.OversampleH = config.OversampleV = 1;
      config.PixelSnapH = true;
      config.SizePixels = _fontSize;
      _fonts.emplace(font_e::default_font, (void *)io.Fonts->AddFontDefault(&config));
    }

    /// @ref
    /// https://github.com/ocornut/imgui/blob/master/docs/FONTS.md#fonts-loading-instructions
    {
#if RESOURCE_AT_RELATIVE_PATH
      auto loc = abs_exe_directory() + "/resource/fonts/AlibabaPuHuiTi-3-65-Medium.ttf";
#else
      // auto loc = std::string{AssetDirPath} + "/resource/fonts/SourceHanSansSC-VF.ttf";
      // auto loc = std::string{AssetDirPath} + "/resource/fonts/FangZhengHeiTi-GBK-1.ttf";
      auto loc = std::string{AssetDirPath} + "/resource/fonts/AlibabaPuHuiTi-3-65-Medium.ttf";
#endif
      ImFontConfig config;
      config.OversampleH = 1;  // FreeType does not support those, reset so
                               // stb_truetype will produce similar results
      config.OversampleV = 1;
      config.MergeMode = false;
      /// @ref https://github.com/ocornut/imgui/pull/6925
      int scale = 1;
      for (int si = 0; si < 1; si++) {
        config.RasterizerDensity = scale;
        auto res = _fonts.emplace(font_e::cn_font + si, (void *)io.Fonts->AddFontFromFileTTF(
                                                            loc.data(), _fontSize, &config,
                                                            io.Fonts->GetGlyphRangesChineseFull()));
        ((ImFont *)res.first->second)->Scale = scale;
        scale *= 2;
      }

      _fonts.emplace(font_e::default_font, _fonts.at(font_e::cn_font));
    }

    // for (int n = 0; n < io.Fonts->ConfigData.Size; n++) {
    //   ImFontConfig *font_config = (ImFontConfig *)&io.Fonts->ConfigData[n];
    //   font_config->RasterizerMultiply = 1.2f;
    // }

#if RESOURCE_AT_RELATIVE_PATH
    auto baseFontDir = abs_exe_directory() + "/resource/fonts/";
#else
    auto baseFontDir = std::string{AssetDirPath} + "/resource/fonts/";
#endif

    // addIconFont();
    {
      // auto iconFontSize = _fontSize * 2.f / 3;
      auto iconFontSize = _fontSize;
      static const ImWchar icons_ranges[] = {ICON_MIN_MD, ICON_MAX_16_MD, 0};
      ImFontConfig icons_config;
      icons_config.OversampleH = icons_config.OversampleV = 1;
      icons_config.MergeMode = true;
      icons_config.PixelSnapH = true;
      icons_config.GlyphOffset = {0.f, iconFontSize / 4};
      icons_config.GlyphExtraSpacing = {0.f, 0.f};
      icons_config.GlyphMinAdvanceX = iconFontSize;
      io.Fonts->AddFontFromFileTTF((baseFontDir + FONT_ICON_FILE_NAME_MD).c_str(), iconFontSize,
                                   &icons_config, icons_ranges);
    }
    {
      // auto iconFontSize = _fontSize * 2.f / 3;
      auto iconFontSize = _fontSize;
      static const ImWchar icons_ranges[] = {ICON_MIN_MS, ICON_MAX_16_MS, 0};
      ImFontConfig icons_config;
      icons_config.OversampleH = icons_config.OversampleV = 1;
      icons_config.MergeMode = true;
      icons_config.PixelSnapH = true;
      icons_config.GlyphOffset = {0, iconFontSize / 6};
      icons_config.GlyphExtraSpacing = {0.f, 0.f};
      icons_config.GlyphMinAdvanceX = iconFontSize;
      io.Fonts->AddFontFromFileTTF((baseFontDir + FONT_ICON_FILE_NAME_MSR).c_str(), iconFontSize,
                                   &icons_config, icons_ranges);
    }

    io.Fonts->TexGlyphPadding = 1;

    io.Fonts->Build();

    ///
    /// imgui style
    ///
    reset_styles();
  }
#if 0
void ImguiSystem::addIconFont() {
  ImGuiIO &io = ImGui::GetIO();

  static const ImWchar icons_ranges[] = {ICON_MIN_MDI, ICON_MAX_MDI, 0};
  ImFontConfig icons_config;
  // merge in icons from Font Awesome
  icons_config.MergeMode = true;
  icons_config.PixelSnapH = true;
  icons_config.GlyphOffset.y = 1.0f;
  icons_config.OversampleH = icons_config.OversampleV = 1;
  icons_config.GlyphMinAdvanceX = 4.0f;
  icons_config.SizePixels = 12.0f;

  io.Fonts->AddFontFromMemoryCompressedTTF(
      MaterialDesign_compressed_data, MaterialDesign_compressed_size, _fontSize,
      &icons_config, icons_ranges);
}
#endif

  void *ImguiSystem::get_window() noexcept { return ImGui::GetIO().ClipboardUserData; }

  void ImguiSystem::reset_styles() {
    ImGuiIO &io = ImGui::GetIO();
    ImGuiStyle &style = ImGui::GetStyle();

    style.WindowPadding = ImVec2(5, 5);
    style.WindowRounding = 8;
    style.FramePadding = ImVec2(4, 4);
    style.ItemSpacing = ImVec2(6, 2);
    style.ItemInnerSpacing = ImVec2(2, 2);
    style.IndentSpacing = 6.0f;
    style.TouchExtraPadding = ImVec2(4, 4);

    style.ScrollbarSize = 16;
    style.DockingSeparatorSize = 0;

    style.WindowBorderSize = 2;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 3;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarOverlineSize = 0.0f;
    style.TabBarBorderSize = 0.0f;

    style.WindowMenuButtonPosition = ImGuiDir_Right;
    style.PopupRounding = 2;
    style.FrameRounding = 6;
    style.ChildRounding = style.FrameRounding;
    style.ScrollbarRounding = 12;
    style.TabRounding = 8;
    style.GrabRounding = 2;
    style.WindowMinSize = ImVec2(200.0f, 200.0f);
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);

    /// @ref Hazelnut
    if constexpr (true) {
      auto &colors = style.Colors;
      colors[ImGuiCol_WindowBg] = g_dark_color;  // ImVec4{0.1f, 0.105f, 0.11f, 1.0f};

      // Headers
      colors[ImGuiCol_Header] = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
      colors[ImGuiCol_HeaderHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
      colors[ImGuiCol_HeaderActive] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

      // Buttons
      colors[ImGuiCol_Button] = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
      colors[ImGuiCol_ButtonHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
      colors[ImGuiCol_ButtonActive] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

      // Menu
      colors[ImGuiCol_MenuBarBg] = g_almost_dark_color;  // ImVec4{0.2f, 0.205f, 0.21f, 1.0f};

      // Frame BG
      colors[ImGuiCol_FrameBg] = g_dark_color;  // ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
      colors[ImGuiCol_FrameBgHovered] = g_dark_color + ImVec4{0.1f, 0.1f, 0.1f, 0.0f};
      colors[ImGuiCol_FrameBgActive]
          = colors[ImGuiCol_FrameBgHovered] + ImVec4{0.1f, 0.1f, 0.1f, 0.0f};

      // Tabs
      colors[ImGuiCol_Tab] = ImColor{33, 33, 33, 255};
      colors[ImGuiCol_TabHovered] = ImVec4{0.38f, 0.3805f, 0.381f, 1.0f};
      // colors[ImGuiCol_TabActive] = ImVec4{0.28f, 0.2805f, 0.281f, 1.0f};
      colors[ImGuiCol_TabActive] = ImColor{66, 66, 66, 255};
      colors[ImGuiCol_TabUnfocused] = colors[ImGuiCol_Tab];
      colors[ImGuiCol_TabUnfocusedActive] = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
      // colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4{0.2f, 0.805f, 0.21f, 1.0f};

      // Title
      colors[ImGuiCol_TitleBg] = colors[ImGuiCol_WindowBg];
      // colors[ImGuiCol_TitleBg] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
      colors[ImGuiCol_TitleBgActive] = colors[ImGuiCol_TitleBg];
      colors[ImGuiCol_TitleBgCollapsed] = colors[ImGuiCol_TitleBg];
    }

    // dock
    {
      if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
      }
    }
  }
  ///
  ///
  ///
  static int glfw_mouse_button_to_imgui(int glfwButton) {
    if (glfwButton == GLFW_MOUSE_BUTTON_LEFT)
      return 0;
    else if (glfwButton == GLFW_MOUSE_BUTTON_RIGHT)
      return 1;
    else if (glfwButton == GLFW_MOUSE_BUTTON_MIDDLE)
      return 2;
    else
      return 4;
  }
  static ImGuiKey glfw_key_to_imgui(int key) {
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

}  // namespace zs